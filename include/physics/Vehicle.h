#pragma once

#include <cstdint>
#include <glm/ext/quaternion_float.hpp>
#include <glm/ext/vector_double3.hpp>
#include <glm/vec3.hpp>
#include <memory>

namespace Geni
{

class PhysicsManager;

// A four-wheeled car (docs/design/0010). Body space: forward is -Z, up +Y, right +X,
// like every engine object. Wheels are 0 front-left, 1 front-right, 2 rear-left,
// 3 rear-right.
struct VehicleSettings
{
    // Half size of the chassis box.
    glm::vec3 halfExtents{0.9f, 0.35f, 2.1f};
    // Moves the centre of mass down from the box's centre (stability).
    float centerOfMassDrop = 0.4f;
    float mass = 1300.0f;
    float maxEngineTorque = 340.0f;
    float minRpm = 900.0f;
    float maxRpm = 6500.0f;
    bool fourWheelDrive = false;
    float wheelRadius = 0.36f;
    float wheelWidth = 0.25f;
    // Wheel centres: half the track (x), the axle's distance from the centre (z) and
    // height relative to the box centre (y).
    float halfTrack = 0.85f;
    float halfWheelbase = 1.35f;
    float wheelHeight = -0.15f;
    float suspensionMin = 0.05f;
    float suspensionMax = 0.3f;
    float suspensionFrequency = 1.6f;
    float suspensionDamping = 0.55f;
    float maxSteerDegrees = 32.0f;
    float maxBrakeTorque = 1800.0f;
    float maxHandBrakeTorque = 4000.0f;
    // Keeps the car from rolling over: the up axis stays within this cone (degrees).
    float maxPitchRollDegrees = 60.0f;
};

// Jolt's VehicleConstraint with a WheeledVehicleController: engine, automatic gearbox,
// differential, anti-roll bars and cylinder-cast suspension. Main thread only.
class Vehicle
{
  public:
    Vehicle(PhysicsManager &physics, const VehicleSettings &settings, const glm::dvec3 &position,
            const glm::quat &rotation, uint64_t userData = 0);
    ~Vehicle();
    Vehicle(const Vehicle &) = delete;
    Vehicle &operator=(const Vehicle &) = delete;

    // forward -1..1 (negative reverses), right -1..1, brake 0..1, handBrake 0..1.
    void SetInput(float forward, float right, float brake, float handBrake);

    glm::dvec3 GetPosition() const;
    glm::quat GetRotation() const;
    glm::vec3 GetLinearVelocity() const;
    // Speed along the car's forward axis, m/s (negative when reversing).
    float GetForwardSpeed() const;
    float GetRpm() const;
    int GetGear() const;
    // The wheel's transform relative to the chassis, for a wheel mesh whose axle is its
    // local X axis.
    void GetWheelLocalTransform(int wheel, glm::vec3 &position, glm::quat &rotation) const;
    bool IsWheelOnGround(int wheel) const;
    int GetWheelCount() const
    {
        return 4;
    }
    // Places the car and stops it.
    void Teleport(const glm::dvec3 &position, const glm::quat &rotation);
    uint32_t GetBodyId() const;
    const VehicleSettings &GetSettings() const
    {
        return m_settings;
    }

  private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    PhysicsManager &m_physics;
    VehicleSettings m_settings;
};

} // namespace Geni
