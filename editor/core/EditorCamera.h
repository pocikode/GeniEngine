#pragma once

#include "core/Common.h"
#include "core/Engine.h"

#include <glm/gtc/quaternion.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

namespace Geni::Editor
{

// What the viewport feeds the camera each frame. Deltas are in pixels.
struct CameraInput
{
    glm::vec2 mouseDelta{0.0f};
    float scroll = 0.0f;
    bool look = false;  // right button: look around, and fly with the move keys
    bool orbit = false; // alt + left button: orbit the pivot
    bool pan = false;   // middle button
    glm::vec3 move{0.0f}; // x right, y up, z forward; -1..1 each
    bool fast = false;
};

// The editor's own view of the scene: a free camera that can fly, orbit a pivot, pan and
// dolly. It is not a scene object, so looking around never edits the scene.
class EditorCamera
{
  public:
    void Update(const CameraInput &input, float deltaTime);
    // Moves the camera so a sphere fills most of the view, looking the way it looks now.
    void Focus(const glm::vec3 &center, float radius);

    glm::vec3 GetPosition() const
    {
        return m_position;
    }
    void SetPosition(const glm::vec3 &position)
    {
        m_position = position;
    }
    // Degrees. Yaw 0 looks down -Z; positive yaw turns left. Pitch is clamped short of the poles.
    void SetAngles(float yawDegrees, float pitchDegrees);
    float GetYaw() const
    {
        return m_yaw;
    }
    float GetPitch() const
    {
        return m_pitch;
    }
    glm::vec3 GetPivot() const
    {
        return m_position + GetForward() * m_pivotDistance;
    }

    glm::quat GetRotation() const;
    glm::vec3 GetForward() const;
    glm::vec3 GetRight() const;
    glm::vec3 GetUp() const;
    glm::mat4 GetViewMatrix() const;

    float fieldOfViewDegrees = 60.0f;
    float nearPlane = 0.1f;
    float farPlane = 2000.0f;
    float moveSpeed = 12.0f;

    ViewOverride GetViewOverride() const;
    // The same camera as the renderer will use it, for picking and overlays.
    CameraData GetCameraData(float aspect) const;

  private:
    glm::vec3 m_position{0.0f, 4.0f, 12.0f};
    float m_yaw = 0.0f;
    float m_pitch = -15.0f;
    // How far in front of the camera the point it orbits and dollies towards is.
    float m_pivotDistance = 12.0f;
};

} // namespace Geni::Editor
