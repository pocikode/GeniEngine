#pragma once

#include "glm/ext/quaternion_float.hpp"
#include "glm/ext/vector_double3.hpp"
#include "glm/ext/vector_float3.hpp"
#include "physics/Collider.h"
#include <cstdint>
#include <memory>

namespace Geni
{

enum class BodyType
{
    Static,
    Dynamic,
    Kinematic
};

// A body in the physics world (Jolt, docs/design/0008). Positions are world positions in
// double precision; PhysicsComponent converts to and from scene space (Scene::ToWorld).
// The transform can be set before the body is added to the world; PhysicsManager creates
// the physics object then.
class RigidBody
{
  public:
    static constexpr uint32_t InvalidBodyId = 0xffffffffu;

    RigidBody(BodyType type, const std::shared_ptr<Collider> &collider, float mass, float friction);
    ~RigidBody();
    RigidBody(const RigidBody &) = delete;
    RigidBody &operator=(const RigidBody &) = delete;

    BodyType GetType() const
    {
        return m_type;
    }
    const std::shared_ptr<Collider> &GetCollider() const
    {
        return m_collider;
    }
    float GetMass() const
    {
        return m_mass;
    }
    float GetFriction() const
    {
        return m_friction;
    }

    // Scale applied to the collider when the body is added (the object's world scale).
    void SetScale(const glm::vec3 &scale)
    {
        m_scale = scale;
    }
    const glm::vec3 &GetScale() const
    {
        return m_scale;
    }

    void SetWorldPosition(const glm::dvec3 &position);
    glm::dvec3 GetWorldPosition() const;
    // Single-precision versions for scenes whose origin is at zero.
    void SetPosition(const glm::vec3 &position)
    {
        SetWorldPosition(glm::dvec3(position));
    }
    glm::vec3 GetPosition() const
    {
        return glm::vec3(GetWorldPosition());
    }
    void SetRotation(const glm::quat &rotation);
    glm::quat GetRotation() const;

    // Returned with ray and sphere hits (PhysicsHit::userData). Set before the body is
    // added to the world.
    void SetUserData(uint64_t userData)
    {
        m_userData = userData;
    }
    uint64_t GetUserData() const
    {
        return m_userData;
    }

    // The physics world's body (InvalidBodyId until PhysicsManager::AddRigidBody).
    uint32_t GetBodyId() const
    {
        return m_bodyId;
    }
    bool IsAddedToWorld() const
    {
        return m_bodyId != InvalidBodyId;
    }

  private:
    friend class PhysicsManager;

    BodyType m_type = BodyType::Static;
    std::shared_ptr<Collider> m_collider;
    float m_mass = 0.0f;
    float m_friction = 0.5f;
    glm::vec3 m_scale{1.0f};
    // Used before the body exists.
    glm::dvec3 m_position{0.0};
    glm::quat m_rotation{1.0f, 0.0f, 0.0f, 0.0f};
    uint32_t m_bodyId = InvalidBodyId;
    uint64_t m_userData = 0;
};

} // namespace Geni
