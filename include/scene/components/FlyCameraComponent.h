#pragma once

#include "scene/Component.h"

namespace Geni
{

// Free-flying camera for large worlds: W/A/S/D move, Q/E go down and up, Shift multiplies
// the speed by `boost`, holding the right mouse button looks around. Ignores input the
// debug overlay is using. Yaw and pitch are kept separately so the horizon stays level.
class FlyCameraComponent : public Component
{
    COMPONENT(FlyCameraComponent)

  public:
    void Init() override;
    void Update(float deltaTime) override;

    float GetSpeed() const
    {
        return m_speed;
    }
    void SetSpeed(float speed)
    {
        m_speed = speed;
    }
    float GetBoost() const
    {
        return m_boost;
    }
    void SetBoost(float boost)
    {
        m_boost = boost;
    }
    float GetSensitivity() const
    {
        return m_sensitivity;
    }
    void SetSensitivity(float sensitivity)
    {
        m_sensitivity = sensitivity;
    }

  private:
    float m_speed = 30.0f;
    float m_boost = 4.0f;
    float m_sensitivity = 0.15f;
    float m_yaw = 0.0f;   // degrees, around +Y
    float m_pitch = 0.0f; // degrees
};

} // namespace Geni
