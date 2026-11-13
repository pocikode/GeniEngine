#include "world/Foliage.h"

#include <cstring>

namespace Geni
{

void WriteFoliage(const std::vector<FoliageInstance> &instances, std::vector<uint8_t> &out)
{
    out.clear();
    out.insert(out.end(), {'G', 'F', 'L', '1'});
    const uint32_t count = static_cast<uint32_t>(instances.size());
    const auto *countBytes = reinterpret_cast<const uint8_t *>(&count);
    out.insert(out.end(), countBytes, countBytes + 4);
    for (const FoliageInstance &instance : instances)
    {
        const float values[5] = {
            instance.position.x, instance.position.y, instance.position.z, instance.yaw, instance.scale
        };
        const auto *bytes = reinterpret_cast<const uint8_t *>(values);
        out.insert(out.end(), bytes, bytes + sizeof(values));
    }
}

bool ReadFoliage(const std::vector<uint8_t> &bytes, std::vector<FoliageInstance> &out, std::string *error)
{
    if (bytes.size() < 8 || std::memcmp(bytes.data(), "GFL1", 4) != 0)
    {
        if (error)
        {
            *error = "not a foliage file";
        }
        return false;
    }
    uint32_t count = 0;
    std::memcpy(&count, bytes.data() + 4, 4);
    if (bytes.size() != 8 + static_cast<size_t>(count) * 5 * sizeof(float))
    {
        if (error)
        {
            *error = "foliage size does not match its count";
        }
        return false;
    }
    out.resize(count);
    for (uint32_t i = 0; i < count; ++i)
    {
        float values[5];
        std::memcpy(values, bytes.data() + 8 + static_cast<size_t>(i) * sizeof(values), sizeof(values));
        out[i].position = glm::vec3(values[0], values[1], values[2]);
        out[i].yaw = values[3];
        out[i].scale = values[4];
    }
    return true;
}

} // namespace Geni
