#include "scene/components/VehicleComponent.h"

#include "core/Engine.h"
#include "scene/GameObject.h"
#include "scene/Reflection.h"
#include "scene/Scene.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace Geni
{

namespace
{
const char *const kWheelNames[4] = {"Wheel0", "Wheel1", "Wheel2", "Wheel3"};
}

void VehicleComponent::Init()
{
    CreateVehicle();
}

void VehicleComponent::CreateVehicle()
{
    PhysicsManager &physics = Engine::GetInstance().GetPhysicsManager();
    if (!physics.IsInitialized())
    {
        return;
    }
    const Scene *scene = m_owner->GetScene();
    const glm::vec3 position = m_owner->GetWorldPosition();
    const glm::dvec3 world = scene ? scene->ToWorld(position) : glm::dvec3(position);
    const glm::quat rotation = m_owner->GetWorldRotation();
    m_vehicle = std::make_unique<Vehicle>(physics, m_settings, world, rotation, m_owner->GetID());
    m_previousPosition = m_currentPosition = world;
    m_previousRotation = m_currentRotation = rotation;
    m_hasStep = false;
}

void VehicleComponent::OnDestroy()
{
    m_vehicle.reset();
}

void VehicleComponent::SetDriverInput(float forward, float steer, float brake, float handBrake)
{
    m_forward = std::clamp(forward, -1.0f, 1.0f);
    m_steer = std::clamp(steer, -1.0f, 1.0f);
    m_brake = std::clamp(brake, 0.0f, 1.0f);
    m_handBrake = std::clamp(handBrake, 0.0f, 1.0f);
}

void VehicleComponent::Teleport(const glm::vec3 &scenePosition, const glm::quat &rotation)
{
    const Scene *scene = m_owner->GetScene();
    const glm::dvec3 world = scene ? scene->ToWorld(scenePosition) : glm::dvec3(scenePosition);
    if (m_vehicle)
    {
        m_vehicle->Teleport(world, rotation);
    }
    m_previousPosition = m_currentPosition = world;
    m_previousRotation = m_currentRotation = rotation;
    m_owner->SetPosition(scenePosition);
    m_owner->SetRotation(rotation);
}

void VehicleComponent::Update(float deltaTime)
{
    (void)deltaTime;
    if (m_playerInput)
    {
        const InputActions &actions = Engine::GetInstance().GetInputActions();
        const float throttle = std::max(0.0f, actions.GetValue("throttle"));
        const float brake = std::max(0.0f, actions.GetValue("brake"));
        // Brake pedal reverses once the car has (almost) stopped, like most games.
        float forward = throttle;
        float brakeInput = 0.0f;
        if (brake > 0.0f)
        {
            if (m_speed > 1.0f)
            {
                brakeInput = brake;
            }
            else
            {
                forward = -brake;
            }
        }
        else if (throttle > 0.0f && m_speed < -1.0f)
        {
            // Throttle while rolling backwards brakes first.
            brakeInput = throttle;
            forward = 0.0f;
        }
        SetDriverInput(forward, actions.GetValue("steer"), brakeInput, actions.IsDown("handbrake") ? 1.0f : 0.0f);
    }

    if (!m_vehicle)
    {
        return;
    }
    // The last physics step's result; m_previous* is the state before that step.
    m_currentPosition = m_vehicle->GetPosition();
    m_currentRotation = m_vehicle->GetRotation();
    m_speed = m_vehicle->GetForwardSpeed();
    m_rpm = m_vehicle->GetRpm();
    m_gear = m_vehicle->GetGear();
    if (!m_hasStep)
    {
        m_previousPosition = m_currentPosition;
        m_previousRotation = m_currentRotation;
    }
    const double alpha = std::clamp(Engine::GetInstance().GetFixedTimestep().GetAlpha(), 0.0, 1.0);
    const glm::dvec3 world = m_previousPosition + (m_currentPosition - m_previousPosition) * alpha;
    const glm::quat rotation = glm::slerp(m_previousRotation, m_currentRotation, static_cast<float>(alpha));
    const Scene *scene = m_owner->GetScene();
    glm::vec3 position = scene ? scene->ToScene(world) : glm::vec3(world);
    glm::quat local = rotation;
    if (GameObject *parent = m_owner->GetParent())
    {
        position = glm::vec3(glm::inverse(parent->GetWorldTransform()) * glm::vec4(position, 1.0f));
        local = glm::inverse(parent->GetWorldRotation()) * rotation;
    }
    m_owner->SetPosition(position);
    m_owner->SetRotation(local);
    PlaceWheels();
}

void VehicleComponent::PlaceWheels()
{
    // Wheel transforms are in chassis space; a wheel child with a scale (its mesh size)
    // keeps it.
    for (int i = 0; i < 4; ++i)
    {
        if (GameObject *wheel = m_owner->FindChildByName(kWheelNames[i]))
        {
            if (wheel->GetParent() != m_owner)
            {
                continue;
            }
            glm::vec3 position;
            glm::quat rotation;
            m_vehicle->GetWheelLocalTransform(i, position, rotation);
            wheel->SetPosition(position);
            wheel->SetRotation(rotation);
        }
    }
}

void VehicleComponent::EditUpdate()
{
    if (!m_vehicle)
    {
        return;
    }
    const Scene *scene = m_owner->GetScene();
    const glm::vec3 position = m_owner->GetWorldPosition();
    const glm::dvec3 world = scene ? scene->ToWorld(position) : glm::dvec3(position);
    const glm::quat rotation = m_owner->GetWorldRotation();
    if (glm::length(world - m_vehicle->GetPosition()) > 1e-3 ||
        std::fabs(glm::dot(rotation, m_vehicle->GetRotation())) < 0.99999f)
    {
        m_vehicle->Teleport(world, rotation);
        m_previousPosition = m_currentPosition = world;
        m_previousRotation = m_currentRotation = rotation;
    }
}

void VehicleComponent::FixedUpdate(float fixedDeltaTime)
{
    if (!m_vehicle)
    {
        return;
    }
    // Ease the steering in and back to centre; faster at low speed.
    const float rate = (std::fabs(m_steer) > std::fabs(m_smoothedSteer) ? 3.0f : 5.0f) * fixedDeltaTime;
    m_smoothedSteer += std::clamp(m_steer - m_smoothedSteer, -rate, rate);
    m_vehicle->SetInput(m_forward, m_smoothedSteer, m_brake, m_handBrake);

    // The constraint runs inside the physics step that follows; record where the body
    // is before it, so the frame can interpolate towards the step's result.
    m_previousPosition = m_vehicle->GetPosition();
    m_previousRotation = m_vehicle->GetRotation();
    m_hasStep = true;
}

namespace
{
using V = VehicleComponent;
const bool s_registered = RegisterComponentType<V>(
    "VehicleComponent",
    {
        FloatProperty<V>("mass", &V::GetMass, &V::SetMass),
        Vec3Property<V>("halfExtents", &V::GetHalfExtents, &V::SetHalfExtents),
        FloatProperty<V>("engineTorque", &V::GetEngineTorque, &V::SetEngineTorque),
        FloatProperty<V>("maxRpm", &V::GetMaxRpm, &V::SetMaxRpm),
        FloatProperty<V>("wheelRadius", &V::GetWheelRadius, &V::SetWheelRadius),
        FloatProperty<V>("wheelWidth", &V::GetWheelWidth, &V::SetWheelWidth),
        FloatProperty<V>("halfTrack", &V::GetHalfTrack, &V::SetHalfTrack),
        FloatProperty<V>("halfWheelbase", &V::GetHalfWheelbase, &V::SetHalfWheelbase),
        FloatProperty<V>("wheelHeight", &V::GetWheelHeight, &V::SetWheelHeight),
        FloatProperty<V>("suspensionTravel", &V::GetSuspensionTravel, &V::SetSuspensionTravel),
        FloatProperty<V>("maxSteer", &V::GetMaxSteer, &V::SetMaxSteer),
        BoolProperty<V>("fourWheelDrive", &V::GetFourWheelDrive, &V::SetFourWheelDrive),
        BoolProperty<V>("playerInput", &V::GetPlayerInput, &V::SetPlayerInput),
    }
);
} // namespace

void LinkVehicleComponent()
{
}

} // namespace Geni
