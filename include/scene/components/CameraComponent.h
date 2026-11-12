#pragma once

#include "scene/Component.h"
#include <glm/mat4x4.hpp>

namespace Geni
{

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
    void SetNearPlane(float nearPlane)
    {
        m_nearPlane = nearPlane;
    }
    void SetFarPlane(float farPlane)
    {
        m_farPlane = farPlane;
    }

  private:
    float m_fov = 60.0f;
    float m_nearPlane = 0.1f;
    float m_farPlane = 1000.0f;
};

} // namespace Geni