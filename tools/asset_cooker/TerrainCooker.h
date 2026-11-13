#pragma once

// Terrain from a heightmap (docs/design/0022): the grid, chunk meshes and parameters.

#include "assets/CookedFormats.h"
#include "world/Heightfield.h"

#include <glm/vec3.hpp>
#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace Geni::cook
{

struct TerrainSource
{
    std::string heightmap; // relative to the .gterrain
    float size = 256.0f;   // metres per side
    float height = 32.0f;  // metres for the heightmap's full range
    int chunks = 8;        // per side
    int chunkQuads = 32;   // per chunk side
    // sRGB colours.
    glm::vec3 grass{0.30f, 0.45f, 0.20f};
    glm::vec3 rock{0.42f, 0.40f, 0.38f};
    glm::vec3 snow{0.92f, 0.94f, 0.97f};
    float rockSlope = 35.0f;  // degrees
    float snowHeight = 0.75f; // fraction of `height`
    float uvScale = 0.25f;
};

bool ParseTerrainSource(const nlohmann::json &document, TerrainSource &out, std::string *error = nullptr);

// Decodes an 8- or 16-bit grey (or colour: its red channel) image and resamples it to the
// terrain grid in metres.
bool BuildTerrainGrid(
    const std::vector<uint8_t> &image, const TerrainSource &source, HeightfieldData &out, std::string *error = nullptr
);

// One chunk's mesh, in the chunk's space (its corner at the origin): position, grass/rock/
// snow weights in the colour attribute, world-scaled UVs and normals, with LODs whose
// borders are locked so chunks always meet.
cooked::CookedMeshData BuildTerrainChunk(
    const HeightfieldData &grid, const TerrainSource &source, int chunkX, int chunkZ
);

} // namespace Geni::cook
