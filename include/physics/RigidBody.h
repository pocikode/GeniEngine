#pragma once

#include "glm/ext/quaternion_float.hpp"
#include "glm/ext/vector_float3.hpp"
#include "physics/Collider.h"
#include <memory>

class btRigidBody;

namespace Geni
{

enum class BodyType
{
    Static,
    Dynamic,
    Kinematic
};

class RigidBody
{
  public:
    RigidBody(BodyType type, const std::shared_ptr<Collider> &collider, float mass, float friction);
    ~RigidBody();
    btRigidBody *GetBody();
    void SetAddedToWorld(bool added);
    bool IsAddedToWorld() const;

    BodyType GetType() const;

    void SetPosition(const glm::vec3 &pos);
    glm::vec3 GetPosition() const;
    void SetRotation(const glm::quat &rot);
    glm::quat GetRotation() const;

  private:
    BodyType m_type = BodyType::Static;
    std::unique_ptr<btRigidBody> m_body;
    std::shared_ptr<Collider> m_collider;
    float m_mass = 0.0f;
    float m_friction = 0.5f;
    bool m_addedToWorld = false;
};

} // namespace Geni