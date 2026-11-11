#include "scene/components/CharacterControllerComponent.h"

#include "core/Engine.h"
#include "scene/GameObject.h"
#include "scene/Reflection.h"
#include "scene/Scene.h"
#include "scene/components/AnimatorComponent.h"

#include <algorithm>
#include <cmath>
#include <glm/gtc/constants.hpp>

namespace Geni
{

namespace
{

glm::dvec3 ObjectWorldPosition(GameObject &object)
{
    const glm::vec3 position = object.GetWorldPosition();
    const Scene *scene = object.GetScene();
    return scene ? scene->ToWorld(position) : glm::dvec3(position);
}

void SetObjectWorldPosition(GameObject &object, const glm::dvec3 &world)
{
    const Scene *scene = object.GetScene();
    glm::vec3 position = scene ? scene->ToScene(world) : glm::vec3(world);
    if (GameObject *parent = object.GetParent())
    {
        position = glm::vec3(glm::inverse(parent->GetWorldTransform()) * glm::vec4(position, 1.0f));
    }
    object.SetPosition(position);
}

float WrapAngle(float angle)
{
    while (angle > glm::pi<float>())
    {
        angle -= glm::two_pi<float>();
    }
    while (angle < -glm::pi<float>())
    {
        angle += glm::two_pi<float>();
    }
    return angle;
}

} // namespace

void CharacterControllerComponent::Init()
{
    // The object faces -Z at yaw 0, like everything in the engine.
    const glm::vec3 forward = m_owner->GetRotation() * glm::vec3(0.0f, 0.0f, -1.0f);
    m_yaw = std::atan2(-forward.x, -forward.z);
    if (m_enabled)
    {
        CreateController();
    }
}

void CharacterControllerComponent::CreateController()
{
    PhysicsManager &physics = Engine::GetInstance().GetPhysicsManager();
    if (!physics.IsInitialized())
    {
        return;
    }
    const glm::dvec3 feet = ObjectWorldPosition(*m_owner);
    m_controller = std::make_unique<CharacterController>(physics, m_settings, feet, m_owner->GetID());
    m_previous = m_current = feet;
    m_hasStep = false;
}

void CharacterControllerComponent::OnDestroy()
{
    m_controller.reset();
}

void CharacterControllerComponent::OnOriginShift(const glm::vec3 &offset)
{
    // Simulated positions are world coordinates; the object was moved by the scene.
    (void)offset;
}

void CharacterControllerComponent::SetEnabled(bool enabled)
{
    if (enabled == m_enabled)
    {
        return;
    }
    m_enabled = enabled;
    if (enabled)
    {
        CreateController();
    }
    else
    {
        m_controller.reset();
        m_horizontalVelocity = glm::vec3(0.0f);
        m_speed = 0.0f;
        m_velocity = glm::vec3(0.0f);
    }
}

void CharacterControllerComponent::SetMoveInput(const glm::vec3 &direction, bool sprint)
{
    glm::vec3 flat(direction.x, 0.0f, direction.z);
    const float length = glm::length(flat);
    m_moveInput = length > 1.0f ? flat / length : flat;
    m_sprint = sprint;
}

void CharacterControllerComponent::Jump()
{
    m_jumpRequested = true;
}

void CharacterControllerComponent::Teleport(const glm::vec3 &scenePosition)
{
    const Scene *scene = m_owner->GetScene();
    const glm::dvec3 world = scene ? scene->ToWorld(scenePosition) : glm::dvec3(scenePosition);
    if (m_controller)
    {
        m_controller->Teleport(world);
    }
    m_previous = m_current = world;
    m_horizontalVelocity = glm::vec3(0.0f);
    SetObjectWorldPosition(*m_owner, world);
}

glm::vec3 CharacterControllerComponent::ReadPlayerInput(bool &sprint) const
{
    Engine &engine = Engine::GetInstance();
    const InputActions &actions = engine.GetInputActions();
    float x = 0.0f;
    float y = 0.0f;
    actions.GetVector("move_x", "move_y", x, y);
    sprint = actions.IsDown("sprint");

    // Relative to the camera's heading on the ground.
    glm::vec3 forward(0.0f, 0.0f, -1.0f);
    if (Scene *scene = m_owner->GetScene())
    {
        if (GameObject *camera = scene->GetMainCamera())
        {
            forward = camera->GetWorldRotation() * glm::vec3(0.0f, 0.0f, -1.0f);
        }
    }
    forward.y = 0.0f;
    if (glm::length(forward) < 1e-4f)
    {
        forward = glm::vec3(0.0f, 0.0f, -1.0f);
    }
    forward = glm::normalize(forward);
    const glm::vec3 right(-forward.z, 0.0f, forward.x);
    return forward * y + right * x;
}

void CharacterControllerComponent::Update(float deltaTime)
{
    Engine &engine = Engine::GetInstance();
    if (m_playerInput && m_enabled)
    {
        bool sprint = false;
        const glm::vec3 move = ReadPlayerInput(sprint);
        SetMoveInput(move, sprint);
        if (engine.GetInputActions().WasPressed("jump"))
        {
            m_jumpRequested = true;
        }
    }

    if (m_controller && m_hasStep)
    {
        // Between the last two simulation steps, so motion is smooth at any frame rate.
        const double alpha = std::clamp(engine.GetFixedTimestep().GetAlpha(), 0.0, 1.0);
        SetObjectWorldPosition(*m_owner, m_previous + (m_current - m_previous) * alpha);
    }

    // Turn towards where the character is going.
    const glm::vec2 flat(m_velocity.x, m_velocity.z);
    if (m_enabled && glm::length(flat) > 0.3f)
    {
        const float target = std::atan2(-flat.x, -flat.y);
        const float delta = WrapAngle(target - m_yaw);
        const float step = std::min(1.0f, m_turnSpeed * deltaTime);
        m_yaw = WrapAngle(m_yaw + delta * step);
        m_owner->SetRotation(glm::angleAxis(m_yaw, glm::vec3(0.0f, 1.0f, 0.0f)));
    }

    if (!m_speedParameter.empty())
    {
        if (auto *animator = m_owner->GetComponent<AnimatorComponent>())
        {
            animator->SetFloat(m_speedParameter, m_speed);
            animator->SetBool("grounded", m_grounded || !m_enabled);
        }
    }
}

void CharacterControllerComponent::EditUpdate()
{
    // The editor moves the object: keep the capsule with it.
    if (m_controller)
    {
        const glm::dvec3 world = ObjectWorldPosition(*m_owner);
        if (glm::length(world - m_controller->GetPosition()) > 1e-3)
        {
            m_controller->Teleport(world);
            m_previous = m_current = world;
        }
    }
}

void CharacterControllerComponent::FixedUpdate(float fixedDeltaTime)
{
    if (!m_controller)
    {
        return;
    }
    const float maxSpeed = m_sprint ? m_runSpeed : m_walkSpeed;
    const glm::vec3 target = m_moveInput * maxSpeed;
    const glm::vec3 delta = target - m_horizontalVelocity;
    const float maxChange = m_acceleration * fixedDeltaTime;
    const float length = glm::length(delta);
    m_horizontalVelocity += length > maxChange ? delta * (maxChange / length) : delta;

    const bool jump = m_jumpRequested && m_controller->IsGrounded();
    m_controller->Step(fixedDeltaTime, m_horizontalVelocity, jump ? m_jumpSpeed : 0.0f);
    m_jumpRequested = false;

    m_previous = m_hasStep ? m_current : m_controller->GetPosition();
    m_current = m_controller->GetPosition();
    m_hasStep = true;
    m_velocity = m_controller->GetVelocity();
    m_speed = glm::length(glm::vec2(m_velocity.x, m_velocity.z));
    m_grounded = m_controller->IsGrounded();
    if (jump)
    {
        if (auto *animator = m_owner->GetComponent<AnimatorComponent>())
        {
            animator->SetTrigger("jump");
        }
    }
}

namespace
{
using C = CharacterControllerComponent;
const bool s_registered = RegisterComponentType<C>(
    "CharacterControllerComponent",
    {
        FloatProperty<C>("radius", &C::GetRadius, &C::SetRadius),
        FloatProperty<C>("height", &C::GetHeight, &C::SetHeight),
        FloatProperty<C>("stepHeight", &C::GetStepHeight, &C::SetStepHeight),
        FloatProperty<C>("maxSlope", &C::GetMaxSlope, &C::SetMaxSlope),
        FloatProperty<C>("walkSpeed", &C::GetWalkSpeed, &C::SetWalkSpeed),
        FloatProperty<C>("runSpeed", &C::GetRunSpeed, &C::SetRunSpeed),
        FloatProperty<C>("jumpSpeed", &C::GetJumpSpeed, &C::SetJumpSpeed),
        FloatProperty<C>("acceleration", &C::GetAcceleration, &C::SetAcceleration),
        FloatProperty<C>("turnSpeed", &C::GetTurnSpeed, &C::SetTurnSpeed),
        BoolProperty<C>("playerInput", &C::GetPlayerInput, &C::SetPlayerInput),
        StringProperty<C>("speedParameter", &C::GetSpeedParameter, &C::SetSpeedParameter),
    }
);
} // namespace

void LinkCharacterControllerComponent()
{
}

} // namespace Geni
