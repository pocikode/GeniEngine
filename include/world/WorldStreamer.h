#pragma once

#include "assets/AssetHandle.h"
#include "assets/AssetID.h"
#include "world/WorldFormat.h"

#include <cstdint>
#include <functional>
#include <glm/ext/vector_double3.hpp>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Geni
{

class AssetManager;
class GameObject;
class Scene;
class SceneAsset;
class SceneInstantiation;
class WorldAsset;

enum class StreamingPlatform
{
    Desktop,
    Mobile,
    Web
};

// How much a world may load, and how fast (docs/design/0007). The presets are the
// per-platform budgets; a game can start from one and change fields.
struct StreamingBudget
{
    // Cells closer than this (to the cell's rectangle, on the ground plane) are loaded in
    // full: objects, collision, lights.
    float loadRadius = 384.0f;
    // Cells closer than this, and not loaded in full, show their HLOD.
    float hlodRadius = 1536.0f;
    // A loaded cell (or HLOD) is dropped only this much beyond its radius.
    float hysteresis = 64.0f;
    // The scene origin moves to the nearest cell corner when the focus is farther away.
    float rebaseDistance = 1024.0f;
    // Scene nodes created per frame, over every cell being instantiated.
    uint32_t nodesPerFrame = 512;
    // Scene nodes destroyed per frame when cells unload.
    uint32_t destroyNodesPerFrame = 2048;
    // Cells whose scenes are being read at the same time.
    uint32_t maxConcurrentLoads = 4;
    // Cap on the objects of loaded cells (CPU memory and per-frame update cost).
    uint32_t maxLoadedNodes = 60000;
    // Resident GPU memory for assets (AssetManager::GetResidentGpuBytes). No cell or
    // HLOD starts loading when it would go over.
    uint64_t gpuMemoryBytes = 768ull << 20;

    static StreamingBudget ForPlatform(StreamingPlatform platform);
    static StreamingPlatform CurrentPlatform();
};

const char *StreamingPlatformName(StreamingPlatform platform);

enum class CellState
{
    Unloaded,
    // Scene file and prefabs being read on workers.
    Loading,
    // Objects being created a slice per frame under an inactive cell root.
    Instantiating,
    // All objects exist; waiting for their meshes and materials before showing them.
    WaitingForAssets,
    Loaded,
    // Root deactivated, objects destroyed a slice per frame.
    Unloading
};

const char *CellStateName(CellState state);

struct StreamingStats
{
    uint32_t cellsLoaded = 0;
    uint32_t cellsLoading = 0; // Loading, Instantiating or WaitingForAssets
    uint32_t cellsUnloading = 0;
    uint32_t hlodCells = 0; // HLODs shown
    uint32_t loadedNodes = 0;
    uint64_t residentGpuBytes = 0;
    uint64_t peakResidentGpuBytes = 0;
    uint32_t peakLoadedNodes = 0;
    uint64_t cellLoadsStarted = 0;
    uint64_t cellsUnloadedTotal = 0;
    // A cell inside the load radius could not start because of the budget this frame.
    bool budgetLimited = false;
    // Frames on which a cell within half the load radius of the focus was not loaded
    // (it would show as HLOD or as nothing close to the camera): visible pop-in.
    uint64_t lateFrames = 0;
    double updateMs = 0.0;
    double peakUpdateMs = 0.0;
    glm::dvec3 focus{0.0};
    glm::dvec3 origin{0.0};
    uint32_t originShifts = 0;
};

// Streams a cooked world into a scene around a focus point (docs/design/0007). The
// engine owns one (Engine::OpenWorld) and updates it once per frame; tools and tests can
// run their own on any scene.
class WorldStreamer
{
  public:
    WorldStreamer();
    ~WorldStreamer();
    WorldStreamer(const WorldStreamer &) = delete;
    WorldStreamer &operator=(const WorldStreamer &) = delete;

    // Loads the world description and its persistent scene into `scene` (cleared first)
    // synchronously, then streams cells from the next Update.
    bool Open(
        Scene &scene, AssetManager &assets, AssetID world, const StreamingBudget &budget, std::string *error = nullptr
    );
    // Destroys every cell and HLOD object (the persistent scene stays) and forgets the
    // world.
    void Close();
    bool IsOpen() const
    {
        return m_scene != nullptr;
    }

    // Streams around this world position instead of the main camera's.
    void SetFocus(const glm::dvec3 &worldPosition);
    void ClearFocus();

    // One frame of streaming: rebase, unload, start loads, instantiate, show.
    void Update();
    // Runs Update (and the asset manager) until nothing is pending: everything the focus
    // needs is loaded. For tests, tools and a loading screen.
    void Flush(int maxFrames = 100000);

    const StreamingBudget &GetBudget() const
    {
        return m_budget;
    }
    void SetBudget(const StreamingBudget &budget)
    {
        m_budget = budget;
    }
    const StreamingStats &GetStats() const
    {
        return m_stats;
    }
    const WorldDescription *GetDescription() const;
    CellState GetCellState(int x, int z) const;
    bool IsHlodShown(int x, int z) const;
    // The root object of a loaded (or loading) cell, nullptr otherwise.
    GameObject *GetCellRoot(int x, int z) const;
    // Called when a cell finishes loading (`loaded` true, its objects are all shown) and
    // when a loaded cell starts unloading (false, before its objects go). Navigation
    // builds and drops its tiles from these (docs/design/0014). Returns an ID for
    // RemoveCellListener.
    using CellListener = std::function<void(int x, int z, GameObject *root, bool loaded)>;
    int AddCellListener(CellListener listener);
    void RemoveCellListener(int id);
    // Calls `listener` for every cell that is loaded now.
    void ForEachLoadedCell(const CellListener &listener) const;

    // The cell a world position lies in (may be outside the grid).
    void CellAt(const glm::dvec3 &worldPosition, int &x, int &z) const;

  private:
    struct Cell;

    Cell &GetOrAddCell(const WorldCellInfo &info);
    float DistanceToCell(const WorldCellInfo &info, const glm::dvec3 &focus) const;
    glm::dvec3 CurrentFocus() const;
    void Rebase(const glm::dvec3 &focus);
    void StartUnload(Cell &cell);
    void StepUnloading(uint32_t &budget);
    void StartLoad(Cell &cell);
    bool StepLoading(Cell &cell, uint32_t &budget);
    bool AssetsReady(GameObject &root) const;
    void ShowHlod(Cell &cell, bool shown);
    void DropHlod(Cell &cell);
    void UpdateHlod(Cell &cell);
    GameObject *FindObject(uint64_t id) const;

    Scene *m_scene = nullptr;
    AssetManager *m_assets = nullptr;
    AssetHandle<WorldAsset> m_world;
    StreamingBudget m_budget;
    StreamingStats m_stats;
    bool m_hasFocus = false;
    glm::dvec3 m_focus{0.0};
    std::unordered_map<int, std::unique_ptr<Cell>> m_cells;
    // Cell roots being destroyed a slice per frame.
    std::vector<uint64_t> m_dying;
    std::vector<std::pair<int, CellListener>> m_cellListeners;
    int m_nextListenerId = 1;
    uint32_t m_framesSinceGarbage = 0;
    bool m_unloadedSinceGarbage = false;
};

} // namespace Geni
