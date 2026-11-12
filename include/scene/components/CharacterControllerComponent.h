#pragma once

#include "physics/CharacterController.h"
#include "scene/Component.h"
#include <glm/ext/vector_double3.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>
#include <memory>
#include <string>

namespace Geni
{

// A walking character (docs/design/0010). With `playerInput` it reads the move_x/move_y,
// sprint and jump actions relative to the main camera; otherwise code, Lua or AI sets
// the wanted motion with SetMoveInput. The object's position is the character's feet.
class CharacterControllerComponent : public Component
{
    COMPONENT(CharacterControllerComponent)

  public:
    void Init() override;
    void Update(float deltaTime) override;
    void EditUpdate() override;
    void FixedUpdate(float fixedDeltaTime) override;
    bool HasFixedUpdate() const override
    {
        return true;
    }
    void OnDestroy() override;
    void OnOriginShift(const glm::vec3 &offset) override;

    // Wanted motion: a direction on the ground in scene space (length up to 1 = walking
    // speed, and up to runSpeed / walkSpeed when `sprint`). Used when playerInput is off.
    void SetMoveInput(const glm::vec3 &direction, bool sprint);
    // Jumps at the next step if standing.
    void Jump();
    // Moves the character there at once (feet position, scene space).
    void Teleport(const glm::vec3 &scenePosition);
    // Off: no collision and no movement (the character is in a car). Recreated on.
    void SetEnabled(bool enabled);
    bool IsEnabled() const
    {
        return m_enabled;
    }

    // Horizontal speed in m/s, for animation.
    float GetSpeed() const
    {
        return m_speed;
    }
    bool IsGrounded() const
    {
        return m_grounded;
    }
    glm::vec3 GetVelocity() const
    {
        return m_velocity;
    }
    CharacterController *GetController() const
    {
        return m_controller.get();
    }

    // Reflected properties.
    float GetRadius() const
    {
        return m_settings.radius;
    }
    void SetRadius(float value)
    {
        m_settings.radius = value;
    }
    float GetHeight() const
    {
        return m_settings.height;
    }
    void SetHeight(float value)
    {
        m_settings.height = value;
    }
    float GetStepHeight() const
    {
        return m_settings.stepHeight;
    }
    void SetStepHeight(float value)
    {
        m_settings.stepHeight = value;
    }
    float GetMaxSlope() const
    {
        return m_settings.maxSlopeDegrees;
    }
    void SetMaxSlope(float value)
    {
        m_settings.maxSlopeDegrees = value;
    }
    float GetWalkSpeed() const
    {
        return m_walkSpeed;
    }
    void SetWalkSpeed(float value)
    {
        m_walkSpeed = value;
    }
    float GetRunSpeed() const
    {
        return m_runSpeed;
    }
    void SetRunSpeed(float value)
    {
        m_runSpeed = value;
    }
    float GetJumpSpeed() const
    {
        return m_jumpSpeed;
    }
    void SetJumpSpeed(float value)
    {
        m_jumpSpeed = value;
    }
    float GetAcceleration() const
    {
        return m_acceleration;
    }
    void SetAcceleration(float value)
    {
        m_acceleration = value;
    }
    float GetTurnSpeed() const
    {
        return m_turnSpeed;
    }
    void SetTurnSpeed(float value)
    {
        m_turnSpeed = value;
    }
    bool GetPlayerInput() const
    {
        return m_playerInput;
    }
    void SetPlayerInput(bool value)
    {
        m_playerInput = value;
    }
    // An animator parameter to receive the speed each frame ("" for none).
    std::string GetSpeedParameter() const
    {
        return m_speedParameter;
    }
    void SetSpeedParameter(const std::string &value)
    {
        m_speedParameter = value;
    }

  private:
    void CreateController();
    glm::vec3 ReadPlayerInput(bool &sprint) const;

    CharacterSettings m_settings;
    std::unique_ptr<CharacterController> m_controller;
    float m_walkSpeed = 1.8f;
    float m_runSpeed = 5.5f;
    float m_jumpSpeed = 5.0f;
    float m_acceleration = 12.0f;
    float m_turnSpeed = 10.0f;
    bool m_playerInput = false;
    std::string m_speedParameter = "speed";
    bool m_enabled = true;

    glm::vec3 m_moveInput{0.0f};
    bool m_sprint = false;
    bool m_jumpRequested = false;
    glm::vec3 m_horizontalVelocity{0.0f};
    float m_speed = 0.0f;
    bool m_grounded = true;
    glm::vec3 m_velocity{0.0f};
    float m_yaw = 0.0f;
    // Simulated feet positions (world) at the last two steps, for interpolation.
    glm::dvec3 m_previous{0.0};
    glm::dvec3 m_current{0.0};
    bool m_hasStep = false;
};

} // namespace Geni
