#pragma once
#include "scene/Component.h"
#include <glm/vec3.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Geni
{

class OrbitCameraComponent : public Component
{
    COMPONENT(OrbitCameraComponent)

  public:
    void Update(float deltaTime) override;

    void SetTarget(const glm::vec3 &target);
    void SetDistance(float distance);
    void SetYaw(float yaw);
    void SetPitch(float pitch);
    void SetOrbitSensitivity(float s);
    void SetPanSensitivity(float s);
    void SetZoomSensitivity(float s);

    glm::vec3 GetTarget() const;
    float GetDistance() const;
    float GetYaw() const;
    float GetPitch() const;

  private:
    glm::vec3 m_target = glm::vec3(0.0f);
    float m_yaw = 45.0f;
    float m_pitch = 30.0f;
    float m_distance = 5.0f;
    float m_minDistance = 0.5f;
    float m_maxDistance = 100.0f;
    float m_orbitSensitivity = 0.3f;
    float m_panSensitivity = 0.005f;
    float m_zoomSensitivity = 0.5f;
};

} // namespace Geni
