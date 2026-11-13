#include "nav/NavigationSystem.h"

#include "assets/AssetManager.h"
#include "assets/CookedFormats.h"
#include "core/JobSystem.h"
#include "core/Log.h"
#include "core/Profiler.h"
#include "physics/RigidBody.h"
#include "scene/GameObject.h"
#include "scene/Scene.h"
#include "scene/components/PhysicsComponent.h"
#include "world/WorldFormat.h"
#include "world/WorldStreamer.h"

#include <DetourAlloc.h>
#include <DetourCrowd.h>
#include <DetourNavMesh.h>
#include <DetourNavMeshBuilder.h>
#include <DetourNavMeshQuery.h>
#include <Recast.h>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <map>
#include <mutex>
#include <random>

namespace Geni
{

namespace
{

constexpr int kMaxPathPolys = 512;
constexpr int kMaxStraightPath = 128;

struct TileKey
{
    int x;
    int z;
    bool operator<(const TileKey &other) const
    {
        return x != other.x ? x < other.x : z < other.z;
    }
};

} // namespace

void NavGeometry::AddColliders(GameObject &root)
{
    Scene *scene = root.GetScene();
    const glm::dvec3 origin = scene ? scene->GetOrigin() : glm::dvec3(0.0);
    auto visit = [&](auto &self, GameObject &object) -> void {
        if (auto *physics = object.GetComponent<PhysicsComponent>())
        {
            if (physics->GetBodyType() == BodyType::Static)
            {
                glm::mat4 world = object.GetWorldTransform();
                world[3] = glm::vec4(glm::vec3(glm::dvec3(world[3]) + origin), 1.0f);
                glm::vec3 size(1.0f);
                switch (physics->GetColliderType())
                {
                case ColliderType::Box:
                    size = physics->GetSize();
                    break;
                case ColliderType::Sphere:
                    size = glm::vec3(2.0f * physics->GetRadius());
                    break;
                case ColliderType::HeightField:
                case ColliderType::Capsule:
                    size = glm::vec3(
                        2.0f * physics->GetRadius(), physics->GetHeight() + 2.0f * physics->GetRadius(),
                        2.0f * physics->GetRadius()
                    );
                    break;
                }
                AddBox(world * glm::scale(glm::mat4(1.0f), size));
            }
        }
        for (const auto &child : object.GetChildren())
        {
            if (child->IsAlive())
            {
                self(self, *child);
            }
        }
    };
    visit(visit, root);
}

struct NavigationSystem::Impl
{
    dtNavMesh *mesh = nullptr;
    dtNavMeshQuery *query = nullptr;
    dtCrowd *crowd = nullptr;
    dtQueryFilter filter;
    JobSystem *jobs = nullptr;

    // Tile builds report here from workers.
    struct Shared
    {
        std::mutex mutex;
        struct Result
        {
            int x;
            int z;
            uint32_t generation;
            std::vector<uint8_t> data;
            double ms;
            // Read from the cook rather than built.
            bool loaded = false;
        };
        std::vector<Result> done;
        std::atomic<int> inFlight{0};
    };
    std::shared_ptr<Shared> shared = std::make_shared<Shared>();
    // Per tile: bumped when a tile is removed or rebuilt, so a stale build is dropped.
    std::map<TileKey, uint32_t> tileGeneration;
    std::vector<JobSystem::JobHandle> jobsInFlight;

    WorldStreamer *streamer = nullptr;
    int listener = 0;

    // Agents' targets, to report arrival.
    struct AgentTarget
    {
        bool has = false;
        float position[3] = {0.0f, 0.0f, 0.0f};
    };
    std::vector<AgentTarget> targets;

    ~Impl()
    {
        dtFreeCrowd(crowd);
        dtFreeNavMeshQuery(query);
        dtFreeNavMesh(mesh);
    }
};

namespace
{
std::mt19937 &Random()
{
    static std::mt19937 generator(1234567u);
    return generator;
}

float FRand()
{
    return std::uniform_real_distribution<float>(0.0f, 1.0f)(Random());
}

void ToFloat(const glm::dvec3 &v, float *out)
{
    out[0] = static_cast<float>(v.x);
    out[1] = static_cast<float>(v.y);
    out[2] = static_cast<float>(v.z);
}

glm::dvec3 FromFloat(const float *v)
{
    return glm::dvec3(v[0], v[1], v[2]);
}

int NextPowerOfTwoLog(int value)
{
    int bits = 0;
    while ((1 << bits) < value)
    {
        ++bits;
    }
    return bits;
}
} // namespace

NavigationSystem::NavigationSystem() = default;

NavigationSystem::~NavigationSystem()
{
    Shutdown();
}

bool NavigationSystem::Init(const NavMeshSettings &settings, JobSystem *jobs)
{
    Shutdown();
    m_settings = settings;
    auto impl = std::make_unique<Impl>();
    if (jobs)
    {
        m_jobs = jobs;
    }
    impl->jobs = m_jobs;

    // Detour packs tile, polygon and salt bits into a 32-bit reference.
    const int tileBits = NextPowerOfTwoLog(std::max(1, settings.maxTiles));
    const int polyBits = NextPowerOfTwoLog(std::max(1, settings.maxPolysPerTile));
    if (tileBits + polyBits > 22)
    {
        GENI_LOG_ERROR(
            "Navigation: %d tiles of %d polygons do not fit Detour's 32-bit references", settings.maxTiles,
            settings.maxPolysPerTile
        );
        return false;
    }

    dtNavMeshParams params{};
    params.orig[0] = static_cast<float>(settings.origin.x);
    params.orig[1] = static_cast<float>(settings.origin.y);
    params.orig[2] = static_cast<float>(settings.origin.z);
    params.tileWidth = settings.tileSize;
    params.tileHeight = settings.tileSize;
    params.maxTiles = 1 << tileBits;
    params.maxPolys = 1 << polyBits;
    impl->mesh = dtAllocNavMesh();
    if (!impl->mesh || dtStatusFailed(impl->mesh->init(&params)))
    {
        GENI_LOG_ERROR("Navigation: could not create the navigation mesh");
        return false;
    }
    impl->query = dtAllocNavMeshQuery();
    if (!impl->query || dtStatusFailed(impl->query->init(impl->mesh, 4096)))
    {
        GENI_LOG_ERROR("Navigation: could not create the query");
        return false;
    }
    impl->crowd = dtAllocCrowd();
    if (!impl->crowd || !impl->crowd->init(settings.maxAgents, std::max(1.0f, settings.agentRadius * 2.0f), impl->mesh))
    {
        GENI_LOG_ERROR("Navigation: could not create the crowd");
        return false;
    }
    // Obstacle avoidance quality presets (as in Recast's demo): 3 is high.
    dtObstacleAvoidanceParams avoidance = *impl->crowd->getObstacleAvoidanceParams(0);
    avoidance.velBias = 0.5f;
    avoidance.adaptiveDivs = 7;
    avoidance.adaptiveRings = 3;
    avoidance.adaptiveDepth = 3;
    impl->crowd->setObstacleAvoidanceParams(3, &avoidance);
    impl->filter.setIncludeFlags(NavMeshBuilder::WalkableFlag);
    impl->filter.setExcludeFlags(0);
    impl->targets.resize(static_cast<size_t>(settings.maxAgents));
    m_impl = std::move(impl);
    m_stats = NavStats();
    ++m_generation;
    return true;
}

void NavigationSystem::Shutdown()
{
    if (!m_impl)
    {
        return;
    }
    DetachFromWorld();
    for (const auto &job : m_impl->jobsInFlight)
    {
        job.Wait();
    }
    m_impl.reset();
    m_stats = NavStats();
}

bool NavigationSystem::IsInitialized() const
{
    return m_impl != nullptr;
}

bool NavigationSystem::AddTile(std::vector<uint8_t> data)
{
    if (!m_impl || data.empty())
    {
        return false;
    }
    const dtMeshHeader *header = reinterpret_cast<const dtMeshHeader *>(data.data());
    if (header->magic != DT_NAVMESH_MAGIC)
    {
        return false;
    }
    // A tile already there (a rebuild) is replaced.
    if (dtTileRef existing = m_impl->mesh->getTileRefAt(header->x, header->y, header->layer))
    {
        m_impl->mesh->removeTile(existing, nullptr, nullptr);
        m_stats.tiles = m_stats.tiles > 0 ? m_stats.tiles - 1 : 0;
    }
    auto *copy = static_cast<unsigned char *>(dtAlloc(data.size(), DT_ALLOC_PERM));
    std::memcpy(copy, data.data(), data.size());
    if (dtStatusFailed(m_impl->mesh->addTile(copy, static_cast<int>(data.size()), DT_TILE_FREE_DATA, 0, nullptr)))
    {
        dtFree(copy);
        GENI_LOG_WARN("Navigation: could not add tile %d,%d", header->x, header->y);
        return false;
    }
    ++m_stats.tiles;
    return true;
}

void NavigationSystem::RemoveTile(int tileX, int tileZ)
{
    if (!m_impl)
    {
        return;
    }
    ++m_impl->tileGeneration[{tileX, tileZ}];
    if (dtTileRef ref = m_impl->mesh->getTileRefAt(tileX, tileZ, 0))
    {
        m_impl->mesh->removeTile(ref, nullptr, nullptr);
        m_stats.tiles = m_stats.tiles > 0 ? m_stats.tiles - 1 : 0;
    }
}

bool NavigationSystem::HasTile(int tileX, int tileZ) const
{
    return m_impl && m_impl->mesh->getTileRefAt(tileX, tileZ, 0) != 0;
}

void NavigationSystem::TileAt(const glm::dvec3 &world, int &tileX, int &tileZ) const
{
    tileX = static_cast<int>(std::floor((world.x - m_settings.origin.x) / m_settings.tileSize));
    tileZ = static_cast<int>(std::floor((world.z - m_settings.origin.z) / m_settings.tileSize));
}

void NavigationSystem::BuildTileAsync(int tileX, int tileZ, NavGeometry geometry)
{
    if (!m_impl)
    {
        return;
    }
    const uint32_t generation = ++m_impl->tileGeneration[{tileX, tileZ}];
    std::shared_ptr<Impl::Shared> shared = m_impl->shared;
    const NavMeshSettings settings = m_settings;
    auto build = [shared, settings, tileX, tileZ, generation, geometry = std::move(geometry)]() {
        const auto start = std::chrono::steady_clock::now();
        std::string error;
        std::vector<uint8_t> data = NavMeshBuilder::BuildTile(geometry, settings, tileX, tileZ, &error);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        std::lock_guard<std::mutex> lock(shared->mutex);
        shared->done.push_back({tileX, tileZ, generation, std::move(data), ms});
        shared->inFlight.fetch_sub(1);
    };
    shared->inFlight.fetch_add(1);
    if (m_impl->jobs)
    {
        m_impl->jobsInFlight.push_back(m_impl->jobs->Schedule(std::move(build), "NavMeshBuilder::BuildTile"));
    }
    else
    {
        build();
    }
}

void NavigationSystem::LoadTileAsync(int tileX, int tileZ, std::string path)
{
    if (!m_impl)
    {
        return;
    }
    const uint32_t generation = ++m_impl->tileGeneration[{tileX, tileZ}];
    std::shared_ptr<Impl::Shared> shared = m_impl->shared;
    auto load = [shared, tileX, tileZ, generation, path = std::move(path)]() {
        const auto start = std::chrono::steady_clock::now();
        std::vector<uint8_t> data;
        if (!cooked::ReadFile(path, data))
        {
            GENI_LOG_ERROR("navigation: cannot read the cooked tile %s", path.c_str());
            data.clear();
        }
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        std::lock_guard<std::mutex> lock(shared->mutex);
        shared->done.push_back({tileX, tileZ, generation, std::move(data), ms, true});
        shared->inFlight.fetch_sub(1);
    };
    shared->inFlight.fetch_add(1);
    if (m_impl->jobs)
    {
        m_impl->jobsInFlight.push_back(m_impl->jobs->Schedule(std::move(load), "NavigationSystem::LoadTile"));
    }
    else
    {
        load();
    }
}

void NavigationSystem::FinishBuilds()
{
    if (!m_impl)
    {
        return;
    }
    for (const auto &job : m_impl->jobsInFlight)
    {
        if (m_impl->jobs)
        {
            m_impl->jobs->Wait(job);
        }
        else
        {
            job.Wait();
        }
    }
    m_impl->jobsInFlight.clear();
    Update(0.0f);
}

bool NavigationSystem::BuildForScene(Scene &scene)
{
    NavGeometry geometry;
    for (const auto &object : scene.GetObjects())
    {
        if (object->IsAlive())
        {
            geometry.AddColliders(*object);
        }
    }
    if (geometry.Empty())
    {
        GENI_LOG_WARN("Navigation: the scene has no static colliders");
        return false;
    }
    if (!m_impl && !Init(m_settings))
    {
        return false;
    }
    int minX = 0;
    int minZ = 0;
    int maxX = 0;
    int maxZ = 0;
    TileAt(glm::dvec3(geometry.boundsMin), minX, minZ);
    TileAt(glm::dvec3(geometry.boundsMax), maxX, maxZ);
    bool any = false;
    for (int z = minZ; z <= maxZ; ++z)
    {
        for (int x = minX; x <= maxX; ++x)
        {
            std::string error;
            std::vector<uint8_t> data = NavMeshBuilder::BuildTile(geometry, m_settings, x, z, &error);
            if (!data.empty())
            {
                any = AddTile(std::move(data)) || any;
                ++m_stats.tilesBuilt;
            }
        }
    }
    return any;
}

bool NavigationSystem::AttachToWorld(WorldStreamer &streamer, Scene &scene, AssetManager *assets)
{
    (void)scene;
    const WorldDescription *world = streamer.GetDescription();
    if (!world)
    {
        return false;
    }
    NavMeshSettings settings = m_settings;
    settings.origin = world->origin;
    settings.tileSize = world->cellSize;
    if (!Init(settings))
    {
        return false;
    }
    m_impl->streamer = &streamer;
    // Cooked tiles are only valid for the settings they were built with.
    const bool cookedUsable = assets && world->navKey != 0 && world->navKey == m_settings.BuildKey();
    if (assets && world->navKey != 0 && !cookedUsable)
    {
        GENI_LOG_INFO("navigation: the world's tiles were cooked with other settings; building them at load");
    }
    auto onCell = [this, world, assets, cookedUsable](int x, int z, GameObject *root, bool loaded) {
        if (!m_impl)
        {
            return;
        }
        if (!loaded)
        {
            RemoveTile(x, z);
            return;
        }
        if (cookedUsable)
        {
            const WorldCellInfo *cell = world->FindCell(x, z);
            if (cell && cell->nav.IsValid())
            {
                const std::filesystem::path path = assets->FindCookedFile(cell->nav, AssetType::NavTile);
                if (!path.empty())
                {
                    LoadTileAsync(x, z, path.string());
                    return;
                }
            }
            else if (cell)
            {
                return; // cooked as having nothing walkable
            }
        }
        if (!root)
        {
            return;
        }
        // Gather on the main thread (it reads the scene), build on a worker.
        NavGeometry geometry;
        geometry.AddColliders(*root);
        if (!geometry.Empty())
        {
            BuildTileAsync(x, z, std::move(geometry));
        }
    };
    m_impl->listener = streamer.AddCellListener(onCell);
    streamer.ForEachLoadedCell(onCell);
    return true;
}

void NavigationSystem::DetachFromWorld()
{
    if (m_impl && m_impl->streamer)
    {
        m_impl->streamer->RemoveCellListener(m_impl->listener);
        m_impl->streamer = nullptr;
        m_impl->listener = 0;
    }
}

bool NavigationSystem::FindNearestPoint(const glm::dvec3 &world, glm::dvec3 &result, float searchRadius) const
{
    if (!m_impl)
    {
        return false;
    }
    float center[3];
    ToFloat(world, center);
    const float extents[3] = {searchRadius, searchRadius, searchRadius};
    dtPolyRef ref = 0;
    float nearest[3];
    if (dtStatusFailed(m_impl->query->findNearestPoly(center, extents, &m_impl->filter, &ref, nearest)) || !ref)
    {
        return false;
    }
    result = FromFloat(nearest);
    return true;
}

bool NavigationSystem::FindPath(const glm::dvec3 &start, const glm::dvec3 &end, std::vector<glm::dvec3> &corners) const
{
    corners.clear();
    if (!m_impl)
    {
        return false;
    }
    float startPos[3];
    float endPos[3];
    ToFloat(start, startPos);
    ToFloat(end, endPos);
    const float extents[3] = {2.0f, 4.0f, 2.0f};
    dtPolyRef startRef = 0;
    dtPolyRef endRef = 0;
    float startNearest[3];
    float endNearest[3];
    m_impl->query->findNearestPoly(startPos, extents, &m_impl->filter, &startRef, startNearest);
    m_impl->query->findNearestPoly(endPos, extents, &m_impl->filter, &endRef, endNearest);
    if (!startRef || !endRef)
    {
        return false;
    }
    dtPolyRef path[kMaxPathPolys];
    int pathCount = 0;
    const dtStatus status = m_impl->query->findPath(
        startRef, endRef, startNearest, endNearest, &m_impl->filter, path, &pathCount, kMaxPathPolys
    );
    if (dtStatusFailed(status) || pathCount == 0)
    {
        return false;
    }
    float straight[kMaxStraightPath * 3];
    int straightCount = 0;
    m_impl->query->findStraightPath(
        startNearest, endNearest, path, pathCount, straight, nullptr, nullptr, &straightCount, kMaxStraightPath
    );
    for (int i = 0; i < straightCount; ++i)
    {
        corners.push_back(FromFloat(&straight[i * 3]));
    }
    // A partial path (the end is unreachable) does not reach the requested point.
    return !dtStatusDetail(status, DT_PARTIAL_RESULT) && !corners.empty();
}

bool NavigationSystem::RandomPointNear(const glm::dvec3 &center, float radius, glm::dvec3 &result) const
{
    if (!m_impl)
    {
        return false;
    }
    float centerPos[3];
    ToFloat(center, centerPos);
    const float extents[3] = {2.0f, 4.0f, 2.0f};
    dtPolyRef startRef = 0;
    float nearest[3];
    if (dtStatusFailed(m_impl->query->findNearestPoly(centerPos, extents, &m_impl->filter, &startRef, nearest)) ||
        !startRef)
    {
        return false;
    }
    dtPolyRef randomRef = 0;
    float point[3];
    if (dtStatusFailed(m_impl->query->findRandomPointAroundCircle(
            startRef, nearest, radius, &m_impl->filter, FRand, &randomRef, point
        )) ||
        !randomRef)
    {
        return false;
    }
    result = FromFloat(point);
    return true;
}

int NavigationSystem::AddAgent(const glm::dvec3 &world, const NavAgentParams &params)
{
    if (!m_impl)
    {
        return -1;
    }
    glm::dvec3 onMesh;
    if (!FindNearestPoint(world, onMesh, 2.0f))
    {
        return -1;
    }
    dtCrowdAgentParams agent{};
    agent.radius = params.radius;
    agent.height = params.height;
    agent.maxAcceleration = params.maxAcceleration;
    agent.maxSpeed = params.maxSpeed;
    agent.collisionQueryRange = params.radius * 12.0f;
    agent.pathOptimizationRange = params.radius * 30.0f;
    agent.separationWeight = params.separationWeight;
    agent.updateFlags = DT_CROWD_ANTICIPATE_TURNS | DT_CROWD_OPTIMIZE_VIS | DT_CROWD_OPTIMIZE_TOPO |
                        DT_CROWD_OBSTACLE_AVOIDANCE | DT_CROWD_SEPARATION;
    agent.obstacleAvoidanceType = 3;
    agent.queryFilterType = 0;
    float position[3];
    ToFloat(onMesh, position);
    const int index = m_impl->crowd->addAgent(position, &agent);
    if (index >= 0)
    {
        m_impl->targets[static_cast<size_t>(index)] = Impl::AgentTarget();
        ++m_stats.agents;
    }
    return index;
}

void NavigationSystem::RemoveAgent(int agent)
{
    if (!m_impl || agent < 0)
    {
        return;
    }
    const dtCrowdAgent *state = m_impl->crowd->getAgent(agent);
    if (state && state->active)
    {
        m_impl->crowd->removeAgent(agent);
        m_stats.agents = m_stats.agents > 0 ? m_stats.agents - 1 : 0;
    }
}

bool NavigationSystem::SetAgentTarget(int agent, const glm::dvec3 &world)
{
    if (!m_impl || agent < 0)
    {
        return false;
    }
    float position[3];
    ToFloat(world, position);
    const float extents[3] = {2.0f, 4.0f, 2.0f};
    dtPolyRef ref = 0;
    float nearest[3];
    if (dtStatusFailed(m_impl->query->findNearestPoly(position, extents, &m_impl->filter, &ref, nearest)) || !ref)
    {
        return false;
    }
    if (!m_impl->crowd->requestMoveTarget(agent, ref, nearest))
    {
        return false;
    }
    Impl::AgentTarget &target = m_impl->targets[static_cast<size_t>(agent)];
    target.has = true;
    std::memcpy(target.position, nearest, sizeof(nearest));
    return true;
}

void NavigationSystem::StopAgent(int agent)
{
    if (m_impl && agent >= 0)
    {
        m_impl->crowd->resetMoveTarget(agent);
        m_impl->targets[static_cast<size_t>(agent)].has = false;
    }
}

void NavigationSystem::SetAgentMaxSpeed(int agent, float speed)
{
    if (!m_impl || agent < 0)
    {
        return;
    }
    const dtCrowdAgent *state = m_impl->crowd->getAgent(agent);
    if (state && state->active)
    {
        dtCrowdAgentParams params = state->params;
        params.maxSpeed = speed;
        m_impl->crowd->updateAgentParameters(agent, &params);
    }
}

NavAgentState NavigationSystem::GetAgent(int agent) const
{
    NavAgentState result;
    if (!m_impl || agent < 0)
    {
        return result;
    }
    const dtCrowdAgent *state = m_impl->crowd->getAgent(agent);
    if (!state || !state->active)
    {
        return result;
    }
    result.valid = state->state != DT_CROWDAGENT_STATE_INVALID;
    result.position = FromFloat(state->npos);
    result.velocity = glm::vec3(state->vel[0], state->vel[1], state->vel[2]);
    const Impl::AgentTarget &target = m_impl->targets[static_cast<size_t>(agent)];
    result.hasTarget = target.has;
    if (target.has)
    {
        const float dx = state->npos[0] - target.position[0];
        const float dz = state->npos[2] - target.position[2];
        result.arrived = dx * dx + dz * dz < std::max(0.25f, state->params.radius * state->params.radius * 4.0f) ||
                         state->targetState == DT_CROWDAGENT_TARGET_FAILED;
    }
    return result;
}

void NavigationSystem::Update(float deltaTime)
{
    if (!m_impl)
    {
        return;
    }
    GENI_PROFILE_SCOPE("NavigationSystem::Update");
    std::vector<Impl::Shared::Result> done;
    {
        std::lock_guard<std::mutex> lock(m_impl->shared->mutex);
        done.swap(m_impl->shared->done);
    }
    for (auto &result : done)
    {
        if (result.loaded)
        {
            ++m_stats.tilesLoaded;
        }
        else
        {
            m_stats.lastBuildMs = result.ms;
            m_stats.peakBuildMs = std::max(m_stats.peakBuildMs, result.ms);
            ++m_stats.tilesBuilt;
        }
        // Dropped if the tile was unloaded (or rebuilt) since the build started.
        if (m_impl->tileGeneration[{result.x, result.z}] == result.generation && !result.data.empty())
        {
            AddTile(std::move(result.data));
        }
    }
    m_impl->jobsInFlight.erase(
        std::remove_if(
            m_impl->jobsInFlight.begin(), m_impl->jobsInFlight.end(),
            [](const JobSystem::JobHandle &job) { return job.IsComplete(); }
        ),
        m_impl->jobsInFlight.end()
    );
    m_stats.tilesBuilding = static_cast<uint32_t>(std::max(0, m_impl->shared->inFlight.load()));
    if (deltaTime > 0.0f)
    {
        m_impl->crowd->update(deltaTime, nullptr);
    }
}

} // namespace Geni
