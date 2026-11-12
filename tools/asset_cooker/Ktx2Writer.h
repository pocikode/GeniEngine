#pragma once

#include <cstdint>
#include <vector>

namespace Geni::cook
{

// Formats the cooker writes. `Srgb` variants are for colour textures (base colour,
// emissive): the GPU decodes them to linear before filtering.
enum class Ktx2Format
{
    RGBA8,
    RGBA8Srgb,
    BC5, // two channels (normal maps: X and Y)
    BC7,
    BC7Srgb,
    ASTC4x4,
    ASTC4x4Srgb
};

// Writes a KTX2 container (no supercompression) that follows the KTX 2.0 specification:
// a Data Format Descriptor that matches the format, a KTXwriter key, and mip levels
// stored smallest first with the required alignment. `levels` is base level first and
// tightly packed. `ktx validate` accepts the output (see tests/test_cooker.cpp).
void WriteKtx2(
    Ktx2Format format, uint32_t width, uint32_t height, const std::vector<std::vector<uint8_t>> &levels,
    std::vector<uint8_t> &out
);

} // namespace Geni::cook
