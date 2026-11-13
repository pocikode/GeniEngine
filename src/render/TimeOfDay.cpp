#include "render/TimeOfDay.h"

#include "glm/common.hpp"
#include "glm/exponential.hpp"
#include "glm/geometric.hpp"
#include "glm/trigonometric.hpp"

#include <cmath>
#include <iterator>

namespace Geni
{

namespace
{

constexpr float kPi = 3.14159265358979f;

// Sky colours by sun elevation (docs/design/0018). The last key is RendererConfig's
// defaults, so a scene at midday looks as it did without a clock.
struct SkyKey
{
    float elevation;
    glm::vec3 zenith;
    glm::vec3 horizon;
    glm::vec3 fog;
    float ambient;
};

const SkyKey kSkyKeys[] = {
    {-18.0f, {0.004f, 0.007f, 0.018f}, {0.012f, 0.018f, 0.035f}, {0.010f, 0.014f, 0.026f}, 0.45f},
    {-6.0f, {0.020f, 0.040f, 0.100f}, {0.090f, 0.085f, 0.140f}, {0.055f, 0.060f, 0.100f}, 0.28f},
    {0.0f, {0.040f, 0.090f, 0.240f}, {0.560f, 0.310f, 0.170f}, {0.400f, 0.300f, 0.240f}, 0.16f},
    {8.0f, {0.050f, 0.120f, 0.300f}, {0.500f, 0.460f, 0.430f}, {0.440f, 0.480f, 0.530f}, 0.15f},
    {25.0f, {0.050f, 0.130f, 0.320f}, {0.380f, 0.500f, 0.680f}, {0.440f, 0.550f, 0.700f}, 0.15f},
};

// The sun's colour by its elevation; the last key is Geni City's authored sun.
struct SunKey
{
    float elevation;
    glm::vec3 color;
};

const SunKey kSunKeys[] = {
    {0.0f, {1.0f, 0.45f, 0.18f}},
    {8.0f, {1.0f, 0.74f, 0.50f}},
    {25.0f, {1.0f, 0.95f, 0.85f}},
};

const glm::vec3 kMoonColor{0.55f, 0.65f, 1.0f};

float Smoothstep(float edge0, float edge1, float x)
{
    const float t = glm::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// Index of the key below `elevation` and the blend towards the next one.
template <typename Key, size_t N> void FindKeys(const Key (&keys)[N], float elevation, size_t &index, float &t)
{
    if (elevation <= keys[0].elevation)
    {
        index = 0;
        t = 0.0f;
        return;
    }
    for (size_t i = 0; i + 1 < N; ++i)
    {
        if (elevation < keys[i + 1].elevation)
        {
            index = i;
            t = (elevation - keys[i].elevation) / (keys[i + 1].elevation - keys[i].elevation);
            return;
        }
    }
    index = N - 1;
    t = 0.0f;
}

template <typename Key, size_t N> const Key &Next(const Key (&keys)[N], size_t index)
{
    return keys[index + 1 < N ? index + 1 : index];
}

} // namespace

float WrapHours(float hours)
{
    if (!std::isfinite(hours))
    {
        return 0.0f;
    }
    float wrapped = std::fmod(hours, 24.0f);
    if (wrapped < 0.0f)
    {
        wrapped += 24.0f;
    }
    // fmod of a value just below a multiple of 24 can round up to 24 itself.
    return wrapped >= 24.0f ? 0.0f : wrapped;
}

TimeOfDayState EvaluateTimeOfDay(float hours, const TimeOfDaySettings &settings)
{
    TimeOfDayState state;

    // Towards the sun: east (+X) at 06:00, up and towards -Z at noon, west at 18:00.
    const float angle = (WrapHours(hours) - 6.0f) / 24.0f * 2.0f * kPi;
    const float tilt = glm::radians(glm::clamp(settings.maxElevation, 1.0f, 90.0f));
    glm::vec3 toSun(std::cos(angle), std::sin(angle) * std::sin(tilt), -std::sin(angle) * std::cos(tilt));
    const float yaw = glm::radians(settings.pathYaw);
    const float c = std::cos(yaw);
    const float s = std::sin(yaw);
    toSun = glm::normalize(glm::vec3(c * toSun.x + s * toSun.z, toSun.y, -s * toSun.x + c * toSun.z));

    const float elevation = glm::degrees(std::asin(glm::clamp(toSun.y, -1.0f, 1.0f)));
    state.sunElevation = elevation;

    // One light: the sun until it is 2 degrees down, then the moon opposite it. Both are
    // dark around the switch, so shadows do not jump.
    state.moon = elevation < -2.0f;
    if (state.moon)
    {
        state.lightDirection = toSun; // from the moon, which is at -toSun, towards the ground
        state.lightColor = kMoonColor;
        state.lightIntensity = settings.moonIntensity * Smoothstep(-2.0f, -10.0f, elevation);
    }
    else
    {
        state.lightDirection = -toSun;
        size_t index = 0;
        float t = 0.0f;
        FindKeys(kSunKeys, elevation, index, t);
        state.lightColor = glm::mix(kSunKeys[index].color, Next(kSunKeys, index).color, t);
        state.lightIntensity = settings.sunIntensity * Smoothstep(-1.0f, 12.0f, elevation);
    }

    size_t index = 0;
    float t = 0.0f;
    FindKeys(kSkyKeys, elevation, index, t);
    const SkyKey &a = kSkyKeys[index];
    const SkyKey &b = Next(kSkyKeys, index);
    state.atmosphere.skyZenith = glm::mix(a.zenith, b.zenith, t);
    state.atmosphere.skyHorizon = glm::mix(a.horizon, b.horizon, t);
    state.atmosphere.fogColor = glm::mix(a.fog, b.fog, t);
    state.atmosphere.ambientIntensity = glm::mix(a.ambient, b.ambient, t);
    state.atmosphere.fogDensity = settings.fogDensity;
    // Lamps come on while the sun sets and are fully on in the blue hour.
    state.atmosphere.darkness = Smoothstep(6.0f, -4.0f, elevation);
    return state;
}

} // namespace Geni
