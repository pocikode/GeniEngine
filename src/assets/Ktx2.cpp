#include "assets/Ktx2.h"

#include <cstring>

namespace Geni
{

namespace
{

constexpr uint8_t kIdentifier[12] = {0xAB, 0x4B, 0x54, 0x58, 0x20, 0x32, 0x30, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A};
constexpr uint32_t kHeaderSize = 80;
constexpr uint32_t kLevelIndexEntrySize = 24;

uint32_t ReadU32(const uint8_t *data)
{
    uint32_t value = 0;
    std::memcpy(&value, data, 4);
    return value;
}

uint64_t ReadU64(const uint8_t *data)
{
    uint64_t value = 0;
    std::memcpy(&value, data, 8);
    return value;
}

bool Fail(std::string *error, const char *message)
{
    if (error)
    {
        *error = message;
    }
    return false;
}

rhi::Format FromVkFormat(uint32_t format)
{
    // Vulkan core format values, as written by the cooker.
    switch (format)
    {
    case 131: // VK_FORMAT_BC1_RGB_UNORM_BLOCK
    case 133: // VK_FORMAT_BC1_RGBA_UNORM_BLOCK
        return rhi::Format::BC1Unorm;
    case 137: // VK_FORMAT_BC3_UNORM_BLOCK
        return rhi::Format::BC3Unorm;
    case 139: // VK_FORMAT_BC4_UNORM_BLOCK
        return rhi::Format::BC4Unorm;
    case 141: // VK_FORMAT_BC5_UNORM_BLOCK
        return rhi::Format::BC5Unorm;
    case 145: // VK_FORMAT_BC7_UNORM_BLOCK
        return rhi::Format::BC7Unorm;
    case 157: // VK_FORMAT_ASTC_4x4_UNORM_BLOCK
        return rhi::Format::ASTC4x4Unorm;
    case 37: // VK_FORMAT_R8G8B8A8_UNORM
        return rhi::Format::RGBA8Unorm;
    case 43: // VK_FORMAT_R8G8B8A8_SRGB
        return rhi::Format::RGBA8Srgb;
    case 146: // VK_FORMAT_BC7_SRGB_BLOCK
        return rhi::Format::BC7Srgb;
    case 158: // VK_FORMAT_ASTC_4x4_SRGB_BLOCK
        return rhi::Format::ASTC4x4Srgb;
    default:
        return rhi::Format::Undefined;
    }
}

} // namespace

bool ReadKtx2(const std::vector<uint8_t> &bytes, Ktx2Image &out, std::string *error)
{
    out = Ktx2Image{};
    if (error)
    {
        error->clear();
    }
    if (bytes.size() < kHeaderSize || std::memcmp(bytes.data(), kIdentifier, sizeof(kIdentifier)) != 0)
    {
        return Fail(error, "not a KTX2 file");
    }

    const uint8_t *header = bytes.data() + sizeof(kIdentifier);
    const uint32_t vkFormat = ReadU32(header + 0);
    const uint32_t width = ReadU32(header + 8);
    const uint32_t height = ReadU32(header + 12);
    const uint32_t layerCount = ReadU32(header + 20);
    const uint32_t faceCount = ReadU32(header + 24);
    uint32_t levelCount = ReadU32(header + 28);
    const uint32_t scheme = ReadU32(header + 32);

    if (width == 0 || height == 0)
    {
        return Fail(error, "KTX2 image has no pixels");
    }
    if (layerCount > 1 || faceCount > 1)
    {
        return Fail(error, "KTX2 arrays and cubemaps are not supported");
    }
    if (scheme != 0)
    {
        return Fail(error, "supercompressed KTX2 is not supported");
    }
    if (levelCount == 0)
    {
        levelCount = 1;
    }
    if (levelCount > 32)
    {
        return Fail(error, "KTX2 image has too many mip levels");
    }

    out.format = FromVkFormat(vkFormat);
    if (out.format == rhi::Format::Undefined)
    {
        return Fail(error, "unsupported KTX2 pixel format");
    }
    out.width = width;
    out.height = height;
    out.mipLevels = levelCount;

    const uint64_t indexBytes = uint64_t(levelCount) * kLevelIndexEntrySize;
    if (uint64_t(kHeaderSize) + indexBytes > bytes.size())
    {
        return Fail(error, "KTX2 level index is truncated");
    }

    out.levels.resize(levelCount);
    for (uint32_t level = 0; level < levelCount; ++level)
    {
        const uint8_t *entry = bytes.data() + kHeaderSize + uint64_t(level) * kLevelIndexEntrySize;
        const uint64_t offset = ReadU64(entry + 0);
        const uint64_t length = ReadU64(entry + 8);
        if (offset > bytes.size() || length > bytes.size() - offset)
        {
            return Fail(error, "KTX2 level data lies outside the file");
        }
        const uint32_t levelWidth = width >> level;
        const uint32_t levelHeight = height >> level;
        const uint64_t expected =
            rhi::TextureLevelSize(out.format, levelWidth > 0 ? levelWidth : 1, levelHeight > 0 ? levelHeight : 1);
        if (length < expected)
        {
            return Fail(error, "KTX2 level is shorter than its format requires");
        }
        out.levels[level].assign(
            bytes.begin() + static_cast<ptrdiff_t>(offset), bytes.begin() + static_cast<ptrdiff_t>(offset + length)
        );
    }

    return true;
}

} // namespace Geni
