#include "scene/components/PhysicsComponent.h"
#include "core/Engine.h"
#include "physics/RigidBody.h"
#include "scene/GameObject.h"
#include "scene/Reflection.h"
#include "scene/Scene.h"

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
        m_rigidBody->SetPlane2D(m_plane2D);
    }

    // Bodies live in world coordinates (docs/design/0008): add the scene's origin, so an
    // origin shift never has to move them. The collider is sized in the object's space.
    const glm::mat4 world = m_owner->GetWorldTransform();
    const Scene *scene = m_owner->GetScene();
    const glm::vec3 position(world[3]);
    m_rigidBody->SetScale(
        glm::vec3(glm::length(glm::vec3(world[0])), glm::length(glm::vec3(world[1])), glm::length(glm::vec3(world[2])))
    );
    m_rigidBody->SetWorldPosition(scene ? scene->ToWorld(position) : glm::dvec3(position));
    m_rigidBody->SetRotation(m_owner->GetWorldRotation());
    // Ray and sphere casts report which object they hit.
    m_rigidBody->SetUserData(m_owner->GetID());

    Engine::GetInstance().GetPhysicsManager().AddRigidBody(m_rigidBody.get());
}

void PhysicsComponent::Update(float deltaTime)
{
    (void)deltaTime;
    if (!m_rigidBody)
    {
        return;
    }

    if (m_rigidBody->GetType() == BodyType::Kinematic && m_rigidBody->IsAddedToWorld())
    {
        // Something (a script, an animation) moves the object: the body follows over the
        // next physics step, with the velocity that gets it there, so it pushes what it
        // meets (docs/design/0010).
        const glm::mat4 world = m_owner->GetWorldTransform();
        const Scene *scene = m_owner->GetScene();
        const glm::vec3 position(world[3]);
        Engine::GetInstance().GetPhysicsManager().SetKinematicTarget(
            m_rigidBody->GetBodyId(), scene ? scene->ToWorld(position) : glm::dvec3(position), m_owner->GetWorldRotation()
        );
        return;
    }

    if (m_rigidBody->GetType() == BodyType::Dynamic)
    {
        const Scene *scene = m_owner->GetScene();
        const glm::dvec3 world = m_rigidBody->GetWorldPosition();
        glm::vec3 position = scene ? scene->ToScene(world) : glm::vec3(world);
        glm::quat rotation = m_rigidBody->GetRotation();
        if (GameObject *parent = m_owner->GetParent())
        {
            // The body's transform is in the world; the object's is local to its parent.
            position = glm::vec3(glm::inverse(parent->GetWorldTransform()) * glm::vec4(position, 1.0f));
            rotation = glm::inverse(parent->GetWorldRotation()) * rotation;
        }
        m_owner->SetPosition(position);
        m_owner->SetRotation(rotation);
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
        BoolProperty<PhysicsComponent>("plane2D", &PhysicsComponent::GetPlane2D, &PhysicsComponent::SetPlane2D),
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
