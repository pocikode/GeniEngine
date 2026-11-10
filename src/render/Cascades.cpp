#include "render/Cascades.h"

#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

namespace Geni
{

namespace
{
constexpr float kSplitLambda = 0.9f; // practical split: mostly logarithmic
}

CascadeSetup ComputeCascades(
    const glm::mat4 &cameraView, const glm::mat4 &cameraProjection, float nearPlane, float farPlane,
    const glm::vec3 &lightDirection, float maxDistance, uint32_t cascadeCount, uint32_t shadowMapSize,
    float casterDistance
)
{
    CascadeSetup setup;
    setup.count = std::clamp<uint32_t>(cascadeCount, 1, static_cast<uint32_t>(setup.cascades.size()));
    if (maxDistance <= nearPlane)
    {
        return setup;
    }

    const float shadowFar = std::min(farPlane, maxDistance);
    const float directionLength = glm::length(lightDirection);
    const glm::vec3 toLight = directionLength > 1e-6f ? -lightDirection / directionLength : glm::vec3(0.0f, 1.0f, 0.0f);
    const glm::vec3 up = std::abs(toLight.y) > 0.99f ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);

    // Camera projection is OpenGL-style (depth -1..1), so the unprojected corners are too.
    const glm::mat4 inverseViewProjection = glm::inverse(cameraView) * glm::inverse(cameraProjection);
    const float texelScale = shadowMapSize > 0 ? 1.0f / static_cast<float>(shadowMapSize) : 0.0f;

    float splitNear = nearPlane;
    float splits[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    for (uint32_t i = 0; i < setup.count; ++i)
    {
        const float p = static_cast<float>(i + 1) / static_cast<float>(setup.count);
        const float logarithmic = nearPlane * std::pow(shadowFar / nearPlane, p);
        const float linear = nearPlane + (shadowFar - nearPlane) * p;
        const float splitFar = std::min(shadowFar, kSplitLambda * logarithmic + (1.0f - kSplitLambda) * linear);

        // The slice's four camera rays, intersected with the split planes in view space.
        glm::vec3 sliceCorners[8];
        int cornerIndex = 0;
        for (float x : {-1.0f, 1.0f})
        {
            for (float y : {-1.0f, 1.0f})
            {
                glm::vec4 nearHom = inverseViewProjection * glm::vec4(x, y, -1.0f, 1.0f);
                glm::vec4 farHom = inverseViewProjection * glm::vec4(x, y, 1.0f, 1.0f);
                const glm::vec3 nearWorld = glm::vec3(nearHom) / nearHom.w;
                const glm::vec3 farWorld = glm::vec3(farHom) / farHom.w;
                const glm::vec3 nearView = glm::vec3(cameraView * glm::vec4(nearWorld, 1.0f));
                const glm::vec3 farView = glm::vec3(cameraView * glm::vec4(farWorld, 1.0f));
                const float rayLength = farView.z - nearView.z;
                for (float depth : {splitNear, splitFar})
                {
                    const float t = std::abs(rayLength) > 1e-6f ? (-depth - nearView.z) / rayLength : 0.0f;
                    sliceCorners[cornerIndex++] = nearWorld + (farWorld - nearWorld) * std::clamp(t, 0.0f, 1.0f);
                }
            }
        }

        glm::vec3 center(0.0f);
        for (const auto &corner : sliceCorners)
        {
            center += corner;
        }
        center /= 8.0f;
        float radius = 0.0f;
        for (const auto &corner : sliceCorners)
        {
            radius = std::max(radius, glm::length(corner - center));
        }
        // The radius is the same for every camera orientation in exact arithmetic; rounding
        // it up removes float noise so the texel size stays constant frame to frame.
        radius = std::ceil(std::max(radius, 0.5f) * 16.0f) / 16.0f;

        // The light view is anchored at the world origin, not at the slice, so moving the
        // camera moves the cascade's centre in light space. Snapping that centre to whole
        // texels keeps the shadow map from swimming.
        const glm::mat4 lightView = glm::lookAt(glm::vec3(0.0f), -toLight, up);
        const glm::vec3 lightCenter = glm::vec3(lightView * glm::vec4(center, 1.0f));
        const float texel = 2.0f * radius * texelScale;
        const float snappedX = texel > 0.0f ? std::round(lightCenter.x / texel) * texel : lightCenter.x;
        const float snappedY = texel > 0.0f ? std::round(lightCenter.y / texel) * texel : lightCenter.y;

        // Depth covers the slice's sphere plus `casterDistance` towards the sun, so casters
        // above the slice (roofs over a street in the first cascade) still write depth.
        const float centerDepth = -lightCenter.z;
        const glm::mat4 projection = glm::ortho(
            snappedX - radius, snappedX + radius, snappedY - radius, snappedY + radius,
            centerDepth - radius - std::max(0.0f, casterDistance), centerDepth + radius
        );
        setup.cascades[i].viewProjection = projection * lightView;
        setup.cascades[i].splitNear = splitNear;
        setup.cascades[i].splitFar = splitFar;
        splits[i] = splitFar;
        splitNear = splitFar;
    }

    setup.splits = glm::vec4(splits[0], splits[1], splits[2], splits[3]);
    return setup;
}

} // namespace Geni