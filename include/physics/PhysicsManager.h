#pragma once

#include <memory>

class btBroadphaseInterface;
class btDefaultCollisionConfiguration;
class btCollisionDispatcher;
class btSequentialImpulseConstraintSolver;
class btDiscreteDynamicsWorld;

namespace Geni
{

class RigidBody;

class PhysicsManager
{
  public:
    PhysicsManager();
    ~PhysicsManager();

    void Init();
    void Shutdown();
    // Advances the world by exactly one step of fixedDeltaTime. Engine::Simulate calls
    // this from the fixed-timestep loop.
    void Step(float fixedDeltaTime);
    // Variable-rate update with Bullet's internal 1/60 s substepping. Not used by the
    // engine loop any more; kept for code that drives physics itself.
    void Update(float deltaTime);

    void AddRigidBody(RigidBody *body);
    void RemoveRigidBody(RigidBody *body);

    btDiscreteDynamicsWorld *GetWorld();

  private:
    std::unique_ptr<btBroadphaseInterface> m_broadphase;
    std::unique_ptr<btDefaultCollisionConfiguration> m_collisionConfig;
    std::unique_ptr<btCollisionDispatcher> m_dispatcher;
    std::unique_ptr<btSequentialImpulseConstraintSolver> m_solver;
    std::unique_ptr<btDiscreteDynamicsWorld> m_world;
};
} // namespace Geni