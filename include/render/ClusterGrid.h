#pragma once

#include "core/Common.h"
#include "render/ShaderInterface.h"
#include <glm/mat4x4.hpp>
#include <vector>

namespace Geni
{

// View-space cluster grid for forward lighting. The CPU assigns point and spot lights to
// the clusters they can affect; the fragment shader looks up the same grid.
struct ClusterSettings
{
    uint32_t countX = 16;
    uint32_t countY = 9;
    uint32_t countZ = 24;
    float nearPlane = 0.1f;
    float farPlane = 100.0f;
    // Existing lights are dropped from a cluster beyond this; 256 leaves room for a very
    // bright alley without paying for it everywhere.
    uint32_t maxLightsPerCluster = ShaderInterface::MaxLightsPerCluster;
};

struct ClusterAssignment
{
    // One entry per cluster, in x-major, then y, then z order (the shader's indexing).
    std::vector<ShaderInterface::ClusterRange> ranges;
    std::vector<uint32_t> lightIndices;
    // Lights dropped because a cluster was full. Zero in normal scenes.
    uint32_t overflowLights = 0;
};

// `lights` may contain directional lights; they are ignored here (the sun is a separate
// frame uniform, not a cluster light). `view` and `projection` are the camera's, with an
// OpenGL-style projection (depth -1..1), as CameraComponent produces.
ClusterAssignment AssignLightsToClusters(
    const glm::mat4 &view, const glm::mat4 &projection, const ClusterSettings &settings,
    const std::vector<LightData> &lights
);

// True for the light types that go into clusters.
bool IsClusteredLight(LightType type);

// The range the renderer uses for a point or spot light: `light.range` when positive,
// otherwise the distance at which the inverse-square falloff of the brightest colour
// channel drops below 0.01.
float EffectiveLightRange(const LightData &light);

} // namespace Geni