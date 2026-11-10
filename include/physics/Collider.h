#pragma once

#include "glm/ext/vector_float3.hpp"
class btCollisionShape;

namespace Geni
{

enum class ColliderType
{
    Box,
    Sphere,
    Capsule
};

class Collider
{
  public:
    virtual ~Collider();
    btCollisionShape *GetShape();
    virtual ColliderType GetType() const = 0;
    virtual glm::vec3 GetExtents() const
    {
        return glm::vec3(0.0f);
    }
    virtual float GetRadius() const
    {
        return 0.0f;
    }
    virtual float GetHeight() const
    {
        return 0.0f;
    }

  protected:
    btCollisionShape *m_shape = nullptr;
};

class BoxCollider : public Collider
{
  public:
    BoxCollider(const glm::vec3 &extents);
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
    SphereCollider(float radius);
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
    CapsuleCollider(float radius, float height);
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