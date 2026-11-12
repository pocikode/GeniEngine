#pragma once

#include "nav/NavigationSystem.h"
#include "scene/Component.h"
#include <glm/vec3.hpp>
#include <string>

namespace Geni
{

// Walks its object over the navigation mesh in the engine's crowd (docs/design/0014).
// Joins the crowd once the mesh exists under it; MoveTo sends it somewhere; with a
// wander radius it keeps choosing reachable points near where it is (pedestrians).
class NavAgentComponent : public Component
{
    COMPONENT(NavAgentComponent)

  public:
    void Update(float deltaTime) override;
    void OnDestroy() override;

    // Scene-space destination. False when nothing on the mesh is near it.
    bool MoveTo(const glm::vec3 &scenePosition);
    void Stop();
    bool IsOnMesh() const
    {
        return m_agent >= 0;
    }
    bool HasArrived() const
    {
        return m_arrived;
    }
    float GetSpeed() const
    {
        return m_speed;
    }

    float GetRadius() const
    {
        return m_params.radius;
    }
    void SetRadius(float value)
    {
        m_params.radius = value;
    }
    float GetHeight() const
    {
        return m_params.height;
    }
    void SetHeight(float value)
    {
        m_params.height = value;
    }
    float GetMaxSpeed() const
    {
        return m_params.maxSpeed;
    }
    void SetMaxSpeed(float value);
    float GetMaxAcceleration() const
    {
        return m_params.maxAcceleration;
    }
    void SetMaxAcceleration(float value)
    {
        m_params.maxAcceleration = value;
    }
    float GetWanderRadius() const
    {
        return m_wanderRadius;
    }
    void SetWanderRadius(float value)
    {
        m_wanderRadius = value;
    }
    std::string GetSpeedParameter() const
    {
        return m_speedParameter;
    }
    void SetSpeedParameter(const std::string &value)
    {
        m_speedParameter = value;
    }

  private:
    void Leave();

    NavAgentParams m_params;
    float m_wanderRadius = 0.0f;
    std::string m_speedParameter = "speed";
    int m_agent = -1;
    uint32_t m_generation = 0;
    bool m_arrived = true;
    bool m_hasPendingTarget = false;
    glm::vec3 m_pendingTarget{0.0f};
    float m_speed = 0.0f;
    float m_yaw = 0.0f;
    float m_wanderWait = 0.0f;
    float m_retry = 0.0f;
};

} // namespace Geni
