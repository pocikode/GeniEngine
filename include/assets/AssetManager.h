#pragma once

#include "assets/AssetHandle.h"
#include "assets/AssetManifest.h"
#include "assets/CookedFormats.h"
#include "core/Log.h"
#include "render/SpriteAtlas.h"
#include "scene/components/AnimationComponent.h"
#include "world/WorldFormat.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Geni
{

class FileSystem;
class JobSystem;
class Material;
class Mesh;
class Renderer;
class Texture;
struct Ktx2Image;

// All animation clips cooked from one source file (a .ganim asset).
class AnimationAsset : public Asset
{
  public:
    std::vector<std::shared_ptr<AnimationClip>> clips;
};

// A parsed .gscene document. The main thread instantiates it into a Scene with
// SceneSerializer, so no object is created on a worker.
class SceneAsset : public Asset
{
  public:
    nlohmann::json document;
};

// A cooked world's cell table (docs/design/0007).
class WorldAsset : public Asset
{
  public:
    WorldDescription description;
};

// A packed sprite atlas (docs/design/0021).
class AtlasAsset : public Asset
{
  public:
    SpriteAtlasData atlas;
};

struct AssetLoadStats
{
    uint64_t assetsLoaded = 0;
    uint64_t bytesLoaded = 0;
    uint32_t pending = 0;
    uint32_t failed = 0;
};

// Loads cooked assets by stable ID through the cooked manifest. Decoding runs on the job
// system; AssetManager::Update (called by Engine once per frame) creates GPU objects and
// marks the handles ready. A handle can be held and queried before it is ready.
//
// `async = false` loads return a ready (or failed) handle: they decode on the calling
// thread and, if the asset or one of its dependencies is already loading, wait for it.
// Materials load their textures asynchronously and become ready once every texture has
// finished, so no texture is read or decoded on the main thread by Update.
class AssetManager
{
  public:
    void Init(FileSystem *fileSystem, JobSystem *jobs, Renderer *renderer);
    void Shutdown();

    // Reads cooked_manifest.json from the assets folder. Called lazily by the first load.
    bool LoadManifest(std::string *error = nullptr);
    const AssetManifest &GetManifest() const
    {
        return m_manifest;
    }
    void ReloadManifest();
    // The ID the cooker gave the asset cooked from `source` (for example
    // "models/city.gltf" for its scene), or an invalid ID.
    AssetID FindBySource(AssetType type, const std::string &source);

    // Runs main-thread finalizers and drains the job system's main queue. Called by
    // Engine::Simulate.
    void Update();
    // Runs Update until no load is pending. For startup and tests.
    void WaitForAll();

    // Drops cached assets that nothing references any more: no handle other than the
    // cache's own and no shared_ptr to the object. GPU objects are released through the
    // device's deferred destruction. Returns how many assets were released. Streaming
    // calls this when it unloads a region; Engine also calls it periodically.
    size_t CollectGarbage();
    // GPU bytes of every loaded mesh and texture: what streaming budgets
    // (docs/design/0007) are checked against.
    uint64_t GetResidentGpuBytes() const;
    size_t GetCachedAssetCount() const
    {
        return m_slots.size();
    }

    bool IsLoading() const;
    // NotLoaded when the asset is not in the cache.
    AssetLoadState GetState(AssetID id) const;
    AssetLoadStats GetStats() const;

    AssetHandle<Mesh> LoadMesh(AssetID id, bool async = false);
    AssetHandle<Texture> LoadTexture(AssetID id, bool async = false);
    AssetHandle<Material> LoadMaterial(AssetID id, bool async = false);
    AssetHandle<AnimationAsset> LoadAnimation(AssetID id, bool async = false);
    AssetHandle<SceneAsset> LoadScene(AssetID id, bool async = false);
    // A .gprefab document (the scene format with one root).
    AssetHandle<SceneAsset> LoadPrefab(AssetID id, bool async = false);
    AssetHandle<WorldAsset> LoadWorld(AssetID id, bool async = false);
    AssetHandle<AtlasAsset> LoadAtlas(AssetID id, bool async = false);

    // The file of a cooked asset of `type`, for systems that read raw cooked data (a
    // world's navigation tiles); empty when the manifest has no such asset.
    std::filesystem::path FindCookedFile(AssetID id, AssetType type);

    // Makes a scene or prefab file usable before the next cook lists it: `sourcePath` is
    // relative to the assets folder. Returns the ID the cooker will give the file. The
    // registration survives ReloadManifest.
    AssetID RegisterSourceAsset(AssetType type, const std::string &sourcePath);

    // Hot reload (docs/design/0006). Reloads one loaded asset from its file and swaps the
    // new object into the same slot, so every handle sees it; a file that fails to decode
    // keeps the old object. Main thread, synchronous.
    bool Reload(AssetID id);
    // Reloads every loaded asset whose file's modification time changed since it was
    // read (re-reading the manifest first). Returns the IDs that were reloaded.
    std::vector<AssetID> ReloadChanged();

    // Direct-path loaders for tests and tools. The ID is derived from the path.
    AssetHandle<Mesh> LoadMeshPath(const std::string &path, bool async = false);
    AssetHandle<Texture> LoadTexturePath(const std::string &path, bool async = false);

  private:
    template <typename T> std::shared_ptr<Detail::AssetSlot<T>> GetOrCreateSlot(AssetID id, AssetType type)
    {
        auto it = m_slots.find(id.value);
        if (it != m_slots.end())
        {
            return std::dynamic_pointer_cast<Detail::AssetSlot<T>>(it->second);
        }

        auto slot = std::make_shared<Detail::AssetSlot<T>>();
        slot->id = id;
        slot->type = type;
        m_slots[id.value] = slot;
        return slot;
    }

    void Fail(Detail::AssetSlotBase &slot, const std::string &message);
    void MarkReady(Detail::AssetSlotBase &slot);

    // The one load path. Reads `path` and runs `decode` (on a worker when `async`), then
    // `finish` on the main thread. `Data` is what decode produces.
    template <typename T, typename Data>
    AssetHandle<T> StartLoad(
        AssetID id, AssetType type, const std::string &path,
        const std::string &source, bool async,
        bool (*decode)(const std::vector<uint8_t> &, Data &, std::string *),
        void (AssetManager::*finish)(const std::shared_ptr<Detail::AssetSlot<T>> &, Data &, const std::string &)
    );
    // For an `async = false` load: runs jobs and finalizers until `slot` is no longer pending.
    void WaitForSlot(const Detail::AssetSlotBase &slot);

    void FinishMesh(
        const std::shared_ptr<Detail::AssetSlot<Mesh>> &slot, cooked::CookedMeshData &data, const std::string &source
    );
    void FinishTexture(const std::shared_ptr<Detail::AssetSlot<Texture>> &slot, Ktx2Image &image, const std::string &source);
    void FinishMaterial(
        const std::shared_ptr<Detail::AssetSlot<Material>> &slot, cooked::CookedMaterialData &data,
        const std::string &source
    );
    void FinishAnimation(
        const std::shared_ptr<Detail::AssetSlot<AnimationAsset>> &slot, std::vector<cooked::CookedAnimationData> &data,
        const std::string &source
    );
    void FinishScene(
        const std::shared_ptr<Detail::AssetSlot<SceneAsset>> &slot, nlohmann::json &document, const std::string &source
    );
    void FinishWorld(
        const std::shared_ptr<Detail::AssetSlot<WorldAsset>> &slot, WorldDescription &world, const std::string &source
    );
    void FinishAtlas(
        const std::shared_ptr<Detail::AssetSlot<AtlasAsset>> &slot, SpriteAtlasData &atlas, const std::string &source
    );

    // A material whose object exists but whose textures are still loading.
    struct PendingMaterial
    {
        std::shared_ptr<Detail::AssetSlot<Material>> slot;
        std::shared_ptr<Material> material;
        std::vector<std::pair<std::string, AssetHandle<Texture>>> textures;
        std::string source;
    };
    bool TryCompleteMaterial(PendingMaterial &pending);
    void UpdatePendingMaterials();
    // Points every loaded material that used `previous` at the reloaded texture.
    void RebindTexture(const Texture *previous, const std::shared_ptr<Texture> &replacement);

    std::string ChooseTexturePath(const AssetManifestEntry &entry) const;
    const AssetManifestEntry *FindEntry(AssetID id);
    std::filesystem::path Resolve(const std::string &path) const;

    FileSystem *m_fileSystem = nullptr;
    JobSystem *m_jobs = nullptr;
    Renderer *m_renderer = nullptr;
    AssetManifest m_manifest;
    bool m_manifestTried = false;
    std::vector<AssetManifestEntry> m_registered;

    std::unordered_map<uint64_t, std::shared_ptr<Detail::AssetSlotBase>> m_slots;
    std::vector<PendingMaterial> m_pendingMaterials;
    std::atomic<uint64_t> m_assetsLoaded{0};
    std::atomic<uint64_t> m_bytesLoaded{0};
    std::atomic<uint32_t> m_pending{0};
    std::atomic<uint32_t> m_failed{0};
};

} // namespace Geni
