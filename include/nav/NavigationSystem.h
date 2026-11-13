#pragma once

#include "nav/NavMeshBuilder.h"

#include <cstdint>
#include <glm/ext/vector_double3.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <memory>
#include <string>
#include <vector>

namespace Geni
{

class AssetManager;
class GameObject;
class JobSystem;
class Scene;
class WorldStreamer;

struct NavAgentParams
{
    float radius = 0.35f;
    float height = 1.8f;
    float maxSpeed = 1.4f;
    float maxAcceleration = 6.0f;
    float separationWeight = 2.0f;
};

struct NavAgentState
{
    glm::dvec3 position{0.0};
    glm::vec3 velocity{0.0f};
    bool valid = false;
    bool hasTarget = false;
    // Within a step of the target (or no target).
    bool arrived = true;
};

struct NavStats
{
    uint32_t tiles = 0;
    uint32_t tilesBuilding = 0;
    uint64_t tilesBuilt = 0;
    // Tiles read from the cook instead of built (docs/design/0014).
    uint64_t tilesLoaded = 0;
    uint32_t agents = 0;
    double lastBuildMs = 0.0;
    double peakBuildMs = 0.0;
};

// The navigation mesh and its crowd (docs/design/0014). Coordinates are world positions
// (scene position + scene origin). Main thread only, except the tile builds it runs on
// the job system.
class NavigationSystem
{
  public:
    NavigationSystem();
    ~NavigationSystem();
    NavigationSystem(const NavigationSystem &) = delete;
    NavigationSystem &operator=(const NavigationSystem &) = delete;

    // `jobs` runs tile builds (nullptr keeps the one given before; with none, builds run
    // on the calling thread).
    bool Init(const NavMeshSettings &settings, JobSystem *jobs = nullptr);
    void SetJobSystem(JobSystem *jobs)
    {
        m_jobs = jobs;
    }
    void Shutdown();
    bool IsInitialized() const;
    const NavMeshSettings &GetSettings() const
    {
        return m_settings;
    }
    // Bumped by every Init: agents added before it are gone.
    uint32_t GetGeneration() const
    {
        return m_generation;
    }

    // Tiles. AddTile takes the data BuildTile produced.
    bool AddTile(std::vector<uint8_t> data);
    void RemoveTile(int tileX, int tileZ);
    bool HasTile(int tileX, int tileZ) const;
    void TileAt(const glm::dvec3 &world, int &tileX, int &tileZ) const;
    // Builds the tile on the job system (or now without one); Update adds it.
    void BuildTileAsync(int tileX, int tileZ, NavGeometry geometry);
    // Builds every tile that `scene`'s static colliders touch, now. For small scenes,
    // tools and tests; streamed worlds use AttachToWorld.
    bool BuildForScene(Scene &scene);
    // Follows a streamed world: Init with the world's grid, then one tile per loaded
    // cell, added when the cell loads and dropped when it unloads. With `assets`, a cell's
    // cooked tile is read on a worker when the world was cooked with the same settings
    // (NavMeshSettings::BuildKey); otherwise the tile is built from the cell's colliders.
    bool AttachToWorld(WorldStreamer &streamer, Scene &scene, AssetManager *assets = nullptr);
    // Reads a cooked tile on the job system (or now without one); Update adds it.
    void LoadTileAsync(int tileX, int tileZ, std::string path);
    void DetachFromWorld();
    // Waits for tile builds in flight and adds them (tests, loading screens).
    void FinishBuilds();

    // Queries.
    bool FindNearestPoint(const glm::dvec3 &world, glm::dvec3 &result, float searchRadius = 4.0f) const;
    bool FindPath(const glm::dvec3 &start, const glm::dvec3 &end, std::vector<glm::dvec3> &corners) const;
    bool RandomPointNear(const glm::dvec3 &center, float radius, glm::dvec3 &result) const;

    // Crowd agents. AddAgent returns -1 when the position is not on the mesh.
    int AddAgent(const glm::dvec3 &world, const NavAgentParams &params);
    void RemoveAgent(int agent);
    bool SetAgentTarget(int agent, const glm::dvec3 &world);
    void StopAgent(int agent);
    void SetAgentMaxSpeed(int agent, float speed);
    NavAgentState GetAgent(int agent) const;

    // Adds finished tiles, then steps the crowd. Engine calls it once per frame.
    void Update(float deltaTime);

    const NavStats &GetStats() const
    {
        return m_stats;
    }

  private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    NavMeshSettings m_settings;
    NavStats m_stats;
    uint32_t m_generation = 0;
    JobSystem *m_jobs = nullptr;
};

} // namespace Geni
