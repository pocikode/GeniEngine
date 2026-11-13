#pragma once

// Placed plants (docs/design/0023): positions, yaw and scale of each instance of one
// prefab, written by the terrain cooker and drawn by FoliageComponent. Depends only on glm,
// so the cooker compiles it too.

#include <glm/vec3.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace Geni
{

struct FoliageInstance
{
    // In the owner's space.
    glm::vec3 position{0.0f};
    // Radians about +Y.
    float yaw = 0.0f;
    float scale = 1.0f;
};

// "GFL1", count, then position (3 floats), yaw, scale per instance.
void WriteFoliage(const std::vector<FoliageInstance> &instances, std::vector<uint8_t> &out);
bool ReadFoliage(const std::vector<uint8_t> &bytes, std::vector<FoliageInstance> &out, std::string *error = nullptr);

} // namespace Geni
