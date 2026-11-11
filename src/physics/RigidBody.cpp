#include "physics/RigidBody.h"

#include "core/Engine.h"
#include "physics/PhysicsManager.h"

namespace Geni
{

RigidBody::RigidBody(BodyType type, const std::shared_ptr<Collider> &collider, float mass, float friction)
    : m_type(type), m_collider(collider), m_mass(mass), m_friction(friction)
{
}

RigidBody::~RigidBody()
{
    if (IsAddedToWorld())
    {
        Engine::GetInstance().GetPhysicsManager().RemoveRigidBody(this);
    }
}

void RigidBody::SetWorldPosition(const glm::dvec3 &position)
{
    m_position = position;
    if (IsAddedToWorld())
    {
        m_rotation = GetRotation();
        Engine::GetInstance().GetPhysicsManager().SetBodyTransform(m_bodyId, m_position, m_rotation);
    }
}

glm::dvec3 RigidBody::GetWorldPosition() const
{
    return IsAddedToWorld() ? Engine::GetInstance().GetPhysicsManager().GetBodyPosition(m_bodyId) : m_position;
}

void RigidBody::SetRotation(const glm::quat &rotation)
{
    m_rotation = rotation;
    if (IsAddedToWorld())
    {
        m_position = GetWorldPosition();
        Engine::GetInstance().GetPhysicsManager().SetBodyTransform(m_bodyId, m_position, m_rotation);
    }
}

glm::quat RigidBody::GetRotation() const
{
    return IsAddedToWorld() ? Engine::GetInstance().GetPhysicsManager().GetBodyRotation(m_bodyId) : m_rotation;
}

} // namespace Geni
