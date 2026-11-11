#include "physics/PhysicsManager.h"

#include "core/Log.h"
#include "core/Profiler.h"
#include "physics/RigidBody.h"

// Jolt is private to src/physics/ (docs/design/0008). Jolt/Jolt.h must come first.
#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <thread>
#include <vector>

namespace Geni
{

namespace
{

// Object layers: static bodies never test against each other.
namespace Layers
{
constexpr JPH::ObjectLayer Static = 0;
constexpr JPH::ObjectLayer Moving = 1;
constexpr uint32_t Count = 2;
} // namespace Layers

namespace BroadPhaseLayers
{
const JPH::BroadPhaseLayer Static(0);
const JPH::BroadPhaseLayer Moving(1);
constexpr uint32_t Count = 2;
} // namespace BroadPhaseLayers

class BroadPhaseLayerMap final : public JPH::BroadPhaseLayerInterface
{
  public:
    JPH::uint GetNumBroadPhaseLayers() const override
    {
        return BroadPhaseLayers::Count;
    }
    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override
    {
        return layer == Layers::Static ? BroadPhaseLayers::Static : BroadPhaseLayers::Moving;
    }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char *GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override
    {
        return layer == BroadPhaseLayers::Static ? "static" : "moving";
    }
#endif
};

class ObjectVsBroadPhaseFilter final : public JPH::ObjectVsBroadPhaseLayerFilter
{
  public:
    bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer broadPhase) const override
    {
        return layer == Layers::Moving || broadPhase == BroadPhaseLayers::Moving;
    }
};

class ObjectPairFilter final : public JPH::ObjectLayerPairFilter
{
  public:
    bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override
    {
        return a == Layers::Moving || b == Layers::Moving;
    }
};

void JoltTrace(const char *format, ...)
{
    char buffer[1024];
    va_list args;
    va_start(args, format);
    std::vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    GENI_LOG_DEBUG("Jolt: %s", buffer);
}

#if defined(JPH_ENABLE_ASSERTS)
bool JoltAssertFailed(const char *expression, const char *message, const char *file, JPH::uint line)
{
    GENI_LOG_ERROR("Jolt assertion %s failed at %s:%u: %s", expression, file, line, message ? message : "");
    return false; // do not break into the debugger
}
#endif

// Jolt's allocator, factory and type registry are process-wide.
std::mutex s_globalMutex;
int s_globalUsers = 0;

void AcquireJolt()
{
    std::lock_guard<std::mutex> lock(s_globalMutex);
    if (s_globalUsers++ == 0)
    {
        JPH::RegisterDefaultAllocator();
        JPH::Trace = JoltTrace;
#if defined(JPH_ENABLE_ASSERTS)
        JPH::AssertFailed = JoltAssertFailed;
#endif
        JPH::Factory::sInstance = new JPH::Factory();
        JPH::RegisterTypes();
    }
}

void ReleaseJolt()
{
    std::lock_guard<std::mutex> lock(s_globalMutex);
    if (--s_globalUsers == 0)
    {
        JPH::UnregisterTypes();
        delete JPH::Factory::sInstance;
        JPH::Factory::sInstance = nullptr;
    }
}

JPH::RVec3 ToJolt(const glm::dvec3 &v)
{
    return JPH::RVec3(v.x, v.y, v.z);
}

JPH::Quat ToJolt(const glm::quat &q)
{
    return JPH::Quat(q.x, q.y, q.z, q.w).Normalized();
}

JPH::ShapeRefC MakeShape(const Collider &collider, const glm::vec3 &scale)
{
    const glm::vec3 s = glm::abs(scale);
    switch (collider.GetType())
    {
    case ColliderType::Box: {
        const glm::vec3 half = glm::max(collider.GetExtents() * s * 0.5f, glm::vec3(0.001f));
        // The convex radius must not exceed the smallest half extent.
        const float radius = std::min(JPH::cDefaultConvexRadius, 0.5f * std::min({half.x, half.y, half.z}));
        return new JPH::BoxShape(JPH::Vec3(half.x, half.y, half.z), radius);
    }
    case ColliderType::Sphere:
        return new JPH::SphereShape(std::max(collider.GetRadius() * std::max({s.x, s.y, s.z}), 0.001f));
    case ColliderType::Capsule: {
        const float radius = std::max(collider.GetRadius() * std::max(s.x, s.z), 0.001f);
        const float halfHeight = std::max(0.5f * collider.GetHeight() * s.y, 0.001f);
        return new JPH::CapsuleShape(halfHeight, radius);
    }
    }
    return new JPH::SphereShape(0.5f);
}

} // namespace

struct PhysicsManager::Impl
{
    BroadPhaseLayerMap broadPhaseLayers;
    ObjectVsBroadPhaseFilter objectVsBroadPhase;
    ObjectPairFilter objectPairs;
    std::unique_ptr<JPH::TempAllocatorImpl> tempAllocator;
    std::unique_ptr<JPH::JobSystemThreadPool> jobs;
    JPH::PhysicsSystem system;
    std::vector<JPH::BodyID> pendingAdds;
    std::vector<JPH::BodyID> pendingRemoves;

    JPH::BodyInterface &Bodies()
    {
        return system.GetBodyInterfaceNoLock();
    }
};

PhysicsManager::PhysicsManager() = default;

PhysicsManager::~PhysicsManager()
{
    Shutdown();
}

void PhysicsManager::Init()
{
    if (m_impl)
    {
        return;
    }
    AcquireJolt();
    m_impl = std::make_unique<Impl>();

    constexpr JPH::uint maxBodies = 131072;
    constexpr JPH::uint maxBodyPairs = 65536;
    constexpr JPH::uint maxContactConstraints = 10240;
    m_impl->system.Init(
        maxBodies, 0, maxBodyPairs, maxContactConstraints, m_impl->broadPhaseLayers, m_impl->objectVsBroadPhase,
        m_impl->objectPairs
    );
    m_impl->system.SetGravity(JPH::Vec3(0.0f, -9.81f, 0.0f));
    // Each step reserves its contact buffer from here (about 1 KiB per contact constraint).
    m_impl->tempAllocator = std::make_unique<JPH::TempAllocatorImpl>(32 * 1024 * 1024);
    // A few threads: the engine's own job system idles while physics steps.
    const int threads = std::max(1, static_cast<int>(std::thread::hardware_concurrency()) / 4);
    m_impl->jobs = std::make_unique<JPH::JobSystemThreadPool>(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, threads);
}

void PhysicsManager::Shutdown()
{
    if (!m_impl)
    {
        return;
    }
    FlushPendingBodies();
    // Bodies whose RigidBody outlived the world: remove and destroy them with it.
    JPH::BodyIDVector bodies;
    m_impl->system.GetBodies(bodies);
    if (!bodies.empty())
    {
        auto &interface = m_impl->Bodies();
        for (const JPH::BodyID &id : bodies)
        {
            if (interface.IsAdded(id))
            {
                interface.RemoveBody(id);
            }
        }
        interface.DestroyBodies(bodies.data(), static_cast<int>(bodies.size()));
    }
    m_impl.reset();
    ReleaseJolt();
}

void PhysicsManager::FlushPendingBodies()
{
    if (!m_impl)
    {
        return;
    }
    auto &interface = m_impl->Bodies();
    if (!m_impl->pendingRemoves.empty())
    {
        auto &ids = m_impl->pendingRemoves;
        interface.RemoveBodies(ids.data(), static_cast<int>(ids.size()));
        interface.DestroyBodies(ids.data(), static_cast<int>(ids.size()));
        ids.clear();
    }
    if (!m_impl->pendingAdds.empty())
    {
        auto &ids = m_impl->pendingAdds;
        // One broad-phase insert for the whole batch (a streamed cell's buildings).
        JPH::BodyInterface::AddState state = interface.AddBodiesPrepare(ids.data(), static_cast<int>(ids.size()));
        interface.AddBodiesFinalize(ids.data(), static_cast<int>(ids.size()), state, JPH::EActivation::Activate);
        ids.clear();
    }
}

void PhysicsManager::Step(float fixedDeltaTime)
{
    if (!m_impl)
    {
        return;
    }
    GENI_PROFILE_SCOPE("PhysicsManager::Step");
    FlushPendingBodies();
    m_impl->system.Update(fixedDeltaTime, 1, m_impl->tempAllocator.get(), m_impl->jobs.get());
}

void PhysicsManager::Update(float deltaTime)
{
    if (!m_impl)
    {
        return;
    }
    const float step = 1.0f / 60.0f;
    const int steps = std::clamp(static_cast<int>(deltaTime / step + 0.5f), 1, 4);
    for (int i = 0; i < steps; ++i)
    {
        Step(step);
    }
}

void PhysicsManager::AddRigidBody(RigidBody *body)
{
    if (!body || !m_impl || body->IsAddedToWorld() || !body->GetCollider())
    {
        return;
    }
    const bool moving = body->GetType() != BodyType::Static;
    JPH::BodyCreationSettings settings(
        MakeShape(*body->GetCollider(), body->GetScale()), ToJolt(body->m_position), ToJolt(body->m_rotation),
        body->GetType() == BodyType::Dynamic     ? JPH::EMotionType::Dynamic
        : body->GetType() == BodyType::Kinematic ? JPH::EMotionType::Kinematic
                                                 : JPH::EMotionType::Static,
        moving ? Layers::Moving : Layers::Static
    );
    settings.mFriction = body->GetFriction();
    if (body->GetType() == BodyType::Dynamic && body->GetMass() > 0.0f)
    {
        settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
        settings.mMassPropertiesOverride.mMass = body->GetMass();
    }
    JPH::Body *created = m_impl->Bodies().CreateBody(settings);
    if (!created)
    {
        GENI_LOG_ERROR("Physics: too many bodies (the world holds %u)", m_impl->system.GetNumBodies());
        return;
    }
    body->m_bodyId = created->GetID().GetIndexAndSequenceNumber();
    m_impl->pendingAdds.push_back(created->GetID());
}

void PhysicsManager::RemoveRigidBody(RigidBody *body)
{
    if (!body || !body->IsAddedToWorld())
    {
        return;
    }
    const JPH::BodyID id(body->m_bodyId);
    body->m_bodyId = RigidBody::InvalidBodyId;
    if (!m_impl)
    {
        return;
    }
    auto &pending = m_impl->pendingAdds;
    const auto it = std::find(pending.begin(), pending.end(), id);
    if (it != pending.end())
    {
        // Never entered the world: destroy it now.
        pending.erase(it);
        m_impl->Bodies().DestroyBody(id);
        return;
    }
    m_impl->pendingRemoves.push_back(id);
}

uint32_t PhysicsManager::GetBodyCount() const
{
    return m_impl ? m_impl->system.GetNumBodies() : 0;
}

size_t PhysicsManager::GetPendingAddCount() const
{
    return m_impl ? m_impl->pendingAdds.size() : 0;
}

void PhysicsManager::SetBodyTransform(uint32_t bodyId, const glm::dvec3 &position, const glm::quat &rotation)
{
    if (m_impl)
    {
        m_impl->Bodies().SetPositionAndRotation(
            JPH::BodyID(bodyId), ToJolt(position), ToJolt(rotation), JPH::EActivation::Activate
        );
    }
}

glm::dvec3 PhysicsManager::GetBodyPosition(uint32_t bodyId) const
{
    if (!m_impl)
    {
        return glm::dvec3(0.0);
    }
    const JPH::RVec3 p = m_impl->Bodies().GetPosition(JPH::BodyID(bodyId));
    return glm::dvec3(p.GetX(), p.GetY(), p.GetZ());
}

glm::quat PhysicsManager::GetBodyRotation(uint32_t bodyId) const
{
    if (!m_impl)
    {
        return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    }
    const JPH::Quat q = m_impl->Bodies().GetRotation(JPH::BodyID(bodyId));
    return glm::quat(q.GetW(), q.GetX(), q.GetY(), q.GetZ());
}

} // namespace Geni
