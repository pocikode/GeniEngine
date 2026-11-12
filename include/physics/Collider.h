#pragma once

#include "glm/ext/vector_float3.hpp"

namespace Geni
{

enum class ColliderType
{
    Box,
    Sphere,
    Capsule
};

// The shape of a rigid body, in the owning object's local space: PhysicsManager builds the
// physics shape when the body is added, with the object's scale applied (docs/design/0008).
// A collider can be shared by many bodies.
class Collider
{
  public:
    virtual ~Collider() = default;
    virtual ColliderType GetType() const = 0;
    // Full size of a box (not half extents).
    virtual glm::vec3 GetExtents() const
    {
        return glm::vec3(0.0f);
    }
    virtual float GetRadius() const
    {
        return 0.0f;
    }
    // Height of a capsule's cylinder, between the centres of its two half-spheres.
    virtual float GetHeight() const
    {
        return 0.0f;
    }
};

class BoxCollider : public Collider
{
  public:
    explicit BoxCollider(const glm::vec3 &extents) : m_extents(extents)
    {
    }
    ColliderType GetType() const override
    {
        return ColliderType::Box;
    }
    glm::vec3 GetExtents() const override
    {
        return m_extents;
    }

  private:
    glm::vec3 m_extents{1.0f};
};

class SphereCollider : public Collider
{
  public:
    explicit SphereCollider(float radius) : m_radius(radius)
    {
    }
    ColliderType GetType() const override
    {
        return ColliderType::Sphere;
    }
    float GetRadius() const override
    {
        return m_radius;
    }

  private:
    float m_radius = 0.5f;
};

class CapsuleCollider : public Collider
{
  public:
    CapsuleCollider(float radius, float height) : m_radius(radius), m_height(height)
    {
    }
    ColliderType GetType() const override
    {
        return ColliderType::Capsule;
    }
    float GetRadius() const override
    {
        return m_radius;
    }
    float GetHeight() const override
    {
        return m_height;
    }

  private:
    float m_radius = 0.5f;
    float m_height = 1.0f;
};

} // namespace Geni
