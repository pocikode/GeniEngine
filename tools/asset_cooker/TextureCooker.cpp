#include "TextureCooker.h"
#include "Ktx2Writer.h"

#include "astcenc.h"
#include "bc7enc.h"
#include "rgbcx.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <mutex>
#include <utility>
#include <vector>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace Geni::cook
{

namespace
{

void EnsureEncoders()
{
    static std::once_flag once;
    std::call_once(once, [] {
        rgbcx::init();
        bc7enc_compress_block_init();
    });
}

void SetError(std::string *error, const std::string &message)
{
    if (error)
    {
        *error = message;
    }
}

std::vector<uint8_t> ToRgba(const unsigned char *data, int width, int height, int channels)
{
    const size_t pixels = size_t(width) * size_t(height);
    std::vector<uint8_t> rgba(pixels * 4);
    for (size_t p = 0; p < pixels; ++p)
    {
        const unsigned char *src = data + p * channels;
        uint8_t *dst = &rgba[p * 4];
        switch (channels)
        {
        case 1:
            dst[0] = dst[1] = dst[2] = src[0];
            dst[3] = 255;
            break;
        case 2:
            dst[0] = dst[1] = dst[2] = src[0];
            dst[3] = src[1];
            break;
        case 3:
            dst[0] = src[0];
            dst[1] = src[1];
            dst[2] = src[2];
            dst[3] = 255;
            break;
        default:
            dst[0] = src[0];
            dst[1] = src[1];
            dst[2] = src[2];
            dst[3] = src[3];
            break;
        }
    }
    return rgba;
}

float SrgbToLinear(float c)
{
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

float LinearToSrgb(float c)
{
    return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
}

uint8_t ToByte(float value)
{
    return static_cast<uint8_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
}

// Texels as floats in the space the filter works in: linear light for colour (alpha
// stays linear), [-1, 1] vectors for normal maps, the stored value for data.
std::vector<float> Decode(const std::vector<uint8_t> &rgba, TextureUsage usage)
{
    std::array<float, 256> srgb{};
    for (int i = 0; i < 256; ++i)
    {
        srgb[i] = SrgbToLinear(i / 255.0f);
    }
    std::vector<float> out(rgba.size());
    for (size_t i = 0; i < rgba.size(); ++i)
    {
        const bool alpha = i % 4 == 3;
        const float value = rgba[i] / 255.0f;
        switch (usage)
        {
        case TextureUsage::Color:
            out[i] = alpha ? value : srgb[rgba[i]];
            break;
        case TextureUsage::Normal:
            out[i] = alpha ? value : value * 2.0f - 1.0f;
            break;
        case TextureUsage::Linear:
            out[i] = value;
            break;
        }
    }
    return out;
}

std::vector<uint8_t> Encode(const std::vector<float> &texels, TextureUsage usage)
{
    std::vector<uint8_t> out(texels.size());
    for (size_t p = 0; p < texels.size(); p += 4)
    {
        const float *t = &texels[p];
        switch (usage)
        {
        case TextureUsage::Color:
            for (int c = 0; c < 3; ++c)
            {
                out[p + c] = ToByte(LinearToSrgb(t[c]));
            }
            break;
        case TextureUsage::Normal:
        {
            // Averaging shortens the vectors; a mip must still hold unit normals.
            float x = t[0];
            float y = t[1];
            float z = t[2];
            const float length = std::sqrt(x * x + y * y + z * z);
            if (length > 1e-6f)
            {
                x /= length;
                y /= length;
                z /= length;
            }
            else
            {
                x = y = 0.0f;
                z = 1.0f;
            }
            out[p + 0] = ToByte(x * 0.5f + 0.5f);
            out[p + 1] = ToByte(y * 0.5f + 0.5f);
            out[p + 2] = ToByte(z * 0.5f + 0.5f);
            break;
        }
        case TextureUsage::Linear:
            for (int c = 0; c < 3; ++c)
            {
                out[p + c] = ToByte(t[c]);
            }
            break;
        }
        out[p + 3] = ToByte(t[3]);
    }
    return out;
}

std::vector<float> Downsample(const std::vector<float> &src, int width, int height, int &outWidth, int &outHeight)
{
    outWidth = std::max(1, width / 2);
    outHeight = std::max(1, height / 2);
    std::vector<float> dst(size_t(outWidth) * size_t(outHeight) * 4);
    for (int y = 0; y < outHeight; ++y)
    {
        const int y0 = std::min(y * 2, height - 1);
        const int y1 = std::min(y * 2 + 1, height - 1);
        for (int x = 0; x < outWidth; ++x)
        {
            const int x0 = std::min(x * 2, width - 1);
            const int x1 = std::min(x * 2 + 1, width - 1);
            for (int c = 0; c < 4; ++c)
            {
                const float sum = src[(size_t(y0) * width + x0) * 4 + c] + src[(size_t(y0) * width + x1) * 4 + c] +
                                  src[(size_t(y1) * width + x0) * 4 + c] + src[(size_t(y1) * width + x1) * 4 + c];
                dst[(size_t(y) * outWidth + x) * 4 + c] = sum * 0.25f;
            }
        }
    }
    return dst;
}

void GatherBlock(const std::vector<uint8_t> &pixels, int width, int height, int blockX, int blockY, uint8_t out[64])
{
    for (int y = 0; y < 4; ++y)
    {
        for (int x = 0; x < 4; ++x)
        {
            const int sx = std::min(blockX * 4 + x, width - 1);
            const int sy = std::min(blockY * 4 + y, height - 1);
            std::memcpy(out + (y * 4 + x) * 4, pixels.data() + (size_t(sy) * width + sx) * 4, 4);
        }
    }
}

std::vector<uint8_t> EncodeBc5(const std::vector<uint8_t> &pixels, int width, int height)
{
    const int blocksX = (width + 3) / 4;
    const int blocksY = (height + 3) / 4;
    std::vector<uint8_t> out(size_t(blocksX) * blocksY * 16);
    uint8_t block[64];
    for (int by = 0; by < blocksY; ++by)
    {
        for (int bx = 0; bx < blocksX; ++bx)
        {
            GatherBlock(pixels, width, height, bx, by, block);
            rgbcx::encode_bc5_hq(out.data() + (size_t(by) * blocksX + bx) * 16, block, 0, 1, 4);
        }
    }
    return out;
}

std::vector<uint8_t> EncodeBc7(const std::vector<uint8_t> &pixels, int width, int height, bool perceptual)
{
    const int blocksX = (width + 3) / 4;
    const int blocksY = (height + 3) / 4;
    std::vector<uint8_t> out(size_t(blocksX) * blocksY * 16);
    bc7enc_compress_block_params params;
    bc7enc_compress_block_params_init(&params);
    // Perceptual weights assume sRGB-encoded colour; data textures need equal weights.
    if (perceptual)
    {
        bc7enc_compress_block_params_init_perceptual_weights(&params);
    }
    else
    {
        bc7enc_compress_block_params_init_linear_weights(&params);
    }
    uint8_t block[64];
    for (int by = 0; by < blocksY; ++by)
    {
        for (int bx = 0; bx < blocksX; ++bx)
        {
            GatherBlock(pixels, width, height, bx, by, block);
            bc7enc_compress_block(out.data() + (size_t(by) * blocksX + bx) * 16, block, &params);
        }
    }
    return out;
}

// One astc-encoder context per texture, reset between mip levels.
class AstcEncoder
{
  public:
    AstcEncoder(TextureUsage usage, TextureQuality quality)
    {
        const astcenc_profile profile = usage == TextureUsage::Color ? ASTCENC_PRF_LDR_SRGB : ASTCENC_PRF_LDR;
        const float preset = quality == TextureQuality::Fast       ? ASTCENC_PRE_FAST
                             : quality == TextureQuality::Thorough ? ASTCENC_PRE_THOROUGH
                                                                   : ASTCENC_PRE_MEDIUM;
        astcenc_config config{};
        if (astcenc_config_init(profile, 4, 4, 1, preset, 0, &config) != ASTCENC_SUCCESS)
        {
            m_error = "astcenc_config_init failed";
            return;
        }
        if (astcenc_context_alloc(&config, 1, &m_context, nullptr) != ASTCENC_SUCCESS)
        {
            m_context = nullptr;
            m_error = "astcenc_context_alloc failed";
        }
    }

    ~AstcEncoder()
    {
        if (m_context)
        {
            astcenc_context_free(m_context);
        }
    }

    AstcEncoder(const AstcEncoder &) = delete;
    AstcEncoder &operator=(const AstcEncoder &) = delete;

    std::vector<uint8_t> Encode(const std::vector<uint8_t> &pixels, int width, int height, std::string *error)
    {
        if (!m_context)
        {
            SetError(error, m_error);
            return {};
        }
        const int blocksX = (width + 3) / 4;
        const int blocksY = (height + 3) / 4;
        std::vector<uint8_t> out(size_t(blocksX) * blocksY * 16);

        astcenc_image image{};
        image.dim_x = static_cast<unsigned int>(width);
        image.dim_y = static_cast<unsigned int>(height);
        image.dim_z = 1;
        image.data_type = ASTCENC_TYPE_U8;
        void *slice = const_cast<uint8_t *>(pixels.data());
        image.data = &slice;
        const astcenc_swizzle swizzle{ASTCENC_SWZ_R, ASTCENC_SWZ_G, ASTCENC_SWZ_B, ASTCENC_SWZ_A};

        const astcenc_error result = astcenc_compress_image(m_context, &image, &swizzle, out.data(), out.size(), 0);
        astcenc_compress_reset(m_context);
        if (result != ASTCENC_SUCCESS)
        {
            SetError(error, std::string("astcenc_compress_image failed: ") + astcenc_get_error_string(result));
            return {};
        }
        return out;
    }

  private:
    astcenc_context *m_context = nullptr;
    std::string m_error;
};

bool WriteRelative(
    const std::filesystem::path &outputRoot, const std::string &relative, const std::vector<uint8_t> &bytes,
    std::string *error
)
{
    const std::filesystem::path path = outputRoot / relative;
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file)
    {
        SetError(error, "cannot write " + path.string());
        return false;
    }
    if (!bytes.empty())
    {
        file.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    return file.good();
}

} // namespace

const char *TextureUsageName(TextureUsage usage)
{
    switch (usage)
    {
    case TextureUsage::Linear:
        return "linear";
    case TextureUsage::Normal:
        return "normal";
    case TextureUsage::Color:
        break;
    }
    return "color";
}

bool TextureUsageFromName(const std::string &name, TextureUsage &out)
{
    if (name == "color")
        out = TextureUsage::Color;
    else if (name == "linear")
        out = TextureUsage::Linear;
    else if (name == "normal")
        out = TextureUsage::Normal;
    else
        return false;
    return true;
}

bool TextureQualityFromName(const std::string &name, TextureQuality &out)
{
    if (name == "fast")
        out = TextureQuality::Fast;
    else if (name == "medium")
        out = TextureQuality::Medium;
    else if (name == "thorough")
        out = TextureQuality::Thorough;
    else
        return false;
    return true;
}

std::vector<std::vector<uint8_t>> BuildMipChain(
    const std::vector<uint8_t> &rgba, int width, int height, TextureUsage usage,
    std::vector<std::pair<int, int>> &sizes
)
{
    // Filter in float and quantize each level from the float chain, so rounding does not
    // accumulate down the chain.
    std::vector<std::vector<uint8_t>> levels;
    sizes.clear();
    std::vector<float> texels = Decode(rgba, usage);
    levels.push_back(rgba);
    sizes.emplace_back(width, height);
    while (sizes.back().first > 1 || sizes.back().second > 1)
    {
        int nextWidth = 0;
        int nextHeight = 0;
        texels = Downsample(texels, sizes.back().first, sizes.back().second, nextWidth, nextHeight);
        levels.push_back(Encode(texels, usage));
        sizes.emplace_back(nextWidth, nextHeight);
    }
    if (usage == TextureUsage::Normal)
    {
        // Renormalize the base level too, so every level holds unit vectors.
        levels[0] = Encode(Decode(rgba, usage), usage);
    }
    return levels;
}

bool CookTexture(
    const std::vector<uint8_t> &fileBytes, const std::string &outputStem, const std::filesystem::path &outputRoot,
    const TextureCookSettings &settings, CookedTextureFiles &out, std::string *error
)
{
    EnsureEncoders();
    if (error)
    {
        error->clear();
    }

    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char *decoded =
        stbi_load_from_memory(fileBytes.data(), static_cast<int>(fileBytes.size()), &width, &height, &channels, 0);
    if (!decoded)
    {
        SetError(error, "cannot decode image");
        return false;
    }
    const std::vector<uint8_t> rgba = ToRgba(decoded, width, height, channels);
    stbi_image_free(decoded);

    const TextureUsage usage = settings.usage;
    std::vector<std::pair<int, int>> sizes;
    const std::vector<std::vector<uint8_t>> rgbaLevels = BuildMipChain(rgba, width, height, usage, sizes);

    AstcEncoder astc(usage, settings.quality);
    std::vector<std::vector<uint8_t>> bcLevels;
    std::vector<std::vector<uint8_t>> astcLevels;
    bcLevels.reserve(rgbaLevels.size());
    astcLevels.reserve(rgbaLevels.size());
    for (size_t level = 0; level < rgbaLevels.size(); ++level)
    {
        const auto [levelWidth, levelHeight] = sizes[level];
        if (usage == TextureUsage::Normal)
        {
            bcLevels.push_back(EncodeBc5(rgbaLevels[level], levelWidth, levelHeight));
        }
        else
        {
            bcLevels.push_back(EncodeBc7(rgbaLevels[level], levelWidth, levelHeight, usage == TextureUsage::Color));
        }
        astcLevels.push_back(astc.Encode(rgbaLevels[level], levelWidth, levelHeight, error));
        if (astcLevels.back().empty())
        {
            return false;
        }
    }

    const bool srgb = usage == TextureUsage::Color;
    const Ktx2Format rgbaFormat = srgb ? Ktx2Format::RGBA8Srgb : Ktx2Format::RGBA8;
    const Ktx2Format bcFormat =
        usage == TextureUsage::Normal ? Ktx2Format::BC5 : (srgb ? Ktx2Format::BC7Srgb : Ktx2Format::BC7);
    const Ktx2Format astcFormat = srgb ? Ktx2Format::ASTC4x4Srgb : Ktx2Format::ASTC4x4;

    std::vector<uint8_t> rgbaFile;
    std::vector<uint8_t> bcFile;
    std::vector<uint8_t> astcFile;
    const auto w = static_cast<uint32_t>(width);
    const auto h = static_cast<uint32_t>(height);
    WriteKtx2(rgbaFormat, w, h, rgbaLevels, rgbaFile);
    WriteKtx2(bcFormat, w, h, bcLevels, bcFile);
    WriteKtx2(astcFormat, w, h, astcLevels, astcFile);

    out.rgba8 = outputStem + "_rgba8.ktx2";
    out.bc = outputStem + (usage == TextureUsage::Normal ? "_bc5.ktx2" : "_bc7.ktx2");
    out.astc = outputStem + "_astc.ktx2";
    return WriteRelative(outputRoot, out.rgba8, rgbaFile, error) && WriteRelative(outputRoot, out.bc, bcFile, error) &&
           WriteRelative(outputRoot, out.astc, astcFile, error);
}

} // namespace Geni::cook
