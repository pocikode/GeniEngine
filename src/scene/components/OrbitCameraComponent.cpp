#include "scene/components/OrbitCameraComponent.h"
#include "core/Engine.h"
#include "scene/GameObject.h"
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/constants.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/trigonometric.hpp>
#include <GLFW/glfw3.h>

namespace Geni
{

void OrbitCameraComponent::Update(float deltaTime)
{
    auto &inputManager = Engine::GetInstance().GetInputManager();

    // Check if ImGui wants mouse input (engine will handle this through imgui integration if available)
    // For now, we just use input directly
    bool imguiWantsMouse = false;

    if (!imguiWantsMouse)
    {
        glm::vec2 mouseDelta = inputManager.GetMousePositionCurrent() - inputManager.GetMousePositionOld();

        if (inputManager.IsMouseButtonPressed(GLFW_MOUSE_BUTTON_LEFT))
        {
            m_yaw -= mouseDelta.x * m_orbitSensitivity;
            m_pitch -= mouseDelta.y * m_orbitSensitivity;
            m_pitch = glm::clamp(m_pitch, -89.0f, 89.0f);
        }

        if (inputManager.IsMouseButtonPressed(GLFW_MOUSE_BUTTON_RIGHT))
        {
            float yawRad = glm::radians(m_yaw);
            float pitchRad = glm::radians(m_pitch);

            glm::vec3 forward = {
                glm::cos(pitchRad) * glm::sin(yawRad),
                glm::sin(pitchRad),
                glm::cos(pitchRad) * glm::cos(yawRad)
            };

            glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0, 1, 0)));
            glm::vec3 up = glm::normalize(glm::cross(right, forward));

            float panScale = m_panSensitivity * m_distance;
            m_target -= right * mouseDelta.x * panScale;
            m_target += up * mouseDelta.y * panScale;
        }

        float scrollDelta = inputManager.GetScrollDelta();
        if (scrollDelta != 0.0f)
        {
            m_distance = glm::clamp(m_distance - scrollDelta * m_zoomSensitivity, m_minDistance, m_maxDistance);
        }
    }

    // Compute camera position from spherical coordinates
    float yawRad = glm::radians(m_yaw);
    float pitchRad = glm::radians(m_pitch);

    glm::vec3 offset = {
        m_distance * glm::cos(pitchRad) * glm::sin(yawRad),
        m_distance * glm::sin(pitchRad),
        m_distance * glm::cos(pitchRad) * glm::cos(yawRad)
    };

    glm::vec3 cameraPos = m_target + offset;
    m_owner->SetPosition(cameraPos);

    // Orient camera to look at target
    glm::vec3 front = glm::normalize(m_target - cameraPos);
    glm::vec3 right = glm::normalize(glm::cross(front, glm::vec3(0, 1, 0)));
    glm::vec3 up = glm::normalize(glm::cross(right, front));

    glm::mat3 rotMatrix = glm::mat3(right, up, -front);
    m_owner->SetRotation(glm::quat_cast(glm::mat4(rotMatrix)));
}

void OrbitCameraComponent::SetTarget(const glm::vec3 &target)
{
    m_target = target;
}

void OrbitCameraComponent::SetDistance(float distance)
{
    m_distance = glm::clamp(distance, m_minDistance, m_maxDistance);
}

void OrbitCameraComponent::SetYaw(float yaw)
{
    m_yaw = yaw;
}

void OrbitCameraComponent::SetPitch(float pitch)
{
    m_pitch = glm::clamp(pitch, -89.0f, 89.0f);
}

void OrbitCameraComponent::SetOrbitSensitivity(float s)
{
    m_orbitSensitivity = s;
}

void OrbitCameraComponent::SetPanSensitivity(float s)
{
    m_panSensitivity = s;
}

void OrbitCameraComponent::SetZoomSensitivity(float s)
{
    m_zoomSensitivity = s;
}

glm::vec3 OrbitCameraComponent::GetTarget() const
{
    return m_target;
}

float OrbitCameraComponent::GetDistance() const
{
    return m_distance;
}

float OrbitCameraComponent::GetYaw() const
{
    return m_yaw;
}

float OrbitCameraComponent::GetPitch() const
{
    return m_pitch;
}

} // namespace Geni
