#pragma once

#include "physics/RigidBody.h"
#include "scene/Component.h"
#include <glm/vec3.hpp>
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

    const std::shared_ptr<RigidBody> &GetRigidBody() const
    {
        return m_rigidBody;
    }

    void SetBodyType(BodyType type)
    {
        m_bodyType = type;
    }
    BodyType GetBodyType() const
    {
        return m_rigidBody ? m_rigidBody->GetType() : m_bodyType;
    }
    void SetColliderType(ColliderType type)
    {
        m_colliderType = type;
    }
    ColliderType GetColliderType() const;
    void SetSize(const glm::vec3 &size)
    {
        m_size = size;
    }
    glm::vec3 GetSize() const;
    void SetRadius(float radius)
    {
        m_radius = radius;
    }
    float GetRadius() const;
    void SetHeight(float height)
    {
        m_height = height;
    }
    float GetHeight() const;
    void SetMass(float mass)
    {
        m_mass = mass;
    }
    float GetMass() const
    {
        return m_rigidBody ? m_rigidBody->GetMass() : m_mass;
    }
    void SetFriction(float friction)
    {
        m_friction = friction;
    }
    float GetFriction() const
    {
        return m_rigidBody ? m_rigidBody->GetFriction() : m_friction;
    }

  private:
    std::shared_ptr<RigidBody> m_rigidBody;
    BodyType m_bodyType = BodyType::Static;
    ColliderType m_colliderType = ColliderType::Box;
    glm::vec3 m_size{1.0f};
    float m_radius = 0.5f;
    float m_height = 1.0f;
    float m_mass = 0.0f;
    float m_friction = 0.5f;
};

} // namespace Geni
