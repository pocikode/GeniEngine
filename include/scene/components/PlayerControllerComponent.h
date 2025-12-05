#pragma once

#include "scene/Component.h"

namespace Orbis
{

class PlayerControllerComponent : public Component
{
    COMPONENT(PlayerControllerComponent)

  public:
    void Update(float deltaTime) override;

  private:
    float m_sensitivity = 0.1f;
    float m_moveSpeed = 1.0f;
};

} // namespace Orbis