#include "Ktx2Writer.h"

#include <algorithm>
#include <cstring>
#include <numeric>
#include <string>

namespace Geni::cook
{

namespace
{

constexpr uint8_t kIdentifier[12] = {0xAB, 0x4B, 0x54, 0x58, 0x20, 0x32, 0x30, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A};

// Khronos Data Format constants (khr_df.h).
constexpr uint8_t kModelRGBSDA = 1;
constexpr uint8_t kModelBC5 = 132;
constexpr uint8_t kModelBC7 = 134;
constexpr uint8_t kModelASTC = 162;
constexpr uint8_t kPrimariesBT709 = 1;
constexpr uint8_t kTransferLinear = 1;
constexpr uint8_t kTransferSRGB = 2;
constexpr uint8_t kChannelRed = 0;
constexpr uint8_t kChannelGreen = 1;
constexpr uint8_t kChannelBlue = 2;
constexpr uint8_t kChannelAlpha = 15;
constexpr uint8_t kQualifierLinear = 0x10; // alpha in an sRGB format is not sRGB-encoded

struct Sample
{
    uint16_t bitOffset = 0;
    uint8_t bitLength = 0; // number of bits - 1
    uint8_t channelType = 0;
    uint32_t lower = 0;
    uint32_t upper = 0;
};

struct FormatInfo
{
    uint32_t vkFormat = 0;
    uint32_t blockBytes = 4;
    bool compressed = false;
    uint8_t colorModel = kModelRGBSDA;
    uint8_t transfer = kTransferLinear;
    std::vector<Sample> samples;
};

std::vector<Sample> Rgba8Samples(bool srgb)
{
    return {
        {0, 7, kChannelRed, 0, 255},
        {8, 7, kChannelGreen, 0, 255},
        {16, 7, kChannelBlue, 0, 255},
        {24, 7, static_cast<uint8_t>(kChannelAlpha | (srgb ? kQualifierLinear : 0)), 0, 255},
    };
}

FormatInfo InfoFor(Ktx2Format format)
{
    constexpr uint32_t full = 0xFFFFFFFFu;
    switch (format)
    {
    case Ktx2Format::RGBA8Srgb:
        return {43, 4, false, kModelRGBSDA, kTransferSRGB, Rgba8Samples(true)}; // VK_FORMAT_R8G8B8A8_SRGB
    case Ktx2Format::BC5: // VK_FORMAT_BC5_UNORM_BLOCK
        return {141, 16, true, kModelBC5, kTransferLinear, {{0, 63, kChannelRed, 0, full}, {64, 63, kChannelGreen, 0, full}}};
    case Ktx2Format::BC7: // VK_FORMAT_BC7_UNORM_BLOCK
        return {145, 16, true, kModelBC7, kTransferLinear, {{0, 127, 0, 0, full}}};
    case Ktx2Format::BC7Srgb: // VK_FORMAT_BC7_SRGB_BLOCK
        return {146, 16, true, kModelBC7, kTransferSRGB, {{0, 127, 0, 0, full}}};
    case Ktx2Format::ASTC4x4: // VK_FORMAT_ASTC_4x4_UNORM_BLOCK
        return {157, 16, true, kModelASTC, kTransferLinear, {{0, 127, 0, 0, full}}};
    case Ktx2Format::ASTC4x4Srgb: // VK_FORMAT_ASTC_4x4_SRGB_BLOCK
        return {158, 16, true, kModelASTC, kTransferSRGB, {{0, 127, 0, 0, full}}};
    case Ktx2Format::RGBA8:
    default:
        return {37, 4, false, kModelRGBSDA, kTransferLinear, Rgba8Samples(false)}; // VK_FORMAT_R8G8B8A8_UNORM
    }
}

void Append(std::vector<uint8_t> &out, const void *data, size_t size)
{
    const size_t offset = out.size();
    out.resize(offset + size);
    if (size > 0)
    {
        std::memcpy(out.data() + offset, data, size);
    }
}

void AppendU16(std::vector<uint8_t> &out, uint16_t value)
{
    Append(out, &value, 2);
}

void AppendU32(std::vector<uint8_t> &out, uint32_t value)
{
    Append(out, &value, 4);
}

void AppendU64(std::vector<uint8_t> &out, uint64_t value)
{
    Append(out, &value, 8);
}

void PadTo(std::vector<uint8_t> &out, size_t alignment)
{
    while (out.size() % alignment != 0)
    {
        out.push_back(0);
    }
}

size_t AlignUp(size_t value, size_t alignment)
{
    return (value + alignment - 1) / alignment * alignment;
}

std::vector<uint8_t> BuildDfd(const FormatInfo &info)
{
    // dfdTotalSize, then one basic descriptor block: a 24-byte header and 16 bytes per
    // sample (Khronos Data Format Specification 1.3, section 5).
    const uint32_t blockSize = 24 + 16 * static_cast<uint32_t>(info.samples.size());
    std::vector<uint8_t> dfd;
    AppendU32(dfd, 4 + blockSize);
    AppendU32(dfd, 0); // vendorId KHR (0), descriptorType basic (0)
    AppendU16(dfd, 2); // versionNumber: KHR_DF_VERSIONNUMBER_1_3
    AppendU16(dfd, static_cast<uint16_t>(blockSize));
    dfd.push_back(info.colorModel);
    dfd.push_back(kPrimariesBT709);
    dfd.push_back(info.transfer);
    dfd.push_back(0); // flags: straight alpha
    // texelBlockDimension0..3, each stored as size - 1.
    dfd.push_back(info.compressed ? 3 : 0);
    dfd.push_back(info.compressed ? 3 : 0);
    dfd.push_back(0);
    dfd.push_back(0);
    // bytesPlane0..7.
    dfd.push_back(static_cast<uint8_t>(info.blockBytes));
    for (int i = 1; i < 8; ++i)
    {
        dfd.push_back(0);
    }
    for (const Sample &sample : info.samples)
    {
        AppendU16(dfd, sample.bitOffset);
        dfd.push_back(sample.bitLength);
        dfd.push_back(sample.channelType);
        AppendU32(dfd, 0); // samplePosition0..3
        AppendU32(dfd, sample.lower);
        AppendU32(dfd, sample.upper);
    }
    return dfd;
}

std::vector<uint8_t> BuildKeyValueData()
{
    // One entry: keyAndValueByteLength, "KTXwriter\0", the value with its NUL, then
    // padding to 4 bytes (not counted in the length).
    const std::string key = "KTXwriter";
    const std::string value = "GeniEngine geni_asset_cooker";
    std::vector<uint8_t> kvd;
    AppendU32(kvd, static_cast<uint32_t>(key.size() + 1 + value.size() + 1));
    Append(kvd, key.c_str(), key.size() + 1);
    Append(kvd, value.c_str(), value.size() + 1);
    PadTo(kvd, 4);
    return kvd;
}

} // namespace

void WriteKtx2(
    Ktx2Format format, uint32_t width, uint32_t height, const std::vector<std::vector<uint8_t>> &levels,
    std::vector<uint8_t> &out
)
{
    out.clear();
    const FormatInfo info = InfoFor(format);
    const uint32_t levelCount = static_cast<uint32_t>(std::max<size_t>(1, levels.size()));
    const size_t headerSize = 80;
    const size_t indexSize = size_t(levelCount) * 24;
    const std::vector<uint8_t> dfd = BuildDfd(info);
    const std::vector<uint8_t> kvd = BuildKeyValueData();
    const size_t dfdOffset = headerSize + indexSize;
    const size_t kvdOffset = dfdOffset + dfd.size();

    // Every level starts at a multiple of lcm(texel block size, 4). The specification
    // stores levels smallest first, so the base level is last in the file.
    const size_t alignment = std::lcm<size_t>(info.blockBytes, 4);
    std::vector<uint64_t> offsets(levelCount, 0);
    std::vector<uint64_t> sizes(levelCount, 0);
    size_t cursor = kvdOffset + kvd.size();
    for (uint32_t i = 0; i < levelCount; ++i)
    {
        const uint32_t level = levelCount - 1 - i;
        cursor = AlignUp(cursor, alignment);
        offsets[level] = cursor;
        sizes[level] = level < levels.size() ? levels[level].size() : 0;
        cursor += sizes[level];
    }

    // Header.
    Append(out, kIdentifier, sizeof(kIdentifier));
    AppendU32(out, info.vkFormat);
    AppendU32(out, 1); // typeSize: 1 for block-compressed and 8-bit formats
    AppendU32(out, width);
    AppendU32(out, height);
    AppendU32(out, 0); // pixelDepth
    AppendU32(out, 0); // layerCount
    AppendU32(out, 1); // faceCount
    AppendU32(out, levelCount);
    AppendU32(out, 0); // supercompressionScheme
    AppendU32(out, static_cast<uint32_t>(dfdOffset));
    AppendU32(out, static_cast<uint32_t>(dfd.size()));
    AppendU32(out, static_cast<uint32_t>(kvdOffset));
    AppendU32(out, static_cast<uint32_t>(kvd.size()));
    AppendU64(out, 0); // sgdByteOffset
    AppendU64(out, 0); // sgdByteLength

    // Level index: base level first, whatever the data order.
    for (uint32_t level = 0; level < levelCount; ++level)
    {
        AppendU64(out, offsets[level]);
        AppendU64(out, sizes[level]);
        AppendU64(out, sizes[level]); // uncompressedByteLength
    }

    Append(out, dfd.data(), dfd.size());
    Append(out, kvd.data(), kvd.size());

    // Level data, smallest first.
    for (uint32_t i = 0; i < levelCount; ++i)
    {
        const uint32_t level = levelCount - 1 - i;
        PadTo(out, alignment);
        if (level < levels.size() && !levels[level].empty())
        {
            Append(out, levels[level].data(), levels[level].size());
        }
    }
}

} // namespace Geni::cook
