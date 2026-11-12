#include "EditorCamera.h"

#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

namespace Geni::Editor
{

namespace
{
constexpr float kLookDegreesPerPixel = 0.2f;
constexpr float kMaxPitch = 89.0f;
} // namespace

void EditorCamera::SetAngles(float yawDegrees, float pitchDegrees)
{
    m_yaw = yawDegrees;
    m_pitch = std::clamp(pitchDegrees, -kMaxPitch, kMaxPitch);
}

glm::quat EditorCamera::GetRotation() const
{
    const glm::quat yaw = glm::angleAxis(glm::radians(m_yaw), glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::quat pitch = glm::angleAxis(glm::radians(m_pitch), glm::vec3(1.0f, 0.0f, 0.0f));
    return yaw * pitch;
}

glm::vec3 EditorCamera::GetForward() const
{
    return GetRotation() * glm::vec3(0.0f, 0.0f, -1.0f);
}

glm::vec3 EditorCamera::GetRight() const
{
    return GetRotation() * glm::vec3(1.0f, 0.0f, 0.0f);
}

glm::vec3 EditorCamera::GetUp() const
{
    return GetRotation() * glm::vec3(0.0f, 1.0f, 0.0f);
}

glm::mat4 EditorCamera::GetViewMatrix() const
{
    return glm::lookAt(m_position, m_position + GetForward(), glm::vec3(0.0f, 1.0f, 0.0f));
}

void EditorCamera::Update(const CameraInput &input, float deltaTime)
{
    if (input.look)
    {
        SetAngles(m_yaw - input.mouseDelta.x * kLookDegreesPerPixel, m_pitch - input.mouseDelta.y * kLookDegreesPerPixel);
        const float speed = moveSpeed * (input.fast ? 4.0f : 1.0f) * deltaTime;
        m_position += (GetRight() * input.move.x + glm::vec3(0.0f, 1.0f, 0.0f) * input.move.y +
                       GetForward() * input.move.z) * speed;
    }
    else if (input.orbit)
    {
        // Turn around the pivot: the pivot stays put, the camera swings.
        const glm::vec3 pivot = GetPivot();
        SetAngles(m_yaw - input.mouseDelta.x * kLookDegreesPerPixel, m_pitch - input.mouseDelta.y * kLookDegreesPerPixel);
        m_position = pivot - GetForward() * m_pivotDistance;
    }
    else if (input.pan)
    {
        // One pixel moves the pivot by about one pixel's worth of world at its distance.
        const float scale = m_pivotDistance * 0.0015f;
        m_position += (-GetRight() * input.mouseDelta.x + GetUp() * input.mouseDelta.y) * scale;
    }

    if (input.scroll != 0.0f)
    {
        // Dolly a fraction of the way to the pivot per step, so it slows down as it nears.
        const float step = m_pivotDistance * (1.0f - std::pow(0.85f, input.scroll));
        m_position += GetForward() * step;
        m_pivotDistance = std::max(m_pivotDistance - step, 0.5f);
    }
}

void EditorCamera::Focus(const glm::vec3 &center, float radius)
{
    const float halfFov = glm::radians(fieldOfViewDegrees) * 0.5f;
    m_pivotDistance = std::max(radius, 0.25f) / std::sin(halfFov) * 1.2f;
    m_position = center - GetForward() * m_pivotDistance;
}

ViewOverride EditorCamera::GetViewOverride() const
{
    ViewOverride view;
    view.viewMatrix = GetViewMatrix();
    view.position = m_position;
    view.fieldOfViewDegrees = fieldOfViewDegrees;
    view.nearPlane = nearPlane;
    view.farPlane = farPlane;
    return view;
}

CameraData EditorCamera::GetCameraData(float aspect) const
{
    CameraData camera;
    camera.viewMatrix = GetViewMatrix();
    camera.projectionMatrix = glm::perspective(glm::radians(fieldOfViewDegrees), aspect, nearPlane, farPlane);
    camera.position = m_position;
    camera.nearPlane = nearPlane;
    camera.farPlane = farPlane;
    return camera;
}

} // namespace Geni::Editor
