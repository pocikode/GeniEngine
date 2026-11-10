#pragma once

#include "scene/Component.h"

namespace Geni
{

class PlayerControllerComponent : public Component
{
    COMPONENT(PlayerControllerComponent)

  public:
    void Update(float deltaTime) override;

    float GetSensitivity() const
    {
        return m_sensitivity;
    }
    void SetSensitivity(float sensitivity)
    {
        m_sensitivity = sensitivity;
    }
    float GetMoveSpeed() const
    {
        return m_moveSpeed;
    }
    void SetMoveSpeed(float moveSpeed)
    {
        m_moveSpeed = moveSpeed;
    }

  private:
    float m_sensitivity = 0.1f;
    float m_moveSpeed = 2.0f;
};

} // namespace Geni