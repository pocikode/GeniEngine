#pragma once

// A terrain's heights (docs/design/0022): a square grid the cooker writes and
// TerrainComponent reads for collision and height queries. Depends only on glm, so the
// cooker compiles it too.

#include <glm/vec3.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace Geni
{

struct HeightfieldData
{
    // Samples per side; sample (x, z) is at (x * cellSize, heights[z * samples + x], z * cellSize)
    // in the terrain's space.
    uint32_t samples = 0;
    float cellSize = 1.0f;
    std::vector<float> heights;

    float At(uint32_t x, uint32_t z) const
    {
        return heights[static_cast<size_t>(z) * samples + x];
    }
    // Bilinear height at (x, z) in the terrain's space, clamped to the grid.
    float HeightAt(float x, float z) const;
    // Unit normal from the neighbouring samples (central differences).
    glm::vec3 NormalAt(float x, float z) const;
    float Extent() const
    {
        return samples > 1 ? static_cast<float>(samples - 1) * cellSize : 0.0f;
    }
};

// "GHF1", samples, cell size, then the heights as little-endian floats.
void WriteHeightfield(const HeightfieldData &data, std::vector<uint8_t> &out);
bool ReadHeightfield(const std::vector<uint8_t> &bytes, HeightfieldData &out, std::string *error = nullptr);

} // namespace Geni
