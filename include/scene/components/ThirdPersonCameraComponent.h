#pragma once

#include "scene/Component.h"
#include <cstdint>
#include <string>

namespace Geni
{

class GameObject;

// Orbits a target object from behind (docs/design/0010). Reads look_x, look_y and zoom
// when `playerInput` is on; swings behind a moving vehicle after a moment without look
// input; sphere-casts against static geometry so walls never come between the camera
// and the target.
class ThirdPersonCameraComponent : public Component
{
    COMPONENT(ThirdPersonCameraComponent)

  public:
    void Update(float deltaTime) override
    {
        (void)deltaTime;
    }
    // After the target has moved this frame.
    void LateUpdate(float deltaTime) override;
    bool HasLateUpdate() const override
    {
        return true;
    }

    void SetTarget(GameObject *target);
    GameObject *GetTargetObject() const;
    // Points the camera behind the target's heading now (after a cut or a teleport).
    void SnapBehindTarget();

    std::string GetTargetName() const
    {
        return m_targetName;
    }
    void SetTargetName(const std::string &name);
    float GetDistance() const
    {
        return m_distance;
    }
    void SetDistance(float value);
    float GetMinDistance() const
    {
        return m_minDistance;
    }
    void SetMinDistance(float value)
    {
        m_minDistance = value;
    }
    float GetMaxDistance() const
    {
        return m_maxDistance;
    }
    void SetMaxDistance(float value)
    {
        m_maxDistance = value;
    }
    float GetHeight() const
    {
        return m_height;
    }
    void SetHeight(float value)
    {
        m_height = value;
    }
    float GetYaw() const
    {
        return m_yaw;
    }
    void SetYaw(float value)
    {
        m_yaw = value;
    }
    float GetPitch() const
    {
        return m_pitch;
    }
    void SetPitch(float value);
    float GetMinPitch() const
    {
        return m_minPitch;
    }
    void SetMinPitch(float value)
    {
        m_minPitch = value;
    }
    float GetMaxPitch() const
    {
        return m_maxPitch;
    }
    void SetMaxPitch(float value)
    {
        m_maxPitch = value;
    }
    float GetCollisionRadius() const
    {
        return m_collisionRadius;
    }
    void SetCollisionRadius(float value)
    {
        m_collisionRadius = value;
    }
    bool GetAutoFollow() const
    {
        return m_autoFollow;
    }
    void SetAutoFollow(bool value)
    {
        m_autoFollow = value;
    }
    bool GetPlayerInput() const
    {
        return m_playerInput;
    }
    void SetPlayerInput(bool value)
    {
        m_playerInput = value;
    }
    // The distance after collision, for tests.
    float GetCurrentDistance() const
    {
        return m_currentDistance;
    }

  private:
    std::string m_targetName;
    uint64_t m_targetID = 0;
    float m_distance = 4.5f;
    float m_minDistance = 1.5f;
    float m_maxDistance = 14.0f;
    float m_height = 1.6f;
    float m_yaw = 0.0f;
    float m_pitch = 15.0f;
    float m_minPitch = -40.0f;
    float m_maxPitch = 70.0f;
    float m_collisionRadius = 0.25f;
    bool m_autoFollow = true;
    bool m_playerInput = true;
    float m_currentDistance = -1.0f;
    float m_timeSinceLook = 10.0f;
};

} // namespace Geni
