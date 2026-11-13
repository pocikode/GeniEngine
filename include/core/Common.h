#pragma once

#include "glm/ext/matrix_float4x4.hpp"
#include "glm/ext/vector_float3.hpp"

namespace Geni
{

enum class LightType : uint32_t
{
    Directional = 0,
    Point = 1,
    Spot = 2,
};

struct CameraData
{
    glm::mat4 viewMatrix{1.0f};
    glm::mat4 projectionMatrix{1.0f};
    glm::vec3 position{0.0f};
    // Used by the cascade fitting and the cluster depth slices. Filled from the camera
    // component; the defaults keep a scene without a camera working.
    float nearPlane = 0.1f;
    float farPlane = 1000.0f;
};

struct LightData
{
    LightType type = LightType::Point;
    // Colour times intensity. A directional light's intensity is in the same units.
    glm::vec3 color{1.0f};
    // Point and spot lights: world position. Ignored by directional lights.
    glm::vec3 position{0.0f};
    // Directional and spot lights: the direction the light points, normalised.
    glm::vec3 direction{0.0f, -1.0f, 0.0f};
    // Point and spot lights: distance at which the light fades out. Zero or negative picks
    // the distance where the light's brightest channel falls below 1% (see
    // EffectiveLightRange in render/ClusterGrid.h); clustered lighting needs a finite range.
    float range = 10.0f;
    // Spot cone angles in radians (outer is the half-angle).
    float innerConeAngle = 0.0f;
    float outerConeAngle = 0.785398f;
    // Scaled by the frame's darkness (Atmosphere::darkness, docs/design/0018): off by day.
    bool nightOnly = false;
};

} // namespace Geni
