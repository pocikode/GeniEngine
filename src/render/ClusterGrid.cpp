#include "render/ClusterGrid.h"

#include <algorithm>
#include <cmath>
#include <glm/geometric.hpp>

namespace Geni
{

namespace
{

// View-space point on a camera ray at `depth` metres in front of the camera. Points along
// a ray are linear in view space, so interpolating between the near and far plane points
// is exact.
glm::vec3 ViewPointOnRay(const glm::vec3 &nearPoint, const glm::vec3 &farPoint, float depth)
{
    const float length = farPoint.z - nearPoint.z;
    const float t = std::abs(length) > 1e-6f ? (-depth - nearPoint.z) / length : 0.0f;
    return nearPoint + (farPoint - nearPoint) * std::clamp(t, 0.0f, 1.0f);
}

struct ViewLight
{
    glm::vec3 position;
    float radius;
};

} // namespace

bool IsClusteredLight(LightType type)
{
    return type == LightType::Point || type == LightType::Spot;
}

float EffectiveLightRange(const LightData &light)
{
    if (light.range > 0.0f)
    {
        return std::max(light.range, 0.01f);
    }
    // color / d^2 = 0.01  =>  d = sqrt(100 * color).
    const float brightest = std::max({light.color.r, light.color.g, light.color.b, 0.0f});
    return std::max(std::sqrt(100.0f * brightest), 0.01f);
}

ClusterAssignment AssignLightsToClusters(
    const glm::mat4 &view, const glm::mat4 &projection, const ClusterSettings &settings,
    const std::vector<LightData> &lights
)
{
    ClusterAssignment result;
    const uint32_t countX = std::max(1u, settings.countX);
    const uint32_t countY = std::max(1u, settings.countY);
    const uint32_t countZ = std::max(1u, settings.countZ);
    const uint32_t clusterCount = countX * countY * countZ;
    result.ranges.assign(clusterCount, ShaderInterface::ClusterRange{0, 0});

    // Only point/spot lights go into clusters. A spot light uses the sphere around its
    // position: conservative, and cheap enough at the scene's light count.
    std::vector<ViewLight> viewLights;
    viewLights.reserve(lights.size());
    for (const auto &light : lights)
    {
        if (!IsClusteredLight(light.type))
        {
            continue;
        }
        viewLights.push_back({glm::vec3(view * glm::vec4(light.position, 1.0f)), EffectiveLightRange(light)});
    }
    if (viewLights.empty())
    {
        return result;
    }

    const float near = std::max(0.001f, settings.nearPlane);
    const float far = std::max(near * 1.01f, settings.farPlane);
    const float logRatio = std::log(far / near);
    const glm::mat4 inverseProjection = glm::inverse(projection);

    // Rays through the four corners of every tile, in view space.
    const size_t cornerCount = size_t(countX) * countY * 4;
    std::vector<glm::vec3> nearCorners(cornerCount);
    std::vector<glm::vec3> farCorners(cornerCount);
    auto cornerIndex = [countX](uint32_t x, uint32_t y, uint32_t corner) {
        return (size_t(y) * countX + x) * 4 + corner;
    };
    for (uint32_t y = 0; y < countY; ++y)
    {
        // gl_FragCoord has its origin at the top-left, so tile row 0 is NDC y = +1.
        const float y0 = 1.0f - 2.0f * static_cast<float>(y) / static_cast<float>(countY);
        const float y1 = 1.0f - 2.0f * static_cast<float>(y + 1) / static_cast<float>(countY);
        for (uint32_t x = 0; x < countX; ++x)
        {
            const float x0 = 2.0f * static_cast<float>(x) / static_cast<float>(countX) - 1.0f;
            const float x1 = 2.0f * static_cast<float>(x + 1) / static_cast<float>(countX) - 1.0f;
            const float xs[4] = {x0, x1, x0, x1};
            const float ys[4] = {y0, y0, y1, y1};
            for (uint32_t corner = 0; corner < 4; ++corner)
            {
                const glm::vec4 nearHom = inverseProjection * glm::vec4(xs[corner], ys[corner], -1.0f, 1.0f);
                const glm::vec4 farHom = inverseProjection * glm::vec4(xs[corner], ys[corner], 1.0f, 1.0f);
                nearCorners[cornerIndex(x, y, corner)] = glm::vec3(nearHom) / nearHom.w;
                farCorners[cornerIndex(x, y, corner)] = glm::vec3(farHom) / farHom.w;
            }
        }
    }

    auto clusterAABB = [&](uint32_t cluster, glm::vec3 &min, glm::vec3 &max) {
        const uint32_t z = cluster / (countX * countY);
        const uint32_t y = (cluster / countX) % countY;
        const uint32_t x = cluster % countX;
        const float sliceNear = near * std::exp(logRatio * static_cast<float>(z) / static_cast<float>(countZ));
        const float sliceFar = near * std::exp(logRatio * static_cast<float>(z + 1) / static_cast<float>(countZ));
        min = glm::vec3(1e30f);
        max = glm::vec3(-1e30f);
        for (uint32_t corner = 0; corner < 4; ++corner)
        {
            const size_t index = cornerIndex(x, y, corner);
            for (float depth : {sliceNear, sliceFar})
            {
                const glm::vec3 point = ViewPointOnRay(nearCorners[index], farCorners[index], depth);
                min = glm::min(min, point);
                max = glm::max(max, point);
            }
        }
    };

    // Pass 1: how many lights each cluster needs.
    std::vector<uint32_t> counts(clusterCount, 0);
    for (uint32_t cluster = 0; cluster < clusterCount; ++cluster)
    {
        glm::vec3 min;
        glm::vec3 max;
        clusterAABB(cluster, min, max);
        for (const ViewLight &light : viewLights)
        {
            const glm::vec3 closest = glm::clamp(light.position, min, max);
            const glm::vec3 delta = closest - light.position;
            if (glm::dot(delta, delta) > light.radius * light.radius)
            {
                continue;
            }
            if (counts[cluster] >= settings.maxLightsPerCluster)
            {
                ++result.overflowLights;
                continue;
            }
            ++counts[cluster];
        }
    }

    uint32_t offset = 0;
    for (uint32_t cluster = 0; cluster < clusterCount; ++cluster)
    {
        result.ranges[cluster].offset = offset;
        result.ranges[cluster].count = counts[cluster];
        offset += counts[cluster];
    }
    result.lightIndices.resize(offset);

    // Pass 2: write the indices at each cluster's offset.
    std::vector<uint32_t> cursor(clusterCount, 0);
    for (uint32_t cluster = 0; cluster < clusterCount; ++cluster)
    {
        if (counts[cluster] == 0)
        {
            continue;
        }
        glm::vec3 min;
        glm::vec3 max;
        clusterAABB(cluster, min, max);
        uint32_t write = result.ranges[cluster].offset;
        for (uint32_t lightIndex = 0; lightIndex < viewLights.size() && cursor[cluster] < counts[cluster]; ++lightIndex)
        {
            const ViewLight &light = viewLights[lightIndex];
            const glm::vec3 closest = glm::clamp(light.position, min, max);
            const glm::vec3 delta = closest - light.position;
            if (glm::dot(delta, delta) > light.radius * light.radius)
            {
                continue;
            }
            result.lightIndices[write++] = lightIndex;
            ++cursor[cluster];
        }
    }

    return result;
}

} // namespace Geni