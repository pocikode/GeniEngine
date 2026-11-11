#include "assets/AssetManager.h"
#include "assets/Ktx2.h"
#include "core/JobSystem.h"
#include "graphics/Texture.h"
#include "io/FileSystem.h"
#include "render/Material.h"
#include "render/Mesh.h"
#include "render/Renderer.h"
#include "render/Shader.h"
#include "rhi/Device.h"

#include <algorithm>
#include <type_traits>
#include <utility>

namespace Geni
{

namespace
{

bool DecodeScene(const std::vector<uint8_t> &bytes, nlohmann::json &out, std::string *error)
{
    try
    {
        out = nlohmann::json::parse(bytes.begin(), bytes.end());
        return true;
    }
    catch (const std::exception &e)
    {
        if (error)
        {
            *error = e.what();
        }
        return false;
    }
}

int64_t FileTime(const std::filesystem::path &path)
{
    std::error_code ec;
    const auto time = std::filesystem::last_write_time(path, ec);
    return ec ? 0 : static_cast<int64_t>(time.time_since_epoch().count());
}

} // namespace

void AssetManager::Init(FileSystem *fileSystem, JobSystem *jobs, Renderer *renderer)
{
    m_fileSystem = fileSystem;
    m_jobs = jobs;
    m_renderer = renderer;
    m_slots.clear();
    m_pendingMaterials.clear();
    m_manifest.Clear();
    m_registered.clear();
    m_manifestTried = false;
    m_assetsLoaded.store(0, std::memory_order_release);
    m_bytesLoaded.store(0, std::memory_order_release);
    m_pending.store(0, std::memory_order_release);
    m_failed.store(0, std::memory_order_release);
}

void AssetManager::Shutdown()
{
    if (m_jobs)
    {
        m_jobs->WaitIdle();
        m_jobs->PumpMainThread();
    }
    m_pendingMaterials.clear();
    m_slots.clear();
    m_manifest.Clear();
    m_registered.clear();
    m_manifestTried = false;
    m_fileSystem = nullptr;
    m_jobs = nullptr;
    m_renderer = nullptr;
}

void AssetManager::ReloadManifest()
{
    m_manifestTried = false;
    m_manifest.Clear();
}

bool AssetManager::LoadManifest(std::string *error)
{
    m_manifestTried = true;
    if (!m_fileSystem)
    {
        if (error)
        {
            *error = "AssetManager has no FileSystem";
        }
        return false;
    }
    const auto path = m_fileSystem->ResolveAssetPath("cooked_manifest.json");
    std::string message;
    const bool loaded = m_manifest.LoadFromFile(path, &message);
    for (const auto &entry : m_registered)
    {
        m_manifest.Register(entry);
    }
    if (!loaded)
    {
        if (error)
        {
            *error = message;
        }
        return false;
    }
    return true;
}

const AssetManifestEntry *AssetManager::FindEntry(AssetID id)
{
    if (!m_manifestTried)
    {
        std::string error;
        if (!LoadManifest(&error))
        {
            GENI_LOG_WARN("no cooked manifest: %s", error.c_str());
        }
    }
    return m_manifest.Find(id);
}

AssetID AssetManager::FindBySource(AssetType type, const std::string &source)
{
    FindEntry(AssetID{}); // loads the manifest on first use
    const AssetManifestEntry *entry = m_manifest.FindBySource(type, source);
    return entry ? entry->id : AssetID{};
}

std::filesystem::path AssetManager::Resolve(const std::string &path) const
{
    return m_fileSystem ? m_fileSystem->ResolveAssetPath(path) : std::filesystem::path(path);
}

void AssetManager::Fail(Detail::AssetSlotBase &slot, const std::string &message)
{
    slot.error = message;
    slot.state.store(AssetLoadState::Failed, std::memory_order_release);
    m_failed.fetch_add(1, std::memory_order_acq_rel);
    GENI_LOG_ERROR("asset load failed: %s", message.c_str());
}

void AssetManager::MarkReady(Detail::AssetSlotBase &slot)
{
    slot.state.store(AssetLoadState::Ready, std::memory_order_release);
    m_assetsLoaded.fetch_add(1, std::memory_order_acq_rel);
}

void AssetManager::Update()
{
    if (m_jobs)
    {
        m_jobs->PumpMainThread();
    }
    UpdatePendingMaterials();
}

void AssetManager::WaitForSlot(const Detail::AssetSlotBase &slot)
{
    while (slot.state.load(std::memory_order_acquire) == AssetLoadState::Pending)
    {
        const bool jobsRunning = m_jobs && m_jobs->GetOutstandingJobs() > 0;
        if (jobsRunning)
        {
            m_jobs->WaitIdle();
        }
        const size_t pendingMaterials = m_pendingMaterials.size();
        Update();
        if (!jobsRunning && m_pendingMaterials.size() == pendingMaterials &&
            slot.state.load(std::memory_order_acquire) == AssetLoadState::Pending)
        {
            // Nothing is running that could finish this load; do not spin forever.
            GENI_LOG_ERROR("asset %s is pending with no work left to finish it", slot.id.ToHex().c_str());
            return;
        }
    }
}

void AssetManager::WaitForAll()
{
    while (m_pending.load(std::memory_order_acquire) > 0)
    {
        const bool jobsRunning = m_jobs && m_jobs->GetOutstandingJobs() > 0;
        if (jobsRunning)
        {
            m_jobs->WaitIdle();
        }
        const uint32_t before = m_pending.load(std::memory_order_acquire);
        Update();
        if (!jobsRunning && m_pending.load(std::memory_order_acquire) == before)
        {
            GENI_LOG_ERROR("%u asset loads are pending with no work left to finish them", before);
            return;
        }
    }
    Update();
}

bool AssetManager::IsLoading() const
{
    return m_pending.load(std::memory_order_acquire) > 0;
}

AssetLoadStats AssetManager::GetStats() const
{
    AssetLoadStats stats;
    stats.assetsLoaded = m_assetsLoaded.load(std::memory_order_acquire);
    stats.bytesLoaded = m_bytesLoaded.load(std::memory_order_acquire);
    stats.pending = m_pending.load(std::memory_order_acquire);
    stats.failed = m_failed.load(std::memory_order_acquire);
    return stats;
}

size_t AssetManager::CollectGarbage()
{
    // Materials hold their textures, so releasing a material can make its textures
    // unreferenced. Repeat until a pass releases nothing.
    size_t released = 0;
    for (;;)
    {
        size_t pass = 0;
        for (auto it = m_slots.begin(); it != m_slots.end();)
        {
            const auto &slot = it->second;
            const bool unreferenced = slot.use_count() == 1 && slot->ExternalAssetReferences() == 0;
            if (unreferenced && slot->state.load(std::memory_order_acquire) != AssetLoadState::Pending)
            {
                it = m_slots.erase(it);
                ++pass;
            }
            else
            {
                ++it;
            }
        }
        released += pass;
        if (pass == 0)
        {
            return released;
        }
    }
}

std::string AssetManager::ChooseTexturePath(const AssetManifestEntry &entry) const
{
    if (m_renderer && m_renderer->GetDevice())
    {
        auto *device = m_renderer->GetDevice();
        // Desktop first; ASTC is the mobile fallback. M7 will pick per platform tier.
        // Vulkan exposes the BC family as one feature, so BC7 support implies BC5.
        if (!entry.bcPath.empty() && device->IsTextureFormatSupported(rhi::Format::BC7Unorm))
        {
            return entry.bcPath;
        }
        if (!entry.astcPath.empty() && device->IsTextureFormatSupported(rhi::Format::ASTC4x4Unorm))
        {
            return entry.astcPath;
        }
    }
    return entry.path;
}

template <typename T, typename Data>
AssetHandle<T> AssetManager::StartLoad(
    AssetID id, AssetType type, const std::string &path, const std::string &source,
    bool async, bool (*decode)(const std::vector<uint8_t> &, Data &, std::string *),
    void (AssetManager::*finish)(const std::shared_ptr<Detail::AssetSlot<T>> &, Data &, const std::string &)
)
{
    auto slot = GetOrCreateSlot<T>(id, type);
    if (!slot)
    {
        GENI_LOG_ERROR("asset %s was already loaded as another type", id.ToHex().c_str());
        return AssetHandle<T>();
    }
    AssetHandle<T> handle(slot);
    const AssetLoadState state = slot->state.load(std::memory_order_acquire);
    if (state == AssetLoadState::Pending && !async)
    {
        WaitForSlot(*slot);
    }
    if (state != AssetLoadState::NotLoaded)
    {
        return handle;
    }

    // An empty path means the manifest has no entry of this type for the ID.
    if (path.empty())
    {
        Fail(*slot, std::string(AssetTypeName(type)) + " asset " + id.ToHex() + " is not in the cooked manifest");
        return handle;
    }

    slot->state.store(AssetLoadState::Pending, std::memory_order_release);
    m_pending.fetch_add(1, std::memory_order_acq_rel);
    const auto fullPath = Resolve(path);
    slot->path = path;
    slot->source = source;
    slot->fileTime = FileTime(fullPath);
    // Hot reload: decode the file again and let the same finisher replace slot->asset.
    // The old object stays in the slot until the new one is ready, and for good when
    // the file does not decode.
    slot->reload = [this, weak = std::weak_ptr<Detail::AssetSlot<T>>(slot), decode, finish]() -> bool {
        auto self = weak.lock();
        if (!self)
        {
            return false;
        }
        // A load that failed (a broken file since fixed) is retried the same way.
        const AssetLoadState before = self->state.load(std::memory_order_acquire);
        if (before != AssetLoadState::Ready && before != AssetLoadState::Failed)
        {
            return false;
        }
        const auto file = Resolve(self->path);
        std::vector<uint8_t> bytes;
        Data data;
        std::string error;
        if (!cooked::ReadFile(file, bytes))
        {
            GENI_LOG_ERROR("asset reload: cannot read %s", file.string().c_str());
            return false;
        }
        self->fileTime = FileTime(file);
        if (!decode(bytes, data, &error))
        {
            GENI_LOG_ERROR("asset reload: %s: %s (keeping the loaded version)", self->source.c_str(), error.c_str());
            return false;
        }
        const std::shared_ptr<T> previous = self->asset;
        m_pending.fetch_add(1, std::memory_order_acq_rel);
        (this->*finish)(self, data, self->source);
        if (self->state.load(std::memory_order_acquire) == AssetLoadState::Failed)
        {
            // The finisher could not build the new object; an old one is still good.
            if (previous)
            {
                self->asset = previous;
                self->state.store(AssetLoadState::Ready, std::memory_order_release);
            }
            return false;
        }
        self->error.clear();
        if constexpr (std::is_same_v<T, Texture>)
        {
            RebindTexture(previous.get(), self->asset);
        }
        return true;
    };

    // Runs on a worker for async loads and returns the main-thread completion.
    auto work = [this, slot, fullPath, source, decode, finish]() -> std::function<void()> {
        auto bytes = std::make_shared<std::vector<uint8_t>>();
        auto data = std::make_shared<Data>();
        std::string error;
        bool ok = cooked::ReadFile(fullPath, *bytes);
        if (!ok)
        {
            error = "cannot read " + fullPath.string();
        }
        else if ((ok = decode(*bytes, *data, &error)))
        {
            m_bytesLoaded.fetch_add(bytes->size(), std::memory_order_acq_rel);
        }
        if (!ok)
        {
            return [this, slot, source, error] {
                Fail(*slot, source + ": " + error);
                m_pending.fetch_sub(1, std::memory_order_acq_rel);
            };
        }
        return [this, slot, data, source, finish] { (this->*finish)(slot, *data, source); };
    };

    if (async && m_jobs)
    {
        m_jobs->Schedule([this, work] { m_jobs->PostToMain(work()); }, "load asset");
    }
    else
    {
        // Decode and finish here, without pumping unrelated main-thread callbacks.
        work()();
        WaitForSlot(*slot); // a material may still wait for its textures
    }
    return handle;
}

AssetHandle<Mesh> AssetManager::LoadMesh(AssetID id, bool async)
{
    const AssetManifestEntry *entry = FindEntry(id);
    const bool valid = entry && entry->type == AssetType::Mesh;
    return StartLoad<Mesh, cooked::CookedMeshData>(
        id, AssetType::Mesh, valid ? entry->path : std::string(), valid ? entry->source : std::string(), async,
        &cooked::ReadMesh, &AssetManager::FinishMesh
    );
}

AssetHandle<Mesh> AssetManager::LoadMeshPath(const std::string &path, bool async)
{
    return StartLoad<Mesh, cooked::CookedMeshData>(
        AssetID::FromName("mesh-path:" + path), AssetType::Mesh, path, path, async, &cooked::ReadMesh,
        &AssetManager::FinishMesh
    );
}

AssetHandle<Texture> AssetManager::LoadTexture(AssetID id, bool async)
{
    const AssetManifestEntry *entry = FindEntry(id);
    const bool valid = entry && entry->type == AssetType::Texture;
    return StartLoad<Texture, Ktx2Image>(
        id, AssetType::Texture, valid ? ChooseTexturePath(*entry) : std::string(),
        valid ? entry->source : std::string(), async, &ReadKtx2, &AssetManager::FinishTexture
    );
}

AssetHandle<Texture> AssetManager::LoadTexturePath(const std::string &path, bool async)
{
    return StartLoad<Texture, Ktx2Image>(
        AssetID::FromName("texture-path:" + path), AssetType::Texture, path, path, async, &ReadKtx2,
        &AssetManager::FinishTexture
    );
}

AssetHandle<Material> AssetManager::LoadMaterial(AssetID id, bool async)
{
    const AssetManifestEntry *entry = FindEntry(id);
    const bool valid = entry && entry->type == AssetType::Material;
    return StartLoad<Material, cooked::CookedMaterialData>(
        id, AssetType::Material, valid ? entry->path : std::string(), valid ? entry->source : std::string(),
        async, &cooked::ReadMaterial, &AssetManager::FinishMaterial
    );
}

AssetHandle<AnimationAsset> AssetManager::LoadAnimation(AssetID id, bool async)
{
    const AssetManifestEntry *entry = FindEntry(id);
    const bool valid = entry && entry->type == AssetType::Animation;
    return StartLoad<AnimationAsset, std::vector<cooked::CookedAnimationData>>(
        id, AssetType::Animation, valid ? entry->path : std::string(), valid ? entry->source : std::string(),
        async, &cooked::ReadAnimations, &AssetManager::FinishAnimation
    );
}

AssetHandle<SceneAsset> AssetManager::LoadScene(AssetID id, bool async)
{
    const AssetManifestEntry *entry = FindEntry(id);
    const bool valid = entry && entry->type == AssetType::Scene;
    return StartLoad<SceneAsset, nlohmann::json>(
        id, AssetType::Scene, valid ? entry->path : std::string(), valid ? entry->source : std::string(), async,
        &DecodeScene, &AssetManager::FinishScene
    );
}

AssetHandle<SceneAsset> AssetManager::LoadPrefab(AssetID id, bool async)
{
    const AssetManifestEntry *entry = FindEntry(id);
    const bool valid = entry && entry->type == AssetType::Prefab;
    return StartLoad<SceneAsset, nlohmann::json>(
        id, AssetType::Prefab, valid ? entry->path : std::string(), valid ? entry->source : std::string(), async,
        &DecodeScene, &AssetManager::FinishScene
    );
}

AssetID AssetManager::RegisterSourceAsset(AssetType type, const std::string &sourcePath)
{
    FindEntry(AssetID{}); // the manifest first, so the registration lands on top of it
    std::string normalized = sourcePath;
    std::replace(normalized.begin(), normalized.end(), '\\', '/');

    // The cooker's rule (tools/asset_cooker/Cooker.h): a .meta sidecar's GUID names the
    // file, its path otherwise.
    std::string rootName = normalized;
    std::vector<uint8_t> meta;
    if (cooked::ReadFile(Resolve(normalized + ".meta"), meta))
    {
        const auto document = nlohmann::json::parse(meta.begin(), meta.end(), nullptr, false);
        if (document.is_object() && AssetID::FromHex(document.value("guid", std::string())).IsValid())
        {
            rootName = "guid:" + AssetID::FromHex(document.value("guid", std::string())).ToHex();
        }
    }

    AssetManifestEntry entry;
    entry.id = AssetID::FromName(std::string(AssetTypeName(type)) + ":" + rootName);
    entry.type = type;
    entry.source = normalized;
    entry.path = normalized;
    for (auto &registered : m_registered)
    {
        if (registered.id == entry.id)
        {
            registered = entry;
            m_manifest.Register(entry);
            return entry.id;
        }
    }
    m_registered.push_back(entry);
    m_manifest.Register(entry);
    return entry.id;
}

bool AssetManager::Reload(AssetID id)
{
    auto it = m_slots.find(id.value);
    if (it == m_slots.end() || !it->second->reload)
    {
        return false;
    }
    // Hold the slot: the reload runs finishers that may touch the cache.
    const std::shared_ptr<Detail::AssetSlotBase> slot = it->second;
    if (const AssetManifestEntry *entry = m_manifest.Find(id))
    {
        slot->path = entry->type == AssetType::Texture ? ChooseTexturePath(*entry) : entry->path;
        slot->source = entry->source;
    }
    return slot->reload();
}

std::vector<AssetID> AssetManager::ReloadChanged()
{
    ReloadManifest();
    FindEntry(AssetID{});

    std::vector<AssetID> changed;
    for (const auto &[value, slot] : m_slots)
    {
        const AssetLoadState state = slot->state.load(std::memory_order_acquire);
        if ((state != AssetLoadState::Ready && state != AssetLoadState::Failed) || !slot->reload)
        {
            continue;
        }
        std::string path = slot->path;
        if (const AssetManifestEntry *entry = m_manifest.Find(slot->id))
        {
            path = entry->type == AssetType::Texture ? ChooseTexturePath(*entry) : entry->path;
        }
        if (path != slot->path || FileTime(Resolve(path)) != slot->fileTime)
        {
            changed.push_back(slot->id);
        }
    }
    std::vector<AssetID> reloaded;
    for (AssetID id : changed)
    {
        if (Reload(id))
        {
            reloaded.push_back(id);
        }
    }
    // A reloaded material waits for its textures like a first load does.
    WaitForAll();
    return reloaded;
}

void AssetManager::RebindTexture(const Texture *previous, const std::shared_ptr<Texture> &replacement)
{
    if (!previous || previous == replacement.get())
    {
        return;
    }
    for (const auto &[value, slot] : m_slots)
    {
        if (slot->type != AssetType::Material)
        {
            continue;
        }
        if (auto material = std::static_pointer_cast<Detail::AssetSlot<Material>>(slot)->asset)
        {
            material->ReplaceTexture(previous, replacement);
        }
    }
}

void AssetManager::FinishMesh(
    const std::shared_ptr<Detail::AssetSlot<Mesh>> &slot, cooked::CookedMeshData &data, const std::string &source
)
{
    if (!m_renderer || !m_renderer->GetDevice())
    {
        Fail(*slot, "no render device to create mesh " + slot->id.ToHex());
        m_pending.fetch_sub(1, std::memory_order_acq_rel);
        return;
    }

    std::shared_ptr<Mesh> mesh;
    if (data.indices.empty())
    {
        mesh = std::make_shared<Mesh>(data.layout, data.vertices, data.topology);
    }
    else
    {
        mesh = std::make_shared<Mesh>(data.layout, data.vertices, data.indices);
    }
    if (!mesh->GetVertexBuffer().IsValid())
    {
        Fail(*slot, "GPU mesh creation failed for " + source);
        m_pending.fetch_sub(1, std::memory_order_acq_rel);
        return;
    }
    mesh->SetAssetID(slot->id);
    mesh->SetSourcePath(source);
    mesh->SetSkinData(data.jointNames, data.inverseBindMatrices);

    slot->asset = std::move(mesh);
    MarkReady(*slot);
    m_pending.fetch_sub(1, std::memory_order_acq_rel);
}

void AssetManager::FinishTexture(
    const std::shared_ptr<Detail::AssetSlot<Texture>> &slot, Ktx2Image &image, const std::string &source
)
{
    auto texture = std::make_shared<Texture>(image);
    if (!texture->GetHandle().IsValid())
    {
        Fail(*slot, "GPU texture creation failed for " + source);
        m_pending.fetch_sub(1, std::memory_order_acq_rel);
        return;
    }
    texture->SetAssetID(slot->id);
    texture->SetSourcePath(source);
    slot->asset = std::move(texture);
    MarkReady(*slot);
    m_pending.fetch_sub(1, std::memory_order_acq_rel);
}

void AssetManager::FinishMaterial(
    const std::shared_ptr<Detail::AssetSlot<Material>> &slot, cooked::CookedMaterialData &data,
    const std::string &source
)
{
    std::shared_ptr<Shader> shader;
    if (data.shader == "builtin:default")
    {
        shader = m_renderer ? m_renderer->GetDefaultShader() : nullptr;
    }
    else if (data.shader == "builtin:skinned")
    {
        shader = m_renderer ? m_renderer->GetSkinnedShader() : nullptr;
    }
    else
    {
        shader = Shader::Load(data.shader, data.shader);
    }
    if (!shader)
    {
        Fail(*slot, "material " + source + " has no usable shader '" + data.shader + "'");
        m_pending.fetch_sub(1, std::memory_order_acq_rel);
        return;
    }

    auto material = std::make_shared<Material>();
    material->SetShader(shader);
    for (const auto &[name, value] : data.floats)
    {
        material->SetParam(name, value);
    }
    for (const auto &[name, value] : data.float2s)
    {
        material->SetParam(name, value.x, value.y);
    }
    for (const auto &[name, value] : data.float4s)
    {
        material->SetParam(name, value);
    }
    material->SetAssetID(slot->id);
    material->SetSourcePath(source);

    // Textures load like any other asset: decoded on workers, uploaded by Update. The
    // material becomes ready once all of them have finished (or failed).
    PendingMaterial pending;
    pending.slot = slot;
    pending.material = std::move(material);
    pending.source = source;
    for (const auto &[name, id] : data.textures)
    {
        pending.textures.emplace_back(name, LoadTexture(id, true));
    }
    if (!TryCompleteMaterial(pending))
    {
        m_pendingMaterials.push_back(std::move(pending));
    }
}

bool AssetManager::TryCompleteMaterial(PendingMaterial &pending)
{
    for (const auto &[name, texture] : pending.textures)
    {
        if (texture.IsPending())
        {
            return false;
        }
    }
    for (const auto &[name, texture] : pending.textures)
    {
        if (texture.IsReady())
        {
            pending.material->SetParam(name, texture.Shared());
        }
        else
        {
            GENI_LOG_WARN(
                "material %s: texture %s for '%s' did not load", pending.source.c_str(),
                texture.GetID().ToHex().c_str(), name.c_str()
            );
        }
    }
    pending.slot->asset = std::move(pending.material);
    MarkReady(*pending.slot);
    m_pending.fetch_sub(1, std::memory_order_acq_rel);
    return true;
}

void AssetManager::UpdatePendingMaterials()
{
    for (size_t i = 0; i < m_pendingMaterials.size();)
    {
        if (TryCompleteMaterial(m_pendingMaterials[i]))
        {
            m_pendingMaterials[i] = std::move(m_pendingMaterials.back());
            m_pendingMaterials.pop_back();
        }
        else
        {
            ++i;
        }
    }
}

void AssetManager::FinishAnimation(
    const std::shared_ptr<Detail::AssetSlot<AnimationAsset>> &slot, std::vector<cooked::CookedAnimationData> &data,
    const std::string &source
)
{
    auto asset = std::make_shared<AnimationAsset>();
    asset->SetAssetID(slot->id);
    asset->SetSourcePath(source);
    for (auto &cookedClip : data)
    {
        auto clip = std::make_shared<AnimationClip>();
        clip->name = std::move(cookedClip.name);
        clip->duration = cookedClip.duration;
        clip->looping = cookedClip.looping;
        clip->tracks = std::move(cookedClip.tracks);
        asset->clips.push_back(std::move(clip));
    }
    slot->asset = std::move(asset);
    MarkReady(*slot);
    m_pending.fetch_sub(1, std::memory_order_acq_rel);
}

void AssetManager::FinishScene(
    const std::shared_ptr<Detail::AssetSlot<SceneAsset>> &slot, nlohmann::json &document, const std::string &source
)
{
    auto asset = std::make_shared<SceneAsset>();
    asset->SetAssetID(slot->id);
    asset->SetSourcePath(source);
    // Moved, not copied: a large scene document would otherwise be deep-copied here, on
    // the main thread.
    asset->document = std::move(document);
    slot->asset = std::move(asset);
    MarkReady(*slot);
    m_pending.fetch_sub(1, std::memory_order_acq_rel);
}

} // namespace Geni
