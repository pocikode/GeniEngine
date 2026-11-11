#pragma once

#include <cstdint>
#include <glm/ext/vector_double3.hpp>
#include <glm/vec3.hpp>
#include <memory>

namespace Geni
{

class PhysicsManager;

struct CharacterSettings
{
    float radius = 0.35f;
    // Total height of the capsule, feet to top of the head.
    float height = 1.8f;
    float maxSlopeDegrees = 50.0f;
    // Kerbs and stairs up to this height are climbed without jumping.
    float stepHeight = 0.4f;
    float mass = 70.0f;
    // Largest force the character pushes dynamic bodies with (newtons).
    float maxStrength = 300.0f;
};

enum class CharacterGround
{
    OnGround,
    // On a slope too steep to stand on: slides down.
    OnSteepGround,
    // Touching something but not supported by it.
    NotSupported,
    InAir
};

// A capsule moved by collision queries (Jolt CharacterVirtual, docs/design/0010). Its
// position is the bottom of the capsule (the feet), in world coordinates. It has an
// inner body on the moving layer so other bodies collide with it, and it pushes dynamic
// bodies it walks into. Main thread only.
class CharacterController
{
  public:
    CharacterController(PhysicsManager &physics, const CharacterSettings &settings, const glm::dvec3 &feetPosition,
                        uint64_t userData = 0);
    ~CharacterController();
    CharacterController(const CharacterController &) = delete;
    CharacterController &operator=(const CharacterController &) = delete;

    // One step: `horizontalVelocity` is where the character wants to go (m/s, Y ignored).
    // While on the ground it moves at that velocity (plus the ground's); in the air it
    // keeps its momentum, steered by `airControl` (0..1). `jump` > 0 leaves the ground at
    // that upward speed. Climbs steps and sticks to the floor going down slopes.
    void Step(float deltaTime, const glm::vec3 &horizontalVelocity, float jump, float airControl = 0.3f,
              float gravityScale = 1.0f);

    glm::dvec3 GetPosition() const;
    // Moves the character without sweeping (spawning, getting out of a car).
    void Teleport(const glm::dvec3 &feetPosition);
    glm::vec3 GetVelocity() const;
    CharacterGround GetGround() const;
    bool IsGrounded() const
    {
        return GetGround() == CharacterGround::OnGround;
    }
    glm::vec3 GetGroundNormal() const;
    // The inner body other bodies collide with.
    uint32_t GetInnerBodyId() const;
    const CharacterSettings &GetSettings() const
    {
        return m_settings;
    }

  private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    PhysicsManager &m_physics;
    CharacterSettings m_settings;
};

} // namespace Geni
