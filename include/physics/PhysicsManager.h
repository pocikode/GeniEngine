#pragma once

#include "glm/ext/quaternion_float.hpp"
#include "glm/ext/vector_double3.hpp"
#include <cstddef>
#include <cstdint>
#include <memory>

namespace Geni
{

class RigidBody;

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

  private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace Geni
