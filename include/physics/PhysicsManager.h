#pragma once

#include "glm/ext/quaternion_float.hpp"
#include "glm/ext/vector_double3.hpp"
#include "glm/ext/vector_float3.hpp"
#include <cstddef>
#include <cstdint>
#include <memory>

namespace Geni
{

class RigidBody;

// Which layers a query tests (docs/design/0010).
enum PhysicsLayerMask : uint32_t
{
    PhysicsLayerStatic = 1u << 0,
    PhysicsLayerMoving = 1u << 1,
    PhysicsLayerAll = PhysicsLayerStatic | PhysicsLayerMoving
};

struct PhysicsHit
{
    glm::dvec3 position{0.0};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    float distance = 0.0f;
    uint32_t bodyId = 0xffffffffu;
    // What the body was created with (RigidBody::SetUserData): PhysicsComponent stores
    // its object's ID here.
    uint64_t userData = 0;
};

// The physics world, on Jolt Physics in double precision (docs/design/0008). Bodies are
// created when added but enter the broad phase in batches at the next Step, and removed
// bodies leave in batches, so a streamed cell's collision costs one insert and one removal.
class PhysicsManager
{
  public:
    PhysicsManager();
    ~PhysicsManager();
    PhysicsManager(const PhysicsManager &) = delete;
    PhysicsManager &operator=(const PhysicsManager &) = delete;

    void Init();
    void Shutdown();
    bool IsInitialized() const
    {
        return m_impl != nullptr;
    }
    // Advances the world by exactly one step of fixedDeltaTime. Engine::Simulate calls
    // this from the fixed-timestep loop.
    void Step(float fixedDeltaTime);
    // Variable-rate update in 1/60 s steps (at most four). Not used by the engine loop;
    // kept for code that drives physics itself.
    void Update(float deltaTime);

    void AddRigidBody(RigidBody *body);
    void RemoveRigidBody(RigidBody *body);
    // Puts queued additions into the world now (Step does this first).
    void FlushPendingBodies();

    // Bodies in the world, and those waiting for the next Step to be added.
    uint32_t GetBodyCount() const;
    size_t GetPendingAddCount() const;

    // Used by RigidBody once its body exists.
    void SetBodyTransform(uint32_t bodyId, const glm::dvec3 &position, const glm::quat &rotation);
    glm::dvec3 GetBodyPosition(uint32_t bodyId) const;
    glm::quat GetBodyRotation(uint32_t bodyId) const;

    // Kinematic bodies: moves the body to the transform over `deltaTime`, giving it the
    // velocity that gets there, so it pushes what it touches.
    void MoveKinematic(uint32_t bodyId, const glm::dvec3 &position, const glm::quat &rotation, float deltaTime);
    // The same, for the next Step (called from per-frame code): the body moves there over
    // the first step of the next frame's simulation.
    void SetKinematicTarget(uint32_t bodyId, const glm::dvec3 &position, const glm::quat &rotation);
    glm::vec3 GetBodyLinearVelocity(uint32_t bodyId) const;
    void SetBodyLinearVelocity(uint32_t bodyId, const glm::vec3 &velocity);
    void AddBodyImpulse(uint32_t bodyId, const glm::vec3 &impulse);
    glm::vec3 GetGravity() const;

    // Closest hit along a ray (direction need not be normalized; maxDistance is in metres
    // along it). `ignoreBody` is skipped (the caster's own body).
    bool CastRay(
        const glm::dvec3 &origin, const glm::vec3 &direction, float maxDistance, PhysicsHit &hit,
        uint32_t layers = PhysicsLayerAll, uint32_t ignoreBody = 0xffffffffu
    ) const;
    // Closest hit of a sphere swept along a ray: hit.distance is how far the sphere's
    // centre travelled.
    bool CastSphere(
        const glm::dvec3 &origin, float radius, const glm::vec3 &direction, float maxDistance, PhysicsHit &hit,
        uint32_t layers = PhysicsLayerAll, uint32_t ignoreBody = 0xffffffffu
    ) const;

    // For src/physics/ only (CharacterController, Vehicle): the Jolt world.
    struct Impl;
    Impl *GetImpl() const
    {
        return m_impl.get();
    }

  private:
    std::unique_ptr<Impl> m_impl;
};

} // namespace Geni
