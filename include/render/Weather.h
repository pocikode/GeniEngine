#pragma once

#include "render/TimeOfDay.h"

#include <glm/ext/vector_float3.hpp>

namespace Geni
{

// The weather at one moment (docs/design/0020). All amounts are 0..1.
struct WeatherState
{
    float cloudCover = 0.0f;
    float fog = 0.0f;
    float precipitation = 0.0f;
    // false: rain.
    bool snow = false;
    // Metres per second; carries rain and snow sideways.
    glm::vec3 wind{0.0f};
};

// Changes the frame's atmosphere for the weather and returns how much the sun is scaled
// by (1 when clear). Pure; Renderer::PrepareFrame calls it with the weather set for the
// frame.
float ApplyWeather(const WeatherState &weather, Atmosphere &atmosphere);

} // namespace Geni
