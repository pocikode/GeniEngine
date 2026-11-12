#pragma once

#include "physics/Vehicle.h"
#include "scene/Component.h"
#include <glm/ext/vector_double3.hpp>
#include <glm/gtc/quaternion.hpp>
#include <memory>

namespace Geni
{

// A drivable car (docs/design/0010). The object is the chassis; child objects named
// Wheel0..Wheel3 (front-left, front-right, rear-left, rear-right) are placed at the
// simulated wheels each frame. With `playerInput` it reads throttle, brake, steer and
// handbrake; otherwise code or Lua drives it with SetDriverInput.
class VehicleComponent : public Component
{
    COMPONENT(VehicleComponent)

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

    // forward -1..1, steer -1..1 (positive right), brake and handBrake 0..1.
    void SetDriverInput(float forward, float steer, float brake, float handBrake);
    // Places the car (scene space) and stops it.
    void Teleport(const glm::vec3 &scenePosition, const glm::quat &rotation);

    // Speed along the car's heading in m/s (negative in reverse).
    float GetSpeed() const
    {
        return m_speed;
    }
    float GetRpm() const
    {
        return m_rpm;
    }
    int GetGear() const
    {
        return m_gear;
    }
    Vehicle *GetVehicle() const
    {
        return m_vehicle.get();
    }

    // Reflected properties.
    float GetMass() const
    {
        return m_settings.mass;
    }
    void SetMass(float value)
    {
        m_settings.mass = value;
    }
    glm::vec3 GetHalfExtents() const
    {
        return m_settings.halfExtents;
    }
    void SetHalfExtents(const glm::vec3 &value)
    {
        m_settings.halfExtents = value;
    }
    float GetEngineTorque() const
    {
        return m_settings.maxEngineTorque;
    }
    void SetEngineTorque(float value)
    {
        m_settings.maxEngineTorque = value;
    }
    float GetMaxRpm() const
    {
        return m_settings.maxRpm;
    }
    void SetMaxRpm(float value)
    {
        m_settings.maxRpm = value;
    }
    float GetWheelRadius() const
    {
        return m_settings.wheelRadius;
    }
    void SetWheelRadius(float value)
    {
        m_settings.wheelRadius = value;
    }
    float GetWheelWidth() const
    {
        return m_settings.wheelWidth;
    }
    void SetWheelWidth(float value)
    {
        m_settings.wheelWidth = value;
    }
    float GetHalfTrack() const
    {
        return m_settings.halfTrack;
    }
    void SetHalfTrack(float value)
    {
        m_settings.halfTrack = value;
    }
    float GetHalfWheelbase() const
    {
        return m_settings.halfWheelbase;
    }
    void SetHalfWheelbase(float value)
    {
        m_settings.halfWheelbase = value;
    }
    float GetWheelHeight() const
    {
        return m_settings.wheelHeight;
    }
    void SetWheelHeight(float value)
    {
        m_settings.wheelHeight = value;
    }
    float GetSuspensionTravel() const
    {
        return m_settings.suspensionMax;
    }
    void SetSuspensionTravel(float value)
    {
        m_settings.suspensionMax = value;
    }
    float GetMaxSteer() const
    {
        return m_settings.maxSteerDegrees;
    }
    void SetMaxSteer(float value)
    {
        m_settings.maxSteerDegrees = value;
    }
    bool GetFourWheelDrive() const
    {
        return m_settings.fourWheelDrive;
    }
    void SetFourWheelDrive(bool value)
    {
        m_settings.fourWheelDrive = value;
    }
    bool GetPlayerInput() const
    {
        return m_playerInput;
    }
    void SetPlayerInput(bool value)
    {
        m_playerInput = value;
    }

  private:
    void CreateVehicle();
    void PlaceWheels();

    VehicleSettings m_settings;
    std::unique_ptr<Vehicle> m_vehicle;
    bool m_playerInput = false;
    float m_forward = 0.0f;
    float m_steer = 0.0f;
    float m_brake = 0.0f;
    float m_handBrake = 0.0f;
    // Steering is eased in, as a driver turns the wheel.
    float m_smoothedSteer = 0.0f;
    float m_speed = 0.0f;
    float m_rpm = 0.0f;
    int m_gear = 0;
    glm::dvec3 m_previousPosition{0.0};
    glm::dvec3 m_currentPosition{0.0};
    glm::quat m_previousRotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::quat m_currentRotation{1.0f, 0.0f, 0.0f, 0.0f};
    bool m_hasStep = false;
};

} // namespace Geni
