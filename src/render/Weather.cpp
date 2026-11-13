#include "render/Weather.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>

#include <algorithm>

namespace Geni
{

namespace
{

float Smoothstep(float edge0, float edge1, float x)
{
    const float t = glm::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// The colour's grey at its own luminance (Rec. 709 weights, linear).
glm::vec3 Grey(const glm::vec3 &color)
{
    return glm::vec3(glm::dot(color, glm::vec3(0.2126f, 0.7152f, 0.0722f)));
}

} // namespace

float ApplyWeather(const WeatherState &weather, Atmosphere &atmosphere)
{
    const float clouds = glm::clamp(weather.cloudCover, 0.0f, 1.0f);
    const float fog = glm::clamp(weather.fog, 0.0f, 1.0f);

    // Overcast: the sky turns grey and a little darker; light comes from the whole sky.
    atmosphere.skyZenith = glm::mix(atmosphere.skyZenith, Grey(atmosphere.skyHorizon) * 0.85f, clouds * 0.9f);
    atmosphere.skyHorizon = glm::mix(atmosphere.skyHorizon, Grey(atmosphere.skyHorizon) * 0.8f, clouds * 0.85f);
    atmosphere.fogColor = glm::mix(atmosphere.fogColor, Grey(atmosphere.fogColor) * 0.85f, clouds * 0.8f);
    atmosphere.ambientIntensity *= 1.0f + 0.4f * clouds;
    // A heavy storm brings the street lights half on, even by day.
    atmosphere.darkness = std::max(atmosphere.darkness, 0.5f * Smoothstep(0.7f, 1.0f, clouds));

    // Fog: up to 12 times as dense, the colour of the horizon.
    atmosphere.fogDensity *= 1.0f + 11.0f * fog * fog;
    atmosphere.fogColor = glm::mix(atmosphere.fogColor, atmosphere.skyHorizon, fog * 0.7f);
    atmosphere.skyHorizon = glm::mix(atmosphere.skyHorizon, atmosphere.fogColor, fog * 0.5f);

    return 1.0f - 0.8f * clouds;
}

} // namespace Geni
