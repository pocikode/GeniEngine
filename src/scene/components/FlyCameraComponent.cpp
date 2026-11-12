#include "scene/components/FlyCameraComponent.h"

#include "core/Engine.h"
#include "scene/GameObject.h"
#include "scene/Reflection.h"

#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
#include <glm/gtc/quaternion.hpp>

namespace Geni
{

namespace
{
glm::quat Orientation(float yaw, float pitch)
{
    return glm::angleAxis(glm::radians(yaw), glm::vec3(0.0f, 1.0f, 0.0f)) *
           glm::angleAxis(glm::radians(pitch), glm::vec3(1.0f, 0.0f, 0.0f));
}
} // namespace

void FlyCameraComponent::Init()
{
    // Start from the object's rotation: forward is -Z.
    const glm::vec3 forward = m_owner->GetRotation() * glm::vec3(0.0f, 0.0f, -1.0f);
    m_pitch = glm::degrees(std::asin(std::clamp(forward.y, -1.0f, 1.0f)));
    m_yaw = glm::degrees(std::atan2(-forward.x, -forward.z));
}

void FlyCameraComponent::Update(float deltaTime)
{
    auto &engine = Engine::GetInstance();
    auto &input = engine.GetInputManager();
    const auto &overlay = engine.GetDebugOverlay();

    if (!overlay.WantsMouse() && input.IsMouseButtonPressed(GLFW_MOUSE_BUTTON_RIGHT))
    {
        const glm::vec2 delta = input.GetMousePositionCurrent() - input.GetMousePositionOld();
        m_yaw -= delta.x * m_sensitivity;
        m_pitch = std::clamp(m_pitch - delta.y * m_sensitivity, -89.0f, 89.0f);
    }
    const glm::quat rotation = Orientation(m_yaw, m_pitch);
    m_owner->SetRotation(rotation);

    if (overlay.WantsKeyboard())
    {
        return;
    }
    glm::vec3 move(0.0f);
    move += input.IsKeyPressed(GLFW_KEY_W) ? glm::vec3(0, 0, -1) : glm::vec3(0.0f);
    move += input.IsKeyPressed(GLFW_KEY_S) ? glm::vec3(0, 0, 1) : glm::vec3(0.0f);
    move += input.IsKeyPressed(GLFW_KEY_D) ? glm::vec3(1, 0, 0) : glm::vec3(0.0f);
    move += input.IsKeyPressed(GLFW_KEY_A) ? glm::vec3(-1, 0, 0) : glm::vec3(0.0f);
    glm::vec3 world = rotation * move;
    world.y += input.IsKeyPressed(GLFW_KEY_E) ? 1.0f : 0.0f;
    world.y -= input.IsKeyPressed(GLFW_KEY_Q) ? 1.0f : 0.0f;
    if (glm::length(world) > 0.0f)
    {
        const float speed = m_speed * (input.IsKeyPressed(GLFW_KEY_LEFT_SHIFT) ? m_boost : 1.0f);
        m_owner->SetPosition(m_owner->GetPosition() + glm::normalize(world) * speed * deltaTime);
    }
}

namespace
{
const bool s_registered = RegisterComponentType<FlyCameraComponent>(
    "FlyCameraComponent",
    {
        FloatProperty<FlyCameraComponent>("speed", &FlyCameraComponent::GetSpeed, &FlyCameraComponent::SetSpeed),
        FloatProperty<FlyCameraComponent>("boost", &FlyCameraComponent::GetBoost, &FlyCameraComponent::SetBoost),
        FloatProperty<FlyCameraComponent>(
            "sensitivity", &FlyCameraComponent::GetSensitivity, &FlyCameraComponent::SetSensitivity
        ),
    }
);
} // namespace

void LinkFlyCameraComponent()
{
}

} // namespace Geni
