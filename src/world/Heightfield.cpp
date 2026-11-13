#include "world/Heightfield.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Geni
{

float HeightfieldData::HeightAt(float x, float z) const
{
    if (samples < 2 || heights.size() < static_cast<size_t>(samples) * samples)
    {
        return 0.0f;
    }
    const float last = static_cast<float>(samples - 1);
    const float gx = glm::clamp(x / cellSize, 0.0f, last);
    const float gz = glm::clamp(z / cellSize, 0.0f, last);
    const uint32_t x0 = std::min(static_cast<uint32_t>(gx), samples - 2);
    const uint32_t z0 = std::min(static_cast<uint32_t>(gz), samples - 2);
    const float fx = gx - static_cast<float>(x0);
    const float fz = gz - static_cast<float>(z0);
    const float top = glm::mix(At(x0, z0), At(x0 + 1, z0), fx);
    const float bottom = glm::mix(At(x0, z0 + 1), At(x0 + 1, z0 + 1), fx);
    return glm::mix(top, bottom, fz);
}

glm::vec3 HeightfieldData::NormalAt(float x, float z) const
{
    const float d = cellSize;
    const float dx = HeightAt(x + d, z) - HeightAt(x - d, z);
    const float dz = HeightAt(x, z + d) - HeightAt(x, z - d);
    return glm::normalize(glm::vec3(-dx, 2.0f * d, -dz));
}

void WriteHeightfield(const HeightfieldData &data, std::vector<uint8_t> &out)
{
    out.clear();
    out.insert(out.end(), {'G', 'H', 'F', '1'});
    auto put = [&out](const void *value, size_t size) {
        const auto *bytes = static_cast<const uint8_t *>(value);
        out.insert(out.end(), bytes, bytes + size);
    };
    put(&data.samples, 4);
    put(&data.cellSize, 4);
    put(data.heights.data(), data.heights.size() * sizeof(float));
}

bool ReadHeightfield(const std::vector<uint8_t> &bytes, HeightfieldData &out, std::string *error)
{
    auto fail = [error](const char *message) {
        if (error)
        {
            *error = message;
        }
        return false;
    };
    if (bytes.size() < 12 || std::memcmp(bytes.data(), "GHF1", 4) != 0)
    {
        return fail("not a height field");
    }
    std::memcpy(&out.samples, bytes.data() + 4, 4);
    std::memcpy(&out.cellSize, bytes.data() + 8, 4);
    const size_t count = static_cast<size_t>(out.samples) * out.samples;
    if (out.samples < 2 || out.samples > 16385 || !(out.cellSize > 0.0f) || bytes.size() != 12 + count * sizeof(float))
    {
        return fail("height field size does not match its header");
    }
    out.heights.resize(count);
    std::memcpy(out.heights.data(), bytes.data() + 12, count * sizeof(float));
    return true;
}

} // namespace Geni
