#include "physics/PhysicsManager.h"
#include "BulletCollision/BroadphaseCollision/btBroadphaseProxy.h"
#include "BulletCollision/BroadphaseCollision/btDbvtBroadphase.h"
#include "BulletCollision/CollisionDispatch/btCollisionDispatcher.h"
#include "BulletCollision/CollisionDispatch/btDefaultCollisionConfiguration.h"
#include "BulletDynamics/ConstraintSolver/btSequentialImpulseConstraintSolver.h"
#include "BulletDynamics/Dynamics/btDiscreteDynamicsWorld.h"
#include "LinearMath/btVector3.h"
#include "physics/RigidBody.h"
#include <memory>

namespace Geni
{

PhysicsManager::PhysicsManager()
{
}

PhysicsManager::~PhysicsManager()
{
}

void PhysicsManager::Init()
{
    m_broadphase = std::make_unique<btDbvtBroadphase>();
    m_collisionConfig = std::make_unique<btDefaultCollisionConfiguration>();
    m_dispatcher = std::make_unique<btCollisionDispatcher>(m_collisionConfig.get());
    m_solver = std::make_unique<btSequentialImpulseConstraintSolver>();
    m_world = std::make_unique<btDiscreteDynamicsWorld>(
        m_dispatcher.get(), m_broadphase.get(), m_solver.get(), m_collisionConfig.get()
    );

    m_world->setGravity(btVector3(0, -9.81f, 0));
}

void PhysicsManager::Update(float deltaTime)
{
    const btScalar fixedTimesStep = 1.0f / 60.0f;
    const int maxSubSteps = 4;
    m_world->stepSimulation(deltaTime, maxSubSteps, fixedTimesStep);
}

void PhysicsManager::AddRigidBody(RigidBody *body)
{
    if (!body || !m_world)
    {
        return;
    }

    if (auto rigidBody = body->GetBody())
    {
        m_world->addRigidBody(rigidBody, btBroadphaseProxy::StaticFilter, btBroadphaseProxy::AllFilter);
        body->SetAddedToWorld(true);
    }
}

void PhysicsManager::RemoveRigidBody(RigidBody *body)
{
    if (!body || !m_world)
    {
        return;
    }

    if (auto rigidBody = body->GetBody())
    {
        m_world->removeRigidBody(rigidBody);
        body->SetAddedToWorld(false);
    }
}

btDiscreteDynamicsWorld *PhysicsManager::GetWorld()
{
    return m_world.get();
}

} // namespace Geni