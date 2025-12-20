#pragma once

#include "physics/RigidBody.h"
#include "scene/Component.h"
#include <memory>

namespace Geni
{

class PhysicsComponent : public Component
{
    COMPONENT(PhysicsComponent);

  public:
    PhysicsComponent() = default;
    PhysicsComponent(const std::shared_ptr<RigidBody> &body);

    void Init() override;
    void Update(float deltaTime) override;

  private:
    std::shared_ptr<RigidBody> m_rigidBody;
};

} // namespace Geni