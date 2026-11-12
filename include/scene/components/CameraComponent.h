#pragma once

#include "scene/Component.h"
#include <glm/mat4x4.hpp>

namespace Geni
{

enum class CameraProjection
{
    Perspective,
    // Parallel projection for 2D (docs/design/0017): `orthoSize` world units fit the
    // view's height, whatever the distance.
    Orthographic
};

class CameraComponent : public Component
{
    COMPONENT(CameraComponent);

  public:
    void Update(float deltaTime) override;

    glm::mat4 GetViewMatrix() const;
    glm::mat4 GetProjectionMatrix(float aspect) const;

    float GetNearPlane() const
    {
        return m_nearPlane;
    }
    float GetFarPlane() const
    {
        return m_farPlane;
    }
    float GetFieldOfView() const
    {
        return m_fov;
    }

    void SetFieldOfView(float fov)
    {
        m_fov = fov;
    }
    CameraProjection GetProjection() const
    {
        return m_projection;
    }
    void SetProjection(CameraProjection projection)
    {
        m_projection = projection;
    }
    // Visible height in world units for an orthographic camera.
    float GetOrthoSize() const
    {
        return m_orthoSize;
    }
    void SetOrthoSize(float size)
    {
        m_orthoSize = size > 1e-4f ? size : 1e-4f;
    }
    void SetNearPlane(float nearPlane)
    {
        m_nearPlane = nearPlane;
    }
    void SetFarPlane(float farPlane)
    {
        m_farPlane = farPlane;
    }

  private:
    CameraProjection m_projection = CameraProjection::Perspective;
    float m_orthoSize = 10.0f;
    float m_fov = 60.0f;
    float m_nearPlane = 0.1f;
    float m_farPlane = 1000.0f;
};

} // namespace Geni