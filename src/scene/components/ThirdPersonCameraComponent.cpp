#include "scene/components/ThirdPersonCameraComponent.h"

#include "core/Engine.h"
#include "scene/GameObject.h"
#include "scene/Reflection.h"
#include "scene/Scene.h"
#include "scene/components/VehicleComponent.h"

#include <algorithm>
#include <cmath>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/trigonometric.hpp>

namespace Geni
{

namespace
{
float WrapDegrees(float angle)
{
    angle = std::fmod(angle + 180.0f, 360.0f);
    if (angle < 0.0f)
    {
        angle += 360.0f;
    }
    return angle - 180.0f;
}

// Heading of an object in the camera's yaw convention (degrees): the yaw at which the
// camera sits behind it.
float HeadingDegrees(const GameObject &object)
{
    const glm::vec3 forward = object.GetWorldRotation() * glm::vec3(0.0f, 0.0f, -1.0f);
    return glm::degrees(std::atan2(-forward.x, -forward.z));
}
} // namespace

void ThirdPersonCameraComponent::SetTarget(GameObject *target)
{
    m_targetID = target ? target->GetID() : 0;
    m_targetName = target ? target->GetName() : std::string();
}

void ThirdPersonCameraComponent::SetTargetName(const std::string &name)
{
    m_targetName = name;
    m_targetID = 0;
}

GameObject *ThirdPersonCameraComponent::GetTargetObject() const
{
    Scene *scene = m_owner ? m_owner->GetScene() : nullptr;
    if (!scene)
    {
        return nullptr;
    }
    if (m_targetID != 0)
    {
        if (GameObject *object = scene->FindObjectByID(m_targetID))
        {
            return object;
        }
    }
    return m_targetName.empty() ? nullptr : scene->FindObject(m_targetName);
}

void ThirdPersonCameraComponent::SetDistance(float value)
{
    m_distance = std::clamp(value, m_minDistance, m_maxDistance);
}

void ThirdPersonCameraComponent::SetPitch(float value)
{
    m_pitch = std::clamp(value, m_minPitch, m_maxPitch);
}

void ThirdPersonCameraComponent::SnapBehindTarget()
{
    if (GameObject *target = GetTargetObject())
    {
        m_yaw = HeadingDegrees(*target);
    }
}

void ThirdPersonCameraComponent::LateUpdate(float deltaTime)
{
    GameObject *target = GetTargetObject();
    if (!target)
    {
        return;
    }
    m_targetID = target->GetID();
    Engine &engine = Engine::GetInstance();

    if (m_playerInput)
    {
        const InputActions &actions = engine.GetInputActions();
        const float lookX = actions.GetValue("look_x");
        const float lookY = actions.GetValue("look_y");
        // Gamepad sticks give a rate, the mouse a distance: the bindings' scales make
        // both degrees per frame at 60 fps, so scale the stick part by frame time.
        const bool pad = actions.GetLastDevice() == InputDevice::Gamepad;
        const float rate = pad ? deltaTime * 60.0f : 1.0f;
        if (lookX != 0.0f || lookY != 0.0f)
        {
            m_timeSinceLook = 0.0f;
        }
        m_yaw = WrapDegrees(m_yaw - lookX * rate);
        m_pitch = std::clamp(m_pitch + lookY * rate, m_minPitch, m_maxPitch);
        const float zoom = actions.GetValue("zoom");
        if (zoom != 0.0f)
        {
            m_distance = std::clamp(m_distance - zoom * 0.6f, m_minDistance, m_maxDistance);
        }
    }
    m_timeSinceLook += deltaTime;

    // Swing behind a moving vehicle once the player stops looking around.
    if (m_autoFollow && m_timeSinceLook > 1.2f)
    {
        if (auto *vehicle = target->GetComponent<VehicleComponent>())
        {
            const float speed = vehicle->GetSpeed();
            if (std::fabs(speed) > 2.0f)
            {
                const float heading = HeadingDegrees(*target) + (speed < 0.0f ? 180.0f : 0.0f);
                const float delta = WrapDegrees(heading - m_yaw);
                m_yaw = WrapDegrees(m_yaw + delta * std::min(1.0f, deltaTime * 2.5f));
                m_pitch += (12.0f - m_pitch) * std::min(1.0f, deltaTime * 1.5f);
            }
        }
    }

    const glm::vec3 pivot = target->GetWorldPosition() + glm::vec3(0.0f, m_height, 0.0f);
    const float yaw = glm::radians(m_yaw);
    const float pitch = glm::radians(m_pitch);
    const glm::vec3 direction(std::cos(pitch) * std::sin(yaw), std::sin(pitch), std::cos(pitch) * std::cos(yaw));

    // Pull in when static geometry is between the pivot and the camera; ease back out.
    float distance = m_distance;
    PhysicsManager &physics = engine.GetPhysicsManager();
    Scene *scene = m_owner->GetScene();
    if (physics.IsInitialized() && scene)
    {
        PhysicsHit hit;
        if (physics.CastSphere(scene->ToWorld(pivot), m_collisionRadius, direction, m_distance, hit, PhysicsLayerStatic))
        {
            distance = std::max(0.3f, hit.distance - 0.05f);
        }
    }
    if (m_currentDistance < 0.0f || distance < m_currentDistance)
    {
        m_currentDistance = distance;
    }
    else
    {
        m_currentDistance += (distance - m_currentDistance) * std::min(1.0f, deltaTime * 4.0f);
    }

    glm::vec3 position = pivot + direction * m_currentDistance;
    glm::vec3 front = glm::normalize(pivot - position);
    glm::vec3 right = glm::cross(front, glm::vec3(0.0f, 1.0f, 0.0f));
    if (glm::length(right) < 1e-4f)
    {
        right = glm::vec3(1.0f, 0.0f, 0.0f);
    }
    right = glm::normalize(right);
    const glm::vec3 up = glm::normalize(glm::cross(right, front));
    glm::quat rotation = glm::quat_cast(glm::mat3(right, up, -front));
    if (GameObject *parent = m_owner->GetParent())
    {
        position = glm::vec3(glm::inverse(parent->GetWorldTransform()) * glm::vec4(position, 1.0f));
        rotation = glm::inverse(parent->GetWorldRotation()) * rotation;
    }
    m_owner->SetPosition(position);
    m_owner->SetRotation(rotation);
}

namespace
{
using T = ThirdPersonCameraComponent;
const bool s_registered = RegisterComponentType<T>(
    "ThirdPersonCameraComponent",
    {
        StringProperty<T>("target", &T::GetTargetName, &T::SetTargetName),
        FloatProperty<T>("distance", &T::GetDistance, &T::SetDistance),
        FloatProperty<T>("minDistance", &T::GetMinDistance, &T::SetMinDistance),
        FloatProperty<T>("maxDistance", &T::GetMaxDistance, &T::SetMaxDistance),
        FloatProperty<T>("height", &T::GetHeight, &T::SetHeight),
        FloatProperty<T>("yaw", &T::GetYaw, &T::SetYaw),
        FloatProperty<T>("pitch", &T::GetPitch, &T::SetPitch),
        FloatProperty<T>("minPitch", &T::GetMinPitch, &T::SetMinPitch),
        FloatProperty<T>("maxPitch", &T::GetMaxPitch, &T::SetMaxPitch),
        FloatProperty<T>("collisionRadius", &T::GetCollisionRadius, &T::SetCollisionRadius),
        BoolProperty<T>("autoFollow", &T::GetAutoFollow, &T::SetAutoFollow),
        BoolProperty<T>("playerInput", &T::GetPlayerInput, &T::SetPlayerInput),
    }
);
} // namespace

void LinkThirdPersonCameraComponent()
{
}

} // namespace Geni
