#include "world/WorldStreamer.h"

#include "assets/AssetManager.h"
#include "core/Log.h"
#include "core/Profiler.h"
#include "scene/GameObject.h"
#include "scene/Scene.h"
#include "scene/SceneSerializer.h"
#include "scene/components/MeshComponent.h"
#include "scene/components/SkinnedMeshComponent.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <thread>

namespace Geni
{

StreamingBudget StreamingBudget::ForPlatform(StreamingPlatform platform)
{
    StreamingBudget budget;
    switch (platform)
    {
    case StreamingPlatform::Desktop:
        break;
    case StreamingPlatform::Mobile:
        // M7 targets: a mid-range phone at 30 fps.
        budget.loadRadius = 256.0f;
        budget.hlodRadius = 896.0f;
        budget.nodesPerFrame = 192;
        budget.destroyNodesPerFrame = 768;
        budget.maxConcurrentLoads = 2;
        budget.maxLoadedNodes = 15000;
        budget.gpuMemoryBytes = 256ull << 20;
        break;
    case StreamingPlatform::Web:
        // M8: browser memory and a single main thread.
        budget.loadRadius = 256.0f;
        budget.hlodRadius = 768.0f;
        budget.nodesPerFrame = 128;
        budget.destroyNodesPerFrame = 512;
        budget.maxConcurrentLoads = 2;
        budget.maxLoadedNodes = 10000;
        budget.gpuMemoryBytes = 128ull << 20;
        break;
    }
    return budget;
}

StreamingPlatform StreamingBudget::CurrentPlatform()
{
#if defined(__EMSCRIPTEN__)
    return StreamingPlatform::Web;
#elif defined(__ANDROID__) || (defined(__APPLE__) && defined(GENI_PLATFORM_IOS))
    return StreamingPlatform::Mobile;
#else
    return StreamingPlatform::Desktop;
#endif
}

const char *StreamingPlatformName(StreamingPlatform platform)
{
    switch (platform)
    {
    case StreamingPlatform::Desktop:
        return "desktop";
    case StreamingPlatform::Mobile:
        return "mobile";
    case StreamingPlatform::Web:
        return "web";
    }
    return "unknown";
}

const char *CellStateName(CellState state)
{
    switch (state)
    {
    case CellState::Unloaded:
        return "unloaded";
    case CellState::Loading:
        return "loading";
    case CellState::Instantiating:
        return "instantiating";
    case CellState::WaitingForAssets:
        return "waiting for assets";
    case CellState::Loaded:
        return "loaded";
    case CellState::Unloading:
        return "unloading";
    }
    return "unknown";
}

struct WorldStreamer::Cell
{
    const WorldCellInfo *info = nullptr;
    CellState state = CellState::Unloaded;
    float distance = 0.0f;

    AssetHandle<SceneAsset> scene;
    std::vector<AssetHandle<SceneAsset>> prefabs;
    std::unique_ptr<SceneInstantiation> instantiation;
    uint64_t root = 0;

    bool hlodWanted = false;
    AssetHandle<SceneAsset> hlodScene;
    uint64_t hlodRoot = 0;
    bool hlodFailed = false;

    bool IsResident() const
    {
        return state == CellState::Loading || state == CellState::Instantiating ||
               state == CellState::WaitingForAssets || state == CellState::Loaded;
    }
};

namespace
{
size_t CountDescendants(const GameObject &object)
{
    size_t count = 0;
    for (const auto &child : object.GetChildren())
    {
        count += 1 + CountDescendants(*child);
    }
    return count;
}

std::string CellName(const char *prefix, int x, int z)
{
    return std::string(prefix) + " " + std::to_string(x) + "," + std::to_string(z);
}
} // namespace

WorldStreamer::WorldStreamer() = default;

WorldStreamer::~WorldStreamer()
{
    // The scene may already be gone at shutdown (Engine::Destroy closes the world first);
    // only forget the cells here.
    m_cells.clear();
}

bool WorldStreamer::Open(
    Scene &scene, AssetManager &assets, AssetID world, const StreamingBudget &budget, std::string *error
)
{
    Close();
    auto fail = [&](const std::string &message) {
        if (error)
        {
            *error = message;
        }
        GENI_LOG_ERROR("world %s: %s", world.ToHex().c_str(), message.c_str());
        return false;
    };

    auto handle = assets.LoadWorld(world, false);
    if (!handle.IsReady())
    {
        return fail(handle.IsValid() && !handle.GetError().empty() ? handle.GetError() : "not in the cooked manifest");
    }

    scene.Clear();
    const WorldDescription &description = handle->description;
    if (description.persistent.IsValid())
    {
        auto persistent = assets.LoadScene(description.persistent, false);
        if (!persistent.IsReady())
        {
            return fail("persistent scene " + description.persistent.ToHex() + " did not load");
        }
        SerializationContext context{&scene, &assets};
        std::string why;
        if (!SceneSerializer::Load(scene, persistent->document, context, &why))
        {
            return fail("persistent scene: " + why);
        }
    }

    m_scene = &scene;
    m_assets = &assets;
    m_world = handle;
    m_budget = budget;
    m_stats = StreamingStats();
    // A world is placed in world coordinates: start with the origin at the world's grid
    // origin plane, so the persistent scene's objects keep their positions.
    m_stats.origin = scene.GetOrigin();
    GENI_LOG_INFO(
        "Opened world '%s': %d x %d cells of %.0f m, %zu with content", description.name.c_str(), description.cellsX,
        description.cellsZ, description.cellSize, description.cells.size()
    );
    return true;
}

void WorldStreamer::Close()
{
    if (m_scene)
    {
        for (auto &[index, cell] : m_cells)
        {
            if (cell->state == CellState::Loaded)
            {
                GameObject *root = FindObject(cell->root);
                for (auto &[id, listener] : m_cellListeners)
                {
                    (void)id;
                    listener(cell->info->x, cell->info->z, root, false);
                }
            }
            if (GameObject *root = FindObject(cell->root))
            {
                m_scene->DestroyObject(root);
            }
            if (GameObject *hlod = FindObject(cell->hlodRoot))
            {
                m_scene->DestroyObject(hlod);
            }
        }
        for (uint64_t id : m_dying)
        {
            if (GameObject *root = FindObject(id))
            {
                m_scene->DestroyObject(root);
            }
        }
    }
    m_cells.clear();
    m_dying.clear();
    m_world.Reset();
    m_scene = nullptr;
    m_assets = nullptr;
    m_hasFocus = false;
}

int WorldStreamer::AddCellListener(CellListener listener)
{
    const int id = m_nextListenerId++;
    m_cellListeners.emplace_back(id, std::move(listener));
    return id;
}

void WorldStreamer::RemoveCellListener(int id)
{
    m_cellListeners.erase(
        std::remove_if(
            m_cellListeners.begin(), m_cellListeners.end(), [id](const auto &entry) { return entry.first == id; }
        ),
        m_cellListeners.end()
    );
}

void WorldStreamer::ForEachLoadedCell(const CellListener &listener) const
{
    for (const auto &[index, cell] : m_cells)
    {
        (void)index;
        if (cell->state == CellState::Loaded)
        {
            listener(cell->info->x, cell->info->z, FindObject(cell->root), true);
        }
    }
}

const WorldDescription *WorldStreamer::GetDescription() const
{
    return m_world.IsReady() ? &m_world->description : nullptr;
}

void WorldStreamer::SetFocus(const glm::dvec3 &worldPosition)
{
    m_focus = worldPosition;
    m_hasFocus = true;
}

void WorldStreamer::ClearFocus()
{
    m_hasFocus = false;
}

GameObject *WorldStreamer::FindObject(uint64_t id) const
{
    return id != 0 && m_scene ? m_scene->FindObjectByID(id) : nullptr;
}

void WorldStreamer::CellAt(const glm::dvec3 &worldPosition, int &x, int &z) const
{
    const WorldDescription *description = GetDescription();
    if (!description)
    {
        x = z = 0;
        return;
    }
    x = static_cast<int>(std::floor((worldPosition.x - description->origin.x) / description->cellSize));
    z = static_cast<int>(std::floor((worldPosition.z - description->origin.z) / description->cellSize));
}

CellState WorldStreamer::GetCellState(int x, int z) const
{
    const WorldDescription *description = GetDescription();
    if (!description || !description->Contains(x, z))
    {
        return CellState::Unloaded;
    }
    const auto it = m_cells.find(description->CellIndex(x, z));
    return it == m_cells.end() ? CellState::Unloaded : it->second->state;
}

bool WorldStreamer::IsHlodShown(int x, int z) const
{
    const WorldDescription *description = GetDescription();
    if (!description || !description->Contains(x, z))
    {
        return false;
    }
    const auto it = m_cells.find(description->CellIndex(x, z));
    if (it == m_cells.end())
    {
        return false;
    }
    const GameObject *hlod = FindObject(it->second->hlodRoot);
    return hlod && hlod->IsActive();
}

GameObject *WorldStreamer::GetCellRoot(int x, int z) const
{
    const WorldDescription *description = GetDescription();
    if (!description || !description->Contains(x, z))
    {
        return nullptr;
    }
    const auto it = m_cells.find(description->CellIndex(x, z));
    return it == m_cells.end() ? nullptr : FindObject(it->second->root);
}

WorldStreamer::Cell &WorldStreamer::GetOrAddCell(const WorldCellInfo &info)
{
    const int index = m_world->description.CellIndex(info.x, info.z);
    auto &slot = m_cells[index];
    if (!slot)
    {
        slot = std::make_unique<Cell>();
        slot->info = &info;
    }
    return *slot;
}

float WorldStreamer::DistanceToCell(const WorldCellInfo &info, const glm::dvec3 &focus) const
{
    const WorldDescription &description = m_world->description;
    const glm::dvec3 lo = description.CellCorner(info.x, info.z);
    const double size = description.cellSize;
    const double dx = std::max({lo.x - focus.x, 0.0, focus.x - (lo.x + size)});
    const double dz = std::max({lo.z - focus.z, 0.0, focus.z - (lo.z + size)});
    return static_cast<float>(std::sqrt(dx * dx + dz * dz));
}

glm::dvec3 WorldStreamer::CurrentFocus() const
{
    if (m_hasFocus)
    {
        return m_focus;
    }
    if (GameObject *camera = m_scene->GetMainCamera())
    {
        return m_scene->ToWorld(camera->GetWorldPosition());
    }
    return m_scene->GetOrigin();
}

void WorldStreamer::Rebase(const glm::dvec3 &focus)
{
    const glm::dvec3 delta = focus - m_scene->GetOrigin();
    if (std::sqrt(delta.x * delta.x + delta.z * delta.z) <= m_budget.rebaseDistance)
    {
        return;
    }
    // Snap to a cell corner: cell roots then sit at whole multiples of the cell size,
    // which floats represent exactly.
    const WorldDescription &description = m_world->description;
    const double size = description.cellSize;
    glm::dvec3 newOrigin = description.origin;
    newOrigin.x += std::round((focus.x - description.origin.x) / size) * size;
    newOrigin.z += std::round((focus.z - description.origin.z) / size) * size;
    newOrigin.y = m_scene->GetOrigin().y;
    GENI_LOG_DEBUG("World origin moves to (%.0f, %.0f, %.0f)", newOrigin.x, newOrigin.y, newOrigin.z);
    m_scene->ShiftOrigin(newOrigin);
}

void WorldStreamer::StartUnload(Cell &cell)
{
    if (cell.state == CellState::Loaded)
    {
        GameObject *root = FindObject(cell.root);
        for (auto &[id, listener] : m_cellListeners)
        {
            (void)id;
            listener(cell.info->x, cell.info->z, root, false);
        }
    }
    cell.instantiation.reset();
    cell.scene.Reset();
    cell.prefabs.clear();
    if (GameObject *root = FindObject(cell.root))
    {
        root->SetActive(false);
        m_dying.push_back(cell.root);
    }
    cell.root = 0;
    cell.state = CellState::Unloaded;
    ++m_stats.cellsUnloadedTotal;
    m_unloadedSinceGarbage = true;
}

void WorldStreamer::StepUnloading(uint32_t &budget)
{
    while (!m_dying.empty() && budget > 0)
    {
        GameObject *root = FindObject(m_dying.back());
        if (!root)
        {
            m_dying.pop_back();
            continue;
        }
        const auto &children = root->GetChildren();
        if (children.empty())
        {
            m_scene->DestroyObject(root);
            m_dying.pop_back();
            --budget;
            continue;
        }
        GameObject *child = children.back().get();
        const uint32_t cost = static_cast<uint32_t>(1 + CountDescendants(*child));
        m_scene->DestroyObject(child);
        budget = budget > cost ? budget - cost : 0;
    }
}

void WorldStreamer::StartLoad(Cell &cell)
{
    cell.scene = m_assets->LoadScene(cell.info->scene, true);
    cell.prefabs.clear();
    for (const AssetID &prefab : cell.info->prefabs)
    {
        cell.prefabs.push_back(m_assets->LoadPrefab(prefab, true));
    }
    cell.state = CellState::Loading;
    ++m_stats.cellLoadsStarted;
}

bool WorldStreamer::AssetsReady(GameObject &root) const
{
    auto pending = [this](AssetID id) { return id.IsValid() && m_assets->GetState(id) == AssetLoadState::Pending; };
    if (auto *mesh = root.GetComponent<MeshComponent>())
    {
        if (pending(mesh->GetMeshID()) || pending(mesh->GetMaterialID()))
        {
            return false;
        }
    }
    if (auto *skinned = root.GetComponent<SkinnedMeshComponent>())
    {
        if (pending(skinned->GetMeshID()) || pending(skinned->GetMaterialID()))
        {
            return false;
        }
    }
    for (const auto &child : root.GetChildren())
    {
        if (!AssetsReady(*child))
        {
            return false;
        }
    }
    return true;
}

// Advances one cell's load. Returns true when it used part of the node budget.
bool WorldStreamer::StepLoading(Cell &cell, uint32_t &budget)
{
    const WorldDescription &description = m_world->description;
    if (cell.state == CellState::Loading)
    {
        if (cell.scene.IsPending())
        {
            return false;
        }
        for (const auto &prefab : cell.prefabs)
        {
            if (prefab.IsPending())
            {
                return false;
            }
        }
        GameObject *root = m_scene->CreateObject(CellName("Cell", cell.info->x, cell.info->z));
        root->SetActive(false);
        root->SetPosition(m_scene->ToScene(description.CellCorner(cell.info->x, cell.info->z)));
        cell.root = root->GetID();
        if (!cell.scene.IsReady())
        {
            GENI_LOG_ERROR(
                "world cell %d,%d: scene %s did not load: %s", cell.info->x, cell.info->z,
                cell.info->scene.ToHex().c_str(), cell.scene.GetError().c_str()
            );
            // An empty, loaded cell: retrying every frame would not help.
            cell.scene.Reset();
            cell.prefabs.clear();
            cell.state = CellState::WaitingForAssets;
            return false;
        }
        SerializationContext context{m_scene, m_assets};
        cell.instantiation = std::make_unique<SceneInstantiation>(*m_scene, cell.scene->document, context, false);
        cell.instantiation->SetAttachParent(root);
        cell.state = CellState::Instantiating;
    }

    if (cell.state == CellState::Instantiating)
    {
        if (budget == 0)
        {
            return false;
        }
        GameObject *root = FindObject(cell.root);
        const size_t before = root ? CountDescendants(*root) : 0;
        const auto status = cell.instantiation->Step(budget);
        const size_t after = root ? CountDescendants(*root) : 0;
        // Objects created this step, at least one unit of work.
        const uint32_t used = static_cast<uint32_t>(std::max<size_t>(1, after > before ? after - before : 1));
        budget = budget > used ? budget - used : 0;
        if (status == SceneInstantiation::Status::InProgress)
        {
            return true;
        }
        if (status == SceneInstantiation::Status::Failed)
        {
            GENI_LOG_ERROR("world cell %d,%d: %s", cell.info->x, cell.info->z, cell.instantiation->GetError().c_str());
        }
        // The document is not needed any more; let garbage collection have it.
        cell.instantiation.reset();
        cell.scene.Reset();
        cell.prefabs.clear();
        cell.state = CellState::WaitingForAssets;
        return true;
    }

    if (cell.state == CellState::WaitingForAssets)
    {
        GameObject *root = FindObject(cell.root);
        if (root && !AssetsReady(*root))
        {
            return false;
        }
        if (root)
        {
            root->SetActive(true);
        }
        cell.state = CellState::Loaded;
        for (auto &[id, listener] : m_cellListeners)
        {
            (void)id;
            listener(cell.info->x, cell.info->z, root, true);
        }
    }
    return false;
}

void WorldStreamer::DropHlod(Cell &cell)
{
    if (GameObject *hlod = FindObject(cell.hlodRoot))
    {
        m_scene->DestroyObject(hlod);
    }
    cell.hlodRoot = 0;
    cell.hlodScene.Reset();
    cell.hlodFailed = false;
}

void WorldStreamer::UpdateHlod(Cell &cell)
{
    if (!cell.hlodWanted)
    {
        DropHlod(cell);
        return;
    }
    if (cell.hlodRoot == 0 && cell.hlodScene.IsReady())
    {
        const WorldDescription &description = m_world->description;
        GameObject *root = m_scene->CreateObject(CellName("HLOD", cell.info->x, cell.info->z));
        root->SetPosition(m_scene->ToScene(description.CellCorner(cell.info->x, cell.info->z)));
        SerializationContext context{m_scene, m_assets};
        std::string error;
        if (!SceneSerializer::InstantiateSubtree(*m_scene, cell.hlodScene->document, context, root, nullptr, &error))
        {
            GENI_LOG_ERROR("world cell %d,%d: HLOD: %s", cell.info->x, cell.info->z, error.c_str());
            cell.hlodFailed = true;
        }
        cell.hlodRoot = root->GetID();
        cell.hlodScene.Reset();
    }
    else if (cell.hlodScene.HasFailed() && !cell.hlodFailed)
    {
        GENI_LOG_ERROR(
            "world cell %d,%d: HLOD scene did not load: %s", cell.info->x, cell.info->z,
            cell.hlodScene.GetError().c_str()
        );
        cell.hlodFailed = true;
    }
    if (GameObject *hlod = FindObject(cell.hlodRoot))
    {
        // The HLOD stands in for the cell until the cell is complete, and again as soon
        // as the cell starts unloading.
        const bool show = cell.state != CellState::Loaded;
        if (hlod->IsActive() != show)
        {
            hlod->SetActive(show);
        }
    }
}

void WorldStreamer::Update()
{
    if (!m_scene || !m_world.IsReady())
    {
        return;
    }
    GENI_PROFILE_FUNCTION();
    const auto start = std::chrono::steady_clock::now();
    const WorldDescription &description = m_world->description;

    const glm::dvec3 focus = CurrentFocus();
    Rebase(focus);
    m_stats.focus = focus;
    m_stats.origin = m_scene->GetOrigin();
    m_stats.originShifts = m_scene->GetOriginShiftCount();
    m_stats.budgetLimited = false;

    // Cells near enough to matter: the square around the focus that holds the HLOD radius.
    const float reach = m_budget.hlodRadius + m_budget.hysteresis;
    const int x0 =
        std::max(0, static_cast<int>(std::floor((focus.x - reach - description.origin.x) / description.cellSize)));
    const int x1 = std::min(
        description.cellsX - 1,
        static_cast<int>(std::floor((focus.x + reach - description.origin.x) / description.cellSize))
    );
    const int z0 =
        std::max(0, static_cast<int>(std::floor((focus.z - reach - description.origin.z) / description.cellSize)));
    const int z1 = std::min(
        description.cellsZ - 1,
        static_cast<int>(std::floor((focus.z + reach - description.origin.z) / description.cellSize))
    );
    for (int z = z0; z <= z1; ++z)
    {
        for (int x = x0; x <= x1; ++x)
        {
            const WorldCellInfo *info = description.FindCell(x, z);
            if (info && DistanceToCell(*info, focus) < std::max(m_budget.loadRadius, m_budget.hlodRadius))
            {
                GetOrAddCell(*info);
            }
        }
    }

    // Distances, unloads and HLOD wishes.
    std::vector<Cell *> wanted;
    for (auto it = m_cells.begin(); it != m_cells.end();)
    {
        Cell &cell = *it->second;
        cell.distance = DistanceToCell(*cell.info, focus);
        const bool keepFull = cell.distance < m_budget.loadRadius + m_budget.hysteresis;
        if (cell.IsResident() && !keepFull)
        {
            StartUnload(cell);
        }
        if (cell.distance < m_budget.hlodRadius && cell.info->hlod.IsValid())
        {
            cell.hlodWanted = true;
        }
        else if (cell.distance >= m_budget.hlodRadius + m_budget.hysteresis)
        {
            cell.hlodWanted = false;
        }
        if (cell.state == CellState::Unloaded && cell.distance < m_budget.loadRadius)
        {
            wanted.push_back(&cell);
        }
        // Forget cells that hold nothing any more.
        if (cell.state == CellState::Unloaded && !cell.hlodWanted && cell.hlodRoot == 0 && !cell.hlodScene.IsValid() &&
            cell.distance >= m_budget.loadRadius)
        {
            it = m_cells.erase(it);
            continue;
        }
        ++it;
    }

    // Budgets: resident nodes from the cooked counts, GPU bytes from the asset manager.
    uint32_t residentNodes = 0;
    uint32_t loading = 0;
    for (const auto &[index, cell] : m_cells)
    {
        if (cell->IsResident())
        {
            residentNodes += cell->info->nodeCount;
        }
        if (cell->state == CellState::Loading)
        {
            ++loading;
        }
    }
    const uint64_t residentBytes = m_assets->GetResidentGpuBytes();

    // Start full loads, nearest first.
    std::sort(wanted.begin(), wanted.end(), [](const Cell *a, const Cell *b) { return a->distance < b->distance; });
    for (Cell *cell : wanted)
    {
        if (loading >= m_budget.maxConcurrentLoads)
        {
            break;
        }
        if (residentNodes + cell->info->nodeCount > m_budget.maxLoadedNodes || residentBytes > m_budget.gpuMemoryBytes)
        {
            m_stats.budgetLimited = true;
            break;
        }
        StartLoad(*cell);
        residentNodes += cell->info->nodeCount;
        ++loading;
    }

    // Start HLOD loads (small files), nearest first, within the GPU budget.
    std::vector<Cell *> active;
    active.reserve(m_cells.size());
    for (auto &[index, cell] : m_cells)
    {
        active.push_back(cell.get());
    }
    std::sort(active.begin(), active.end(), [](const Cell *a, const Cell *b) { return a->distance < b->distance; });
    uint64_t plannedBytes = residentBytes;
    for (Cell *cell : active)
    {
        // Loaded cells get their HLOD too, so it is there the moment they start unloading.
        if (cell->hlodWanted && cell->hlodRoot == 0 && !cell->hlodScene.IsValid() && !cell->hlodFailed)
        {
            if (plannedBytes + cell->info->hlodBytes > m_budget.gpuMemoryBytes)
            {
                m_stats.budgetLimited = true;
                continue;
            }
            cell->hlodScene = m_assets->LoadScene(cell->info->hlod, true);
            plannedBytes += cell->info->hlodBytes;
        }
    }

    // Instantiate, nearest first, within the shared node budget.
    uint32_t nodeBudget = m_budget.nodesPerFrame;
    for (Cell *cell : active)
    {
        if (cell->state == CellState::Loading || cell->state == CellState::Instantiating ||
            cell->state == CellState::WaitingForAssets)
        {
            StepLoading(*cell, nodeBudget);
        }
        UpdateHlod(*cell);
    }

    uint32_t destroyBudget = m_budget.destroyNodesPerFrame;
    StepUnloading(destroyBudget);

    // Garbage: once unloading has finished, now and then.
    ++m_framesSinceGarbage;
    if (m_unloadedSinceGarbage && m_dying.empty() && m_framesSinceGarbage >= 30)
    {
        m_assets->CollectGarbage();
        m_framesSinceGarbage = 0;
        m_unloadedSinceGarbage = false;
    }

    // Statistics.
    m_stats.cellsLoaded = m_stats.cellsLoading = m_stats.hlodCells = 0;
    m_stats.loadedNodes = 0;
    bool late = false;
    for (const auto &[index, cell] : m_cells)
    {
        if (cell->state == CellState::Loaded)
        {
            ++m_stats.cellsLoaded;
        }
        else if (cell->IsResident())
        {
            ++m_stats.cellsLoading;
        }
        if (cell->IsResident())
        {
            m_stats.loadedNodes += cell->info->nodeCount;
        }
        if (const GameObject *hlod = FindObject(cell->hlodRoot); hlod && hlod->IsActive())
        {
            ++m_stats.hlodCells;
        }
        if (cell->state != CellState::Loaded && cell->distance < m_budget.loadRadius * 0.5f)
        {
            late = true;
        }
    }
    if (late)
    {
        ++m_stats.lateFrames;
    }
    m_stats.cellsUnloading = static_cast<uint32_t>(m_dying.size());
    m_stats.residentGpuBytes = m_assets->GetResidentGpuBytes();
    m_stats.peakResidentGpuBytes = std::max(m_stats.peakResidentGpuBytes, m_stats.residentGpuBytes);
    m_stats.peakLoadedNodes = std::max(m_stats.peakLoadedNodes, m_stats.loadedNodes);
    m_stats.updateMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    m_stats.peakUpdateMs = std::max(m_stats.peakUpdateMs, m_stats.updateMs);
}

void WorldStreamer::Flush(int maxFrames)
{
    if (!m_scene)
    {
        return;
    }
    for (int frame = 0; frame < maxFrames; ++frame)
    {
        m_assets->Update();
        Update();
        bool pending = !m_dying.empty() || m_assets->IsLoading();
        for (const auto &[index, cell] : m_cells)
        {
            if (cell->state == CellState::Loading || cell->state == CellState::Instantiating ||
                cell->state == CellState::WaitingForAssets)
            {
                pending = true;
            }
            if (cell->hlodWanted && cell->hlodRoot == 0 && !cell->hlodFailed && !m_stats.budgetLimited)
            {
                pending = true;
            }
        }
        if (!pending)
        {
            break;
        }
        std::this_thread::yield();
    }
    // A flush is a loading screen: what it waited for is not a hitch.
    m_stats.lateFrames = 0;
    m_stats.peakUpdateMs = 0.0;
}

} // namespace Geni
