#include "physics/Vehicle.h"

#include "JoltInternal.h"

#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/OffsetCenterOfMassShape.h>
#include <Jolt/Physics/Vehicle/VehicleCollisionTester.h>
#include <Jolt/Physics/Vehicle/VehicleConstraint.h>
#include <Jolt/Physics/Vehicle/WheeledVehicleController.h>

#include <algorithm>

namespace Geni
{

struct Vehicle::Impl
{
    JPH::BodyID body;
    JPH::Ref<JPH::VehicleConstraint> constraint;
    JPH::WheeledVehicleController *controller = nullptr;
};

Vehicle::Vehicle(
    PhysicsManager &physics, const VehicleSettings &settings, const glm::dvec3 &position, const glm::quat &rotation,
    uint64_t userData
)
    : m_physics(physics), m_settings(settings)
{
    PhysicsManager::Impl *world = physics.GetImpl();
    if (!world)
    {
        return;
    }
    JPH::BodyInterface &bodies = world->Bodies();

    const glm::vec3 half = glm::max(settings.halfExtents, glm::vec3(0.1f));
    const float convexRadius = std::min(0.05f, 0.5f * std::min({half.x, half.y, half.z}));
    JPH::RefConst<JPH::Shape> shape =
        JPH::OffsetCenterOfMassShapeSettings(
            JPH::Vec3(0.0f, -settings.centerOfMassDrop, 0.0f), new JPH::BoxShape(ToJolt(half), convexRadius)
        )
            .Create()
            .Get();
    JPH::BodyCreationSettings bodySettings(
        shape, ToJolt(position), ToJolt(rotation), JPH::EMotionType::Dynamic, JoltLayers::Moving
    );
    bodySettings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
    bodySettings.mMassPropertiesOverride.mMass = std::max(settings.mass, 1.0f);
    bodySettings.mUserData = userData;
    bodySettings.mFriction = 0.4f;
    // Stays awake while it has a driver; Jolt puts it to sleep when it stops otherwise.
    bodySettings.mAllowSleeping = true;
    JPH::Body *body = bodies.CreateBody(bodySettings);
    if (!body)
    {
        return;
    }

    JPH::VehicleConstraintSettings vehicle;
    vehicle.mUp = JPH::Vec3::sAxisY();
    vehicle.mForward = -JPH::Vec3::sAxisZ();
    vehicle.mMaxPitchRollAngle = JPH::DegreesToRadians(settings.maxPitchRollDegrees);

    const float wheelZ[4] = {-settings.halfWheelbase, -settings.halfWheelbase, settings.halfWheelbase,
                             settings.halfWheelbase};
    const float wheelX[4] = {-settings.halfTrack, settings.halfTrack, -settings.halfTrack, settings.halfTrack};
    for (int i = 0; i < 4; ++i)
    {
        const bool front = i < 2;
        auto *wheel = new JPH::WheelSettingsWV;
        wheel->mPosition = JPH::Vec3(wheelX[i], settings.wheelHeight, wheelZ[i]);
        wheel->mSuspensionDirection = -JPH::Vec3::sAxisY();
        wheel->mSteeringAxis = JPH::Vec3::sAxisY();
        wheel->mWheelUp = JPH::Vec3::sAxisY();
        wheel->mWheelForward = -JPH::Vec3::sAxisZ();
        wheel->mSuspensionMinLength = settings.suspensionMin;
        wheel->mSuspensionMaxLength = std::max(settings.suspensionMax, settings.suspensionMin + 0.01f);
        wheel->mSuspensionSpring.mFrequency = settings.suspensionFrequency;
        wheel->mSuspensionSpring.mDamping = settings.suspensionDamping;
        wheel->mRadius = settings.wheelRadius;
        wheel->mWidth = settings.wheelWidth;
        wheel->mMaxSteerAngle = front ? JPH::DegreesToRadians(settings.maxSteerDegrees) : 0.0f;
        wheel->mMaxBrakeTorque = settings.maxBrakeTorque;
        wheel->mMaxHandBrakeTorque = front ? 0.0f : settings.maxHandBrakeTorque;
        vehicle.mWheels.push_back(wheel);
    }

    auto *controller = new JPH::WheeledVehicleControllerSettings;
    controller->mEngine.mMaxTorque = settings.maxEngineTorque;
    controller->mEngine.mMinRPM = settings.minRpm;
    controller->mEngine.mMaxRPM = settings.maxRpm;
    controller->mTransmission.mMode = JPH::ETransmissionMode::Auto;
    if (settings.fourWheelDrive)
    {
        controller->mDifferentials.resize(2);
        controller->mDifferentials[0].mLeftWheel = 0;
        controller->mDifferentials[0].mRightWheel = 1;
        controller->mDifferentials[0].mEngineTorqueRatio = 0.5f;
        controller->mDifferentials[1].mLeftWheel = 2;
        controller->mDifferentials[1].mRightWheel = 3;
        controller->mDifferentials[1].mEngineTorqueRatio = 0.5f;
    }
    else
    {
        // Rear-wheel drive.
        controller->mDifferentials.resize(1);
        controller->mDifferentials[0].mLeftWheel = 2;
        controller->mDifferentials[0].mRightWheel = 3;
    }
    vehicle.mController = controller;

    vehicle.mAntiRollBars.resize(2);
    vehicle.mAntiRollBars[0].mLeftWheel = 0;
    vehicle.mAntiRollBars[0].mRightWheel = 1;
    vehicle.mAntiRollBars[1].mLeftWheel = 2;
    vehicle.mAntiRollBars[1].mRightWheel = 3;

    m_impl = std::make_unique<Impl>();
    m_impl->body = body->GetID();
    m_impl->constraint = new JPH::VehicleConstraint(*body, vehicle);
    m_impl->constraint->SetVehicleCollisionTester(new JPH::VehicleCollisionTesterCastCylinder(JoltLayers::Moving, 0.1f));
    m_impl->controller = static_cast<JPH::WheeledVehicleController *>(m_impl->constraint->GetController());

    bodies.AddBody(m_impl->body, JPH::EActivation::Activate);
    world->system.AddConstraint(m_impl->constraint);
    world->system.AddStepListener(m_impl->constraint);
}

Vehicle::~Vehicle()
{
    PhysicsManager::Impl *world = m_physics.GetImpl();
    if (!m_impl || !world)
    {
        if (m_impl && m_impl->constraint)
        {
            // The world (and the body) are gone; the constraint still points at the body.
            m_impl->constraint->AddRef();
        }
        return;
    }
    world->system.RemoveStepListener(m_impl->constraint);
    world->system.RemoveConstraint(m_impl->constraint);
    m_impl->constraint = nullptr;
    JPH::BodyInterface &bodies = world->Bodies();
    bodies.RemoveBody(m_impl->body);
    bodies.DestroyBody(m_impl->body);
}

void Vehicle::SetInput(float forward, float right, float brake, float handBrake)
{
    PhysicsManager::Impl *world = m_physics.GetImpl();
    if (!m_impl || !world)
    {
        return;
    }
    forward = std::clamp(forward, -1.0f, 1.0f);
    right = std::clamp(right, -1.0f, 1.0f);
    brake = std::clamp(brake, 0.0f, 1.0f);
    handBrake = std::clamp(handBrake, 0.0f, 1.0f);
    m_impl->controller->SetDriverInput(forward, right, brake, handBrake);
    if (forward != 0.0f || right != 0.0f || brake != 0.0f || handBrake != 0.0f)
    {
        world->Bodies().ActivateBody(m_impl->body);
    }
}

glm::dvec3 Vehicle::GetPosition() const
{
    PhysicsManager::Impl *world = m_physics.GetImpl();
    return m_impl && world ? FromJolt(world->Bodies().GetPosition(m_impl->body)) : glm::dvec3(0.0);
}

glm::quat Vehicle::GetRotation() const
{
    PhysicsManager::Impl *world = m_physics.GetImpl();
    return m_impl && world ? FromJolt(world->Bodies().GetRotation(m_impl->body)) : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
}

glm::vec3 Vehicle::GetLinearVelocity() const
{
    PhysicsManager::Impl *world = m_physics.GetImpl();
    if (!m_impl || !world)
    {
        return glm::vec3(0.0f);
    }
    const JPH::Vec3 v = world->Bodies().GetLinearVelocity(m_impl->body);
    return glm::vec3(v.GetX(), v.GetY(), v.GetZ());
}

float Vehicle::GetForwardSpeed() const
{
    const glm::vec3 forward = GetRotation() * glm::vec3(0.0f, 0.0f, -1.0f);
    const glm::vec3 v = GetLinearVelocity();
    return v.x * forward.x + v.y * forward.y + v.z * forward.z;
}

float Vehicle::GetRpm() const
{
    return m_impl ? m_impl->controller->GetEngine().GetCurrentRPM() : 0.0f;
}

int Vehicle::GetGear() const
{
    return m_impl ? m_impl->controller->GetTransmission().GetCurrentGear() : 0;
}

void Vehicle::GetWheelLocalTransform(int wheel, glm::vec3 &position, glm::quat &rotation) const
{
    position = glm::vec3(0.0f);
    rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    if (!m_impl || wheel < 0 || wheel >= 4)
    {
        return;
    }
    // The wheel mesh's axle is its local X, its up is Y.
    const JPH::Mat44 m =
        m_impl->constraint->GetWheelLocalTransform(static_cast<JPH::uint>(wheel), JPH::Vec3::sAxisX(), JPH::Vec3::sAxisY());
    const JPH::Vec3 t = m.GetTranslation();
    position = glm::vec3(t.GetX(), t.GetY(), t.GetZ());
    rotation = FromJolt(m.GetQuaternion());
}

bool Vehicle::IsWheelOnGround(int wheel) const
{
    if (!m_impl || wheel < 0 || wheel >= 4)
    {
        return false;
    }
    return m_impl->constraint->GetWheel(static_cast<JPH::uint>(wheel))->HasContact();
}

void Vehicle::Teleport(const glm::dvec3 &position, const glm::quat &rotation)
{
    PhysicsManager::Impl *world = m_physics.GetImpl();
    if (!m_impl || !world)
    {
        return;
    }
    JPH::BodyInterface &bodies = world->Bodies();
    bodies.SetPositionAndRotation(m_impl->body, ToJolt(position), ToJolt(rotation), JPH::EActivation::Activate);
    bodies.SetLinearAndAngularVelocity(m_impl->body, JPH::Vec3::sZero(), JPH::Vec3::sZero());
}

uint32_t Vehicle::GetBodyId() const
{
    return m_impl ? m_impl->body.GetIndexAndSequenceNumber() : 0xffffffffu;
}

} // namespace Geni
