#include "scene/components/PhysicsComponent.h"
#include "core/Engine.h"
#include "physics/RigidBody.h"
#include "scene/GameObject.h"

namespace Geni
{

PhysicsComponent::PhysicsComponent(const std::shared_ptr<RigidBody> &body) : m_rigidBody(body)
{
}

void PhysicsComponent::Init()
{
    if (!m_rigidBody)
    {
        return;
    }

    const auto pos = m_owner->GetWorldPosition();
    const auto rot = m_owner->GetRotation();

    m_rigidBody->SetPosition(pos);
    m_rigidBody->SetRotation(rot);

    Engine::GetInstance().GetPhysicsManager().AddRigidBody(m_rigidBody.get());
}

void PhysicsComponent::Update(float deltaTime)
{
    if (!m_rigidBody)
    {
        return;
    }

    if (m_rigidBody->GetType() == BodyType::Dynamic)
    {
        m_owner->SetPosition(m_rigidBody->GetPosition());
        m_owner->SetRotation(m_rigidBody->GetRotation());
    }
}

} // namespace Geni