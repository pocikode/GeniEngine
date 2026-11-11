#include "physics/CharacterController.h"

#include "JoltInternal.h"

#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>

#include <algorithm>
#include <cmath>

namespace Geni
{

struct CharacterController::Impl
{
    JPH::Ref<JPH::CharacterVirtual> character;
};

CharacterController::CharacterController(
    PhysicsManager &physics, const CharacterSettings &settings, const glm::dvec3 &feetPosition, uint64_t userData
)
    : m_physics(physics), m_settings(settings)
{
    PhysicsManager::Impl *world = physics.GetImpl();
    if (!world)
    {
        return;
    }
    const float radius = std::max(settings.radius, 0.05f);
    const float halfCylinder = std::max(0.5f * settings.height - radius, 0.01f);
    // The shape's origin is at the feet, so the character's position is where it stands.
    JPH::RefConst<JPH::Shape> capsule =
        JPH::RotatedTranslatedShapeSettings(
            JPH::Vec3(0.0f, halfCylinder + radius, 0.0f), JPH::Quat::sIdentity(), new JPH::CapsuleShape(halfCylinder, radius)
        )
            .Create()
            .Get();
    // A slightly thinner inner body, so it never blocks the character's own sweeps.
    const float innerRadius = radius * 0.9f;
    JPH::RefConst<JPH::Shape> inner =
        JPH::RotatedTranslatedShapeSettings(
            JPH::Vec3(0.0f, halfCylinder + radius, 0.0f), JPH::Quat::sIdentity(),
            new JPH::CapsuleShape(halfCylinder, innerRadius)
        )
            .Create()
            .Get();

    JPH::CharacterVirtualSettings virtualSettings;
    virtualSettings.mShape = capsule;
    virtualSettings.mMaxSlopeAngle = JPH::DegreesToRadians(settings.maxSlopeDegrees);
    virtualSettings.mMass = settings.mass;
    virtualSettings.mMaxStrength = settings.maxStrength;
    virtualSettings.mCharacterPadding = 0.02f;
    virtualSettings.mPenetrationRecoverySpeed = 1.0f;
    virtualSettings.mPredictiveContactDistance = 0.1f;
    // Ignore contacts below this plane (the capsule's bottom sphere) for supporting.
    virtualSettings.mSupportingVolume = JPH::Plane(JPH::Vec3::sAxisY(), -radius);
    virtualSettings.mInnerBodyShape = inner;
    virtualSettings.mInnerBodyLayer = JoltLayers::Moving;
    virtualSettings.mEnhancedInternalEdgeRemoval = true;

    m_impl = std::make_unique<Impl>();
    m_impl->character = new JPH::CharacterVirtual(
        &virtualSettings, ToJolt(feetPosition), JPH::Quat::sIdentity(), userData, &world->system
    );
}

CharacterController::~CharacterController()
{
    // CharacterVirtual removes and destroys its inner body in its destructor; the physics
    // system must still exist (components go before PhysicsManager::Shutdown).
    if (m_impl && !m_physics.GetImpl())
    {
        // The world is gone already, and the inner body with it: the character's
        // destructor would reach into the dead system, so leak the (small) object.
        m_impl->character->AddRef();
    }
    m_impl.reset();
}

void CharacterController::Step(
    float deltaTime, const glm::vec3 &horizontalVelocity, float jump, float airControl, float gravityScale
)
{
    PhysicsManager::Impl *world = m_physics.GetImpl();
    if (!m_impl || !world || deltaTime <= 0.0f)
    {
        return;
    }
    JPH::CharacterVirtual &character = *m_impl->character;
    const JPH::Vec3 up = JPH::Vec3::sAxisY();
    const JPH::Vec3 gravity = world->system.GetGravity() * gravityScale;

    character.UpdateGroundVelocity();
    const JPH::Vec3 current = character.GetLinearVelocity();
    const JPH::Vec3 currentVertical = up * current.Dot(up);
    const JPH::Vec3 groundVelocity = character.GetGroundVelocity();
    const bool movingTowardsGround = (currentVertical.GetY() - groundVelocity.GetY()) < 0.1f;
    const JPH::Vec3 desired(horizontalVelocity.x, 0.0f, horizontalVelocity.z);

    JPH::Vec3 velocity;
    const bool supported = character.GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround &&
                           !character.IsSlopeTooSteep(character.GetGroundNormal());
    if (supported && movingTowardsGround)
    {
        // Standing: take the ground's velocity (a moving platform), then walk on it.
        velocity = groundVelocity + desired;
        if (jump > 0.0f)
        {
            velocity += up * jump;
        }
    }
    else
    {
        // In the air: keep momentum, steer a little.
        const JPH::Vec3 horizontal = current - currentVertical;
        const float control = std::clamp(airControl, 0.0f, 1.0f);
        velocity = currentVertical + horizontal + (desired - horizontal) * std::min(1.0f, control * deltaTime * 5.0f);
    }
    velocity += gravity * deltaTime;
    character.SetLinearVelocity(velocity);

    JPH::CharacterVirtual::ExtendedUpdateSettings update;
    update.mWalkStairsStepUp = up * m_settings.stepHeight;
    update.mStickToFloorStepDown = -up * std::max(0.5f, m_settings.stepHeight);
    const JPH::DefaultBroadPhaseLayerFilter broadPhase =
        world->system.GetDefaultBroadPhaseLayerFilter(JoltLayers::Moving);
    const JPH::DefaultObjectLayerFilter layers = world->system.GetDefaultLayerFilter(JoltLayers::Moving);
    const JPH::BodyFilter bodies;
    const JPH::ShapeFilter shapes;
    character.ExtendedUpdate(deltaTime, gravity, update, broadPhase, layers, bodies, shapes, *world->tempAllocator);
}

glm::dvec3 CharacterController::GetPosition() const
{
    return m_impl ? FromJolt(m_impl->character->GetPosition()) : glm::dvec3(0.0);
}

void CharacterController::Teleport(const glm::dvec3 &feetPosition)
{
    PhysicsManager::Impl *world = m_physics.GetImpl();
    if (!m_impl || !world)
    {
        return;
    }
    m_impl->character->SetPosition(ToJolt(feetPosition));
    m_impl->character->SetLinearVelocity(JPH::Vec3::sZero());
    const JPH::DefaultBroadPhaseLayerFilter broadPhase =
        world->system.GetDefaultBroadPhaseLayerFilter(JoltLayers::Moving);
    const JPH::DefaultObjectLayerFilter layers = world->system.GetDefaultLayerFilter(JoltLayers::Moving);
    m_impl->character->RefreshContacts(broadPhase, layers, JPH::BodyFilter(), JPH::ShapeFilter(), *world->tempAllocator);
}

glm::vec3 CharacterController::GetVelocity() const
{
    if (!m_impl)
    {
        return glm::vec3(0.0f);
    }
    const JPH::Vec3 v = m_impl->character->GetLinearVelocity();
    return glm::vec3(v.GetX(), v.GetY(), v.GetZ());
}

CharacterGround CharacterController::GetGround() const
{
    if (!m_impl)
    {
        return CharacterGround::InAir;
    }
    switch (m_impl->character->GetGroundState())
    {
    case JPH::CharacterVirtual::EGroundState::OnGround:
        return CharacterGround::OnGround;
    case JPH::CharacterVirtual::EGroundState::OnSteepGround:
        return CharacterGround::OnSteepGround;
    case JPH::CharacterVirtual::EGroundState::NotSupported:
        return CharacterGround::NotSupported;
    case JPH::CharacterVirtual::EGroundState::InAir:
        break;
    }
    return CharacterGround::InAir;
}

glm::vec3 CharacterController::GetGroundNormal() const
{
    if (!m_impl)
    {
        return glm::vec3(0.0f, 1.0f, 0.0f);
    }
    const JPH::Vec3 n = m_impl->character->GetGroundNormal();
    return glm::vec3(n.GetX(), n.GetY(), n.GetZ());
}

uint32_t CharacterController::GetInnerBodyId() const
{
    return m_impl ? m_impl->character->GetInnerBodyID().GetIndexAndSequenceNumber() : 0xffffffffu;
}

} // namespace Geni
