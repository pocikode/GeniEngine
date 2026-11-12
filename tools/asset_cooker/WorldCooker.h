#pragma once

#include "assets/AssetID.h"

#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>

// Cooks a .gworld into a `world` asset and builds each cell's HLOD (docs/design/0007).
namespace Geni::cook
{

struct WorldCookAsset
{
    AssetID id;
    AssetType type = AssetType::Unknown;
    std::string source;
    std::string path;
};

// What the world cooker needs from the cook it runs in: cells and prefabs are cooked as
// scenes before worlds, so their IDs and the meshes they use are known by then.
class WorldCookHost
{
  public:
    virtual ~WorldCookHost() = default;
    virtual const std::filesystem::path &SourceRoot() const = 0;
    virtual const std::filesystem::path &OutputRoot() const = 0;
    // The name a source file's IDs derive from (its path, or its .meta GUID).
    virtual std::string RootName(const std::string &relative) = 0;
    virtual bool AddAsset(const WorldCookAsset &asset) = 0;
    // Output-relative path of a cooked asset of this cook, or empty.
    virtual std::string FindCookedPath(AssetID id) const = 0;
    // A prefab document of this cook, or nullptr.
    virtual const nlohmann::json *FindPrefab(AssetID id) const = 0;
    virtual bool Fail(const std::string &message) = 0;
};

// Objects smaller than this (bounding sphere diameter, metres) are left out of an HLOD.
constexpr float HlodMinimumSize = 1.5f;
// Objects smaller than this with more than a box's worth of triangles (trees, props) go
// into an HLOD as their bounding box: a few pixels at HLOD distance either way.
constexpr float HlodProxySize = 12.0f;

bool CookWorld(WorldCookHost &host, const std::string &relative);

} // namespace Geni::cook
