#pragma once

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

namespace Geni
{

// Six planes extracted from a view-projection matrix (Gribb-Hartmann). The RHI's clip
// space has depth 0..1 and Y up, which is what FromViewProjection expects.
class Frustum
{
  public:
    // `zeroToOne` selects the clip-space depth range: true for the RHI/WebGPU convention
    // (0..1), false for the OpenGL-style projections used by CameraComponent and the
    // shadow cascades.
    static Frustum FromViewProjection(const glm::mat4 &viewProjection, bool zeroToOne = true);

    bool IntersectsSphere(const glm::vec3 &center, float radius) const;
    bool IntersectsAABB(const glm::vec3 &min, const glm::vec3 &max) const;

    const glm::vec4 &GetPlane(int index) const
    {
        return m_planes[index];
    }

  private:
    // ax + by + cz + d, normalised so a sphere test is a plain distance.
    glm::vec4 m_planes[6];
};

} // namespace Geni