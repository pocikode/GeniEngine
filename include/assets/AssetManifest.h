#pragma once

#include "assets/AssetID.h"

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace Geni
{

// One logical asset as recorded by the cooker. For a texture, `path` is the RGBA8
// fallback; `bcPath` (BC7, or BC5 for normal maps) and `astcPath` are chosen at load time
// by what the device supports.
struct AssetManifestEntry
{
    AssetID id;
    AssetType type = AssetType::Unknown;
    std::string source;
    std::string path;
    std::string bcPath;
    std::string astcPath;
};

// cooked_manifest.json: the only place asset paths and stable IDs meet.
class AssetManifest
{
  public:
    bool LoadFromFile(const std::filesystem::path &path, std::string *error = nullptr);
    void Clear();
    // Adds an entry, or replaces the one with the same ID. Used for assets created
    // after the last cook (the editor saving a new scene or prefab).
    void Register(const AssetManifestEntry &entry);

    const AssetManifestEntry *Find(AssetID id) const;
    // Looks an asset up by the source the cooker recorded (for example
    // "models/city.gltf" for its scene). IDs do not have to be derived from paths, so
    // code that starts from a file name uses this instead of AssetID::FromName.
    const AssetManifestEntry *FindBySource(AssetType type, const std::string &source) const;
    const std::vector<AssetManifestEntry> &GetEntries() const
    {
        return m_entries;
    }
    bool IsLoaded() const
    {
        return m_loaded;
    }

  private:
    std::vector<AssetManifestEntry> m_entries;
    std::unordered_map<uint64_t, size_t> m_byID;
    bool m_loaded = false;
};

} // namespace Geni
