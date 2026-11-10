#pragma once

#include <array>
#include <cstdint>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

namespace Geni
{

// One shadow cascade: a light-space matrix (depth -1..1 like the camera projection) and
// the camera-space distance it covers.
struct ShadowCascade
{
    glm::mat4 viewProjection{1.0f};
    float splitNear = 0.0f;
    float splitFar = 0.0f;
};

struct CascadeSetup
{
    std::array<ShadowCascade, 4> cascades{};
    uint32_t count = 0;
    // Far split per cascade, in view-space depth, for the shader's cascade selection.
    glm::vec4 splits{0.0f};
};

// Fits `cascadeCount` (1-4) cascades to the part of the camera frustum within
// `maxDistance`. `cameraProjection` is the camera component's OpenGL-style projection;
// `lightDirection` points from the sun towards the scene. Cascades are snapped to shadow
// texel increments so they do not shimmer when the camera moves. Each cascade's depth
// range reaches `casterDistance` beyond the slice towards the sun, so casters outside the
// slice (a tall building over a narrow first cascade) still cast into it.
CascadeSetup ComputeCascades(
    const glm::mat4 &cameraView, const glm::mat4 &cameraProjection, float nearPlane, float farPlane,
    const glm::vec3 &lightDirection, float maxDistance, uint32_t cascadeCount, uint32_t shadowMapSize,
    float casterDistance = 0.0f
);

} // namespace Geni