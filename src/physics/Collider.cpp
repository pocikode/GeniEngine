#include "physics/Collider.h"
#include "BulletCollision/CollisionShapes/btBoxShape.h"
#include "BulletCollision/CollisionShapes/btCapsuleShape.h"
#include "BulletCollision/CollisionShapes/btSphereShape.h"
#include "LinearMath/btScalar.h"
#include "LinearMath/btVector3.h"
#include "glm/ext/vector_float3.hpp"

namespace Geni
{

Collider::~Collider()
{
    if (m_shape)
    {
        delete m_shape;
    }
}

btCollisionShape *Collider::GetShape()
{
    return m_shape;
}

BoxCollider::BoxCollider(const glm::vec3 &extents)
{
    glm::vec3 halfExtents = extents * 0.5f;
    m_shape = new btBoxShape(btVector3(halfExtents.x, halfExtents.y, halfExtents.z));
}

SphereCollider::SphereCollider(float radius)
{
    m_shape = new btSphereShape(radius);
}

CapsuleCollider::CapsuleCollider(float radius, float height)
{
    m_shape = new btCapsuleShape(btScalar(radius), btScalar(height));
}

} // namespace Geni