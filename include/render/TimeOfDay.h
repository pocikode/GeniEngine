#pragma once

#include "glm/ext/vector_float3.hpp"

namespace Geni
{

// The sky, fog and ambient values the renderer puts in the frame uniforms (linear colours).
// RendererConfig holds the defaults; Renderer::SetAtmosphereOverride replaces them for one
// frame.
struct Atmosphere
{
    glm::vec3 skyZenith{0.05f, 0.13f, 0.32f};
    glm::vec3 skyHorizon{0.38f, 0.50f, 0.68f};
    glm::vec3 fogColor{0.44f, 0.55f, 0.70f};
    float fogDensity = 0.0018f;
    float ambientIntensity = 0.15f;
};

// How the clock maps to the sky (docs/design/0018). Sunrise is at 06:00 due +X, sunset at
// 18:00 due -X; at noon the sun is maxElevation degrees above the horizon towards -Z.
// pathYaw turns the whole path about +Y.
struct TimeOfDaySettings
{
    float maxElevation = 60.0f;
    float pathYaw = 0.0f;
    float sunIntensity = 2.2f;
    float moonIntensity = 0.12f;
    float fogDensity = 0.0018f;
};

struct TimeOfDayState
{
    // The direction light travels (from the sun or moon towards the ground), unit length.
    glm::vec3 lightDirection{0.0f, -1.0f, 0.0f};
    // Linear colour; the light's radiance is colour * intensity.
    glm::vec3 lightColor{1.0f};
    float lightIntensity = 0.0f;
    // True when the light is the moon (the sun is more than 2 degrees below the horizon).
    bool moon = false;
    // Degrees above the horizon; negative at night.
    float sunElevation = 0.0f;
    Atmosphere atmosphere;
};

// Wraps any number of hours into [0, 24).
float WrapHours(float hours);

// The lighting at `hours` (wrapped into 0..24).
TimeOfDayState EvaluateTimeOfDay(float hours, const TimeOfDaySettings &settings = {});

} // namespace Geni
