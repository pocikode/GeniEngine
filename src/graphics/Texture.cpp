#include "graphics/Texture.h"
#include "core/Engine.h"
#include "core/Log.h"
#include "rhi/Device.h"
#include <algorithm>
#include <memory>
#include <vector>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace Geni
{

namespace
{

// Expands 1-4 channels to RGBA, the way OpenGL widened RGB uploads: missing color
// channels repeat the gray value, missing alpha is opaque.
std::vector<uint8_t> ToRgba(int width, int height, int channels, const unsigned char *data)
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

// Next mip level: average of each 2x2 block (box filter), clamped at odd edges.
std::vector<uint8_t> Downsample(const std::vector<uint8_t> &src, int width, int height, int &outWidth, int &outHeight)
{
    outWidth = std::max(1, width / 2);
    outHeight = std::max(1, height / 2);
    std::vector<uint8_t> dst(size_t(outWidth) * size_t(outHeight) * 4);
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
                const int sum = src[(size_t(y0) * width + x0) * 4 + c] + src[(size_t(y0) * width + x1) * 4 + c] +
                                src[(size_t(y1) * width + x0) * 4 + c] + src[(size_t(y1) * width + x1) * 4 + c];
                dst[(size_t(y) * outWidth + x) * 4 + c] = static_cast<uint8_t>((sum + 2) / 4);
            }
        }
    }
    return dst;
}

} // namespace

Texture::Texture(int width, int height, int numChannels, const unsigned char *data)
    : m_width(width), m_height(height)
{
    rhi::Device *device = Engine::GetInstance().GetRenderer().GetDevice();
    if (!device || width <= 0 || height <= 0 || !data || numChannels < 1 || numChannels > 4)
    {
        GENI_LOG_ERROR("Texture %dx%d (%d channels) not created: %s", width, height, numChannels,
                       device ? "invalid image" : "no render device");
        return;
    }

    std::vector<std::vector<uint8_t>> levels;
    levels.push_back(ToRgba(width, height, numChannels, data));
    int w = width;
    int h = height;
    while (w > 1 || h > 1)
    {
        int nextW = 0;
        int nextH = 0;
        levels.push_back(Downsample(levels.back(), w, h, nextW, nextH));
        w = nextW;
        h = nextH;
    }

    std::vector<const void *> mipData;
    for (const auto &level : levels)
    {
        mipData.push_back(level.data());
    }

    rhi::TextureDesc desc;
    desc.width = static_cast<uint32_t>(width);
    desc.height = static_cast<uint32_t>(height);
    desc.mipLevels = static_cast<uint32_t>(levels.size());
    desc.format = rhi::Format::RGBA8Unorm;
    desc.debugName = "texture";
    m_handle = device->CreateTexture(desc, mipData.data());
    m_mipLevels = static_cast<int>(levels.size());
}

Texture::~Texture()
{
    if (rhi::Device *device = Engine::GetInstance().GetRenderer().GetDevice())
    {
        device->DestroyTexture(m_handle);
    }
}

std::shared_ptr<Texture> Texture::Load(const std::string &path)
{
    int width, height, numChannels;

    auto &fs = Geni::Engine::GetInstance().GetFileSystem();
    auto fullPath = fs.ResolveAssetPath(path);

    std::error_code ec;
    if (!std::filesystem::exists(fullPath, ec))
    {
        return nullptr;
    }

    std::shared_ptr<Texture> result;

    unsigned char *data = stbi_load(fullPath.string().c_str(), &width, &height, &numChannels, 0);
    if (data)
    {
        result = std::make_shared<Texture>(width, height, numChannels, data);
        stbi_image_free(data);
        if (!result->GetHandle().IsValid())
        {
            result.reset();
        }
    }

    return result;
}

std::shared_ptr<Texture> TextureManager::GetOrLoadTexture(const std::string &path)
{
    auto it = m_textures.find(path);
    if (it != m_textures.end())
    {
        return it->second;
    }

    auto texture = Texture::Load(path);
    m_textures[path] = texture;

    return texture;
}

std::shared_ptr<Texture> TextureManager::GetWhiteTexture()
{
    if (!m_whiteTexture)
    {
        unsigned char pixels[4] = {255, 255, 255, 255};
        m_whiteTexture = std::make_shared<Texture>(1, 1, 4, pixels);
    }
    return m_whiteTexture;
}

void TextureManager::Clear()
{
    m_textures.clear();
    m_whiteTexture.reset();
}

} // namespace Geni
