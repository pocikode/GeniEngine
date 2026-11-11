#include "physics/PhysicsManager.h"

#include "core/Log.h"
#include "core/Profiler.h"
#include "physics/RigidBody.h"

// Jolt is private to src/physics/ (docs/design/0008). Jolt/Jolt.h must come first.
#include "JoltInternal.h"

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
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

namespace Layers = JoltLayers;

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
    // Kinematic bodies moved last step but given no new target have arrived: stop them,
    // or they would keep last step's velocity and overshoot.
    for (const JPH::BodyID &id : m_impl->kinematicMoved)
    {
        const bool retargeted = std::any_of(
            m_impl->kinematicTargets.begin(), m_impl->kinematicTargets.end(),
            [&](const Impl::KinematicTarget &target) { return target.body == id; }
        );
        if (!retargeted && m_impl->Bodies().IsAdded(id))
        {
            m_impl->Bodies().SetLinearAndAngularVelocity(id, JPH::Vec3::sZero(), JPH::Vec3::sZero());
        }
    }
    m_impl->kinematicMoved.clear();
    for (const Impl::KinematicTarget &target : m_impl->kinematicTargets)
    {
        if (m_impl->Bodies().IsAdded(target.body))
        {
            m_impl->Bodies().MoveKinematic(target.body, target.position, target.rotation, fixedDeltaTime);
            m_impl->kinematicMoved.push_back(target.body);
        }
    }
    m_impl->kinematicTargets.clear();
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
    settings.mUserData = body->GetUserData();
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

namespace
{

// Layer mask -> broad-phase and object-layer filters for queries.
class MaskBroadPhaseFilter final : public JPH::BroadPhaseLayerFilter
{
  public:
    explicit MaskBroadPhaseFilter(uint32_t mask) : m_mask(mask)
    {
    }
    bool ShouldCollide(JPH::BroadPhaseLayer layer) const override
    {
        return layer == JoltBroadPhaseLayers::Static ? (m_mask & PhysicsLayerStatic) != 0
                                                     : (m_mask & PhysicsLayerMoving) != 0;
    }

  private:
    uint32_t m_mask;
};

class MaskObjectLayerFilter final : public JPH::ObjectLayerFilter
{
  public:
    explicit MaskObjectLayerFilter(uint32_t mask) : m_mask(mask)
    {
    }
    bool ShouldCollide(JPH::ObjectLayer layer) const override
    {
        return layer == JoltLayers::Static ? (m_mask & PhysicsLayerStatic) != 0 : (m_mask & PhysicsLayerMoving) != 0;
    }

  private:
    uint32_t m_mask;
};

class IgnoreBodyFilter final : public JPH::BodyFilter
{
  public:
    explicit IgnoreBodyFilter(uint32_t ignored) : m_ignored(ignored)
    {
    }
    bool ShouldCollide(const JPH::BodyID &id) const override
    {
        return id.GetIndexAndSequenceNumber() != m_ignored;
    }

  private:
    uint32_t m_ignored;
};

void FillHit(JPH::PhysicsSystem &system, const JPH::BodyID &body, const JPH::SubShapeID &subShape,
             const JPH::RVec3 &point, const glm::vec3 &fallbackNormal, PhysicsHit &hit)
{
    hit.position = FromJolt(point);
    hit.bodyId = body.GetIndexAndSequenceNumber();
    hit.normal = fallbackNormal;
    JPH::BodyLockRead lock(system.GetBodyLockInterfaceNoLock(), body);
    if (lock.Succeeded())
    {
        const JPH::Body &locked = lock.GetBody();
        hit.userData = locked.GetUserData();
        const JPH::Vec3 n = locked.GetWorldSpaceSurfaceNormal(subShape, point);
        hit.normal = glm::vec3(n.GetX(), n.GetY(), n.GetZ());
    }
}

} // namespace

void PhysicsManager::MoveKinematic(uint32_t bodyId, const glm::dvec3 &position, const glm::quat &rotation, float deltaTime)
{
    if (m_impl && deltaTime > 0.0f)
    {
        m_impl->Bodies().MoveKinematic(JPH::BodyID(bodyId), ToJolt(position), ToJolt(rotation), deltaTime);
    }
}

void PhysicsManager::SetKinematicTarget(uint32_t bodyId, const glm::dvec3 &position, const glm::quat &rotation)
{
    if (!m_impl)
    {
        return;
    }
    const JPH::BodyID id(bodyId);
    for (Impl::KinematicTarget &target : m_impl->kinematicTargets)
    {
        if (target.body == id)
        {
            target.position = ToJolt(position);
            target.rotation = ToJolt(rotation);
            return;
        }
    }
    m_impl->kinematicTargets.push_back({id, ToJolt(position), ToJolt(rotation)});
}

glm::vec3 PhysicsManager::GetBodyLinearVelocity(uint32_t bodyId) const
{
    if (!m_impl)
    {
        return glm::vec3(0.0f);
    }
    const JPH::Vec3 v = m_impl->Bodies().GetLinearVelocity(JPH::BodyID(bodyId));
    return glm::vec3(v.GetX(), v.GetY(), v.GetZ());
}

void PhysicsManager::SetBodyLinearVelocity(uint32_t bodyId, const glm::vec3 &velocity)
{
    if (m_impl)
    {
        m_impl->Bodies().SetLinearVelocity(JPH::BodyID(bodyId), ToJolt(velocity));
    }
}

void PhysicsManager::AddBodyImpulse(uint32_t bodyId, const glm::vec3 &impulse)
{
    if (m_impl)
    {
        m_impl->Bodies().AddImpulse(JPH::BodyID(bodyId), ToJolt(impulse));
    }
}

glm::vec3 PhysicsManager::GetGravity() const
{
    if (!m_impl)
    {
        return glm::vec3(0.0f, -9.81f, 0.0f);
    }
    const JPH::Vec3 g = m_impl->system.GetGravity();
    return glm::vec3(g.GetX(), g.GetY(), g.GetZ());
}

bool PhysicsManager::CastRay(
    const glm::dvec3 &origin, const glm::vec3 &direction, float maxDistance, PhysicsHit &hit, uint32_t layers,
    uint32_t ignoreBody
) const
{
    const float length = glm::length(direction);
    if (!m_impl || length <= 0.0f || maxDistance <= 0.0f)
    {
        return false;
    }
    const glm::vec3 dir = direction / length;
    JPH::RRayCast ray(ToJolt(origin), ToJolt(dir * maxDistance));
    JPH::RayCastResult result;
    MaskBroadPhaseFilter broadPhase(layers);
    MaskObjectLayerFilter objectLayers(layers);
    IgnoreBodyFilter bodies(ignoreBody);
    if (!m_impl->system.GetNarrowPhaseQueryNoLock().CastRay(ray, result, broadPhase, objectLayers, bodies))
    {
        return false;
    }
    FillHit(m_impl->system, result.mBodyID, result.mSubShapeID2, ray.GetPointOnRay(result.mFraction), -dir, hit);
    hit.distance = result.mFraction * maxDistance;
    return true;
}

bool PhysicsManager::CastSphere(
    const glm::dvec3 &origin, float radius, const glm::vec3 &direction, float maxDistance, PhysicsHit &hit,
    uint32_t layers, uint32_t ignoreBody
) const
{
    const float length = glm::length(direction);
    if (!m_impl || length <= 0.0f || maxDistance <= 0.0f || radius <= 0.0f)
    {
        return false;
    }
    const glm::vec3 dir = direction / length;
    JPH::SphereShape sphere(radius);
    sphere.SetEmbedded();
    const JPH::RShapeCast cast(
        &sphere, JPH::Vec3::sOne(), JPH::RMat44::sTranslation(ToJolt(origin)), ToJolt(dir * maxDistance)
    );
    JPH::ShapeCastSettings settings;
    settings.mBackFaceModeTriangles = JPH::EBackFaceMode::IgnoreBackFaces;
    settings.mBackFaceModeConvex = JPH::EBackFaceMode::IgnoreBackFaces;
    JPH::ClosestHitCollisionCollector<JPH::CastShapeCollector> collector;
    MaskBroadPhaseFilter broadPhase(layers);
    MaskObjectLayerFilter objectLayers(layers);
    IgnoreBodyFilter bodies(ignoreBody);
    m_impl->system.GetNarrowPhaseQueryNoLock().CastShape(
        cast, settings, ToJolt(origin), collector, broadPhase, objectLayers, bodies
    );
    if (!collector.HadHit())
    {
        return false;
    }
    const JPH::ShapeCastResult &result = collector.mHit;
    const JPH::RVec3 contact = ToJolt(origin) + result.mContactPointOn2;
    FillHit(m_impl->system, result.mBodyID2, result.mSubShapeID2, contact, -dir, hit);
    // The penetration axis points from the sphere into the body: its reverse is the normal.
    const JPH::Vec3 axis = result.mPenetrationAxis;
    if (axis.LengthSq() > 0.0f)
    {
        const JPH::Vec3 n = -axis.Normalized();
        hit.normal = glm::vec3(n.GetX(), n.GetY(), n.GetZ());
    }
    hit.distance = result.mFraction * maxDistance;
    return true;
}

} // namespace Geni
