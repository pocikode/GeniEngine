#pragma once

// Jolt internals shared by the files in src/physics/ (docs/design/0008, 0010). Nothing
// outside src/physics/ includes this: Jolt types never appear in public headers.

#include "physics/PhysicsManager.h"

#include <Jolt/Jolt.h>

#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/PhysicsSystem.h>

#include <glm/ext/quaternion_float.hpp>
#include <glm/ext/vector_double3.hpp>
#include <glm/ext/vector_float3.hpp>
#include <memory>
#include <vector>

namespace Geni
{

namespace JoltLayers
{
// Object layers: static bodies never test against each other.
constexpr JPH::ObjectLayer Static = 0;
constexpr JPH::ObjectLayer Moving = 1;
constexpr uint32_t Count = 2;
} // namespace JoltLayers

namespace JoltBroadPhaseLayers
{
const JPH::BroadPhaseLayer Static(0);
const JPH::BroadPhaseLayer Moving(1);
constexpr uint32_t Count = 2;
} // namespace JoltBroadPhaseLayers

class JoltBroadPhaseLayerMap final : public JPH::BroadPhaseLayerInterface
{
  public:
    JPH::uint GetNumBroadPhaseLayers() const override
    {
        return JoltBroadPhaseLayers::Count;
    }
    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override
    {
        return layer == JoltLayers::Static ? JoltBroadPhaseLayers::Static : JoltBroadPhaseLayers::Moving;
    }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char *GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override
    {
        return layer == JoltBroadPhaseLayers::Static ? "static" : "moving";
    }
#endif
};

class JoltObjectVsBroadPhaseFilter final : public JPH::ObjectVsBroadPhaseLayerFilter
{
  public:
    bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer broadPhase) const override
    {
        return layer == JoltLayers::Moving || broadPhase == JoltBroadPhaseLayers::Moving;
    }
};

class JoltObjectPairFilter final : public JPH::ObjectLayerPairFilter
{
  public:
    bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override
    {
        return a == JoltLayers::Moving || b == JoltLayers::Moving;
    }
};

inline JPH::RVec3 ToJolt(const glm::dvec3 &v)
{
    return JPH::RVec3(v.x, v.y, v.z);
}

inline JPH::Vec3 ToJolt(const glm::vec3 &v)
{
    return JPH::Vec3(v.x, v.y, v.z);
}

inline JPH::Quat ToJolt(const glm::quat &q)
{
    return JPH::Quat(q.x, q.y, q.z, q.w).Normalized();
}

inline glm::dvec3 FromJolt(JPH::RVec3Arg v)
{
    return glm::dvec3(v.GetX(), v.GetY(), v.GetZ());
}

#if defined(JPH_DOUBLE_PRECISION)
inline glm::vec3 FromJoltFloat(JPH::Vec3Arg v)
{
    return glm::vec3(v.GetX(), v.GetY(), v.GetZ());
}
#endif

inline glm::quat FromJolt(JPH::QuatArg q)
{
    return glm::quat(q.GetW(), q.GetX(), q.GetY(), q.GetZ());
}

struct PhysicsManager::Impl
{
    JoltBroadPhaseLayerMap broadPhaseLayers;
    JoltObjectVsBroadPhaseFilter objectVsBroadPhase;
    JoltObjectPairFilter objectPairs;
    std::unique_ptr<JPH::TempAllocatorImpl> tempAllocator;
    std::unique_ptr<JPH::JobSystemThreadPool> jobs;
    JPH::PhysicsSystem system;
    std::vector<JPH::BodyID> pendingAdds;
    std::vector<JPH::BodyID> pendingRemoves;
    struct KinematicTarget
    {
        JPH::BodyID body;
        JPH::RVec3 position;
        JPH::Quat rotation;
    };
    std::vector<KinematicTarget> kinematicTargets;
    std::vector<JPH::BodyID> kinematicMoved;

    JPH::BodyInterface &Bodies()
    {
        return system.GetBodyInterfaceNoLock();
    }
};

} // namespace Geni
