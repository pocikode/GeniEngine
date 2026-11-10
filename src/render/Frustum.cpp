#include "render/Frustum.h"

#include <glm/geometric.hpp>

namespace Geni
{

namespace
{
glm::vec4 Row(const glm::mat4 &m, int row)
{
    return glm::vec4(m[0][row], m[1][row], m[2][row], m[3][row]);
}
} // namespace

Frustum Frustum::FromViewProjection(const glm::mat4 &viewProjection, bool zeroToOne)
{
    const glm::vec4 r0 = Row(viewProjection, 0);
    const glm::vec4 r1 = Row(viewProjection, 1);
    const glm::vec4 r2 = Row(viewProjection, 2);
    const glm::vec4 r3 = Row(viewProjection, 3);

    Frustum frustum;
    // x >= -w, x <= w, y >= -w, y <= w, near, far.
    frustum.m_planes[0] = r3 + r0;
    frustum.m_planes[1] = r3 - r0;
    frustum.m_planes[2] = r3 + r1;
    frustum.m_planes[3] = r3 - r1;
    // Depth 0..1: z >= 0, z <= w. OpenGL depth -1..1: z >= -w, z <= w.
    frustum.m_planes[4] = zeroToOne ? r2 : r3 + r2;
    frustum.m_planes[5] = r3 - r2;
    for (auto &plane : frustum.m_planes)
    {
        const float length = glm::length(glm::vec3(plane));
        if (length > 1e-8f)
        {
            plane /= length;
        }
    }
    return frustum;
}

bool Frustum::IntersectsSphere(const glm::vec3 &center, float radius) const
{
    for (const auto &plane : m_planes)
    {
        if (glm::dot(glm::vec3(plane), center) + plane.w < -radius)
        {
            return false;
        }
    }
    return true;
}

bool Frustum::IntersectsAABB(const glm::vec3 &min, const glm::vec3 &max) const
{
    for (const auto &plane : m_planes)
    {
        // The point of the box furthest along the plane normal. If even that is behind
        // the plane, the box is outside.
        const glm::vec3 positive(
            plane.x >= 0.0f ? max.x : min.x, plane.y >= 0.0f ? max.y : min.y, plane.z >= 0.0f ? max.z : min.z
        );
        if (glm::dot(glm::vec3(plane), positive) + plane.w < 0.0f)
        {
            return false;
        }
    }
    return true;
}

} // namespace Geni