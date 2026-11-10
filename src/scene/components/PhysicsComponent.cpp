#include "scene/components/PhysicsComponent.h"
#include "core/Engine.h"
#include "physics/RigidBody.h"
#include "scene/GameObject.h"
#include "scene/Reflection.h"

namespace Geni
{

PhysicsComponent::PhysicsComponent(const std::shared_ptr<RigidBody> &body) : m_rigidBody(body)
{
    if (body)
    {
        m_bodyType = body->GetType();
        m_mass = body->GetMass();
        m_friction = body->GetFriction();
        if (const auto &collider = body->GetCollider())
        {
            m_colliderType = collider->GetType();
            m_size = collider->GetExtents();
            m_radius = collider->GetRadius();
            m_height = collider->GetHeight();
        }
    }
}

ColliderType PhysicsComponent::GetColliderType() const
{
    if (m_rigidBody && m_rigidBody->GetCollider())
    {
        return m_rigidBody->GetCollider()->GetType();
    }
    return m_colliderType;
}

glm::vec3 PhysicsComponent::GetSize() const
{
    if (m_rigidBody && m_rigidBody->GetCollider())
    {
        return m_rigidBody->GetCollider()->GetExtents();
    }
    return m_size;
}

float PhysicsComponent::GetRadius() const
{
    if (m_rigidBody && m_rigidBody->GetCollider())
    {
        return m_rigidBody->GetCollider()->GetRadius();
    }
    return m_radius;
}

float PhysicsComponent::GetHeight() const
{
    if (m_rigidBody && m_rigidBody->GetCollider())
    {
        return m_rigidBody->GetCollider()->GetHeight();
    }
    return m_height;
}

void PhysicsComponent::Init()
{
    if (!m_rigidBody)
    {
        std::shared_ptr<Collider> collider;
        switch (m_colliderType)
        {
        case ColliderType::Box:
            collider = std::make_shared<BoxCollider>(m_size);
            break;
        case ColliderType::Sphere:
            collider = std::make_shared<SphereCollider>(m_radius);
            break;
        case ColliderType::Capsule:
            collider = std::make_shared<CapsuleCollider>(m_radius, m_height);
            break;
        }
        m_rigidBody = std::make_shared<RigidBody>(m_bodyType, collider, m_mass, m_friction);
    }

    const auto pos = m_owner->GetWorldPosition();
    const auto rot = m_owner->GetRotation();

    m_rigidBody->SetPosition(pos);
    m_rigidBody->SetRotation(rot);

    Engine::GetInstance().GetPhysicsManager().AddRigidBody(m_rigidBody.get());
}

void PhysicsComponent::Update(float deltaTime)
{
    (void)deltaTime;
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

namespace
{
const bool s_registered = RegisterComponentType<PhysicsComponent>(
    "PhysicsComponent",
    {
        EnumProperty<PhysicsComponent, BodyType>(
            "bodyType", &PhysicsComponent::GetBodyType, &PhysicsComponent::SetBodyType,
            {{BodyType::Static, "static"}, {BodyType::Dynamic, "dynamic"}, {BodyType::Kinematic, "kinematic"}}
        ),
        EnumProperty<PhysicsComponent, ColliderType>(
            "colliderType", &PhysicsComponent::GetColliderType, &PhysicsComponent::SetColliderType,
            {{ColliderType::Box, "box"}, {ColliderType::Sphere, "sphere"}, {ColliderType::Capsule, "capsule"}}
        ),
        Vec3Property<PhysicsComponent>(
            "size", [](const PhysicsComponent &component) { return component.GetSize(); },
            [](PhysicsComponent &component, const glm::vec3 &value) { component.SetSize(value); }
        ),
        FloatProperty<PhysicsComponent>("radius", &PhysicsComponent::GetRadius, &PhysicsComponent::SetRadius),
        FloatProperty<PhysicsComponent>("height", &PhysicsComponent::GetHeight, &PhysicsComponent::SetHeight),
        FloatProperty<PhysicsComponent>("mass", &PhysicsComponent::GetMass, &PhysicsComponent::SetMass),
        FloatProperty<PhysicsComponent>("friction", &PhysicsComponent::GetFriction, &PhysicsComponent::SetFriction),
    }
);
} // namespace

} // namespace Geni

namespace Geni
{
void LinkPhysicsComponent()
{
}
} // namespace Geni
