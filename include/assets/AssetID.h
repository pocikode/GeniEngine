#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace Geni
{

enum class AssetType
{
    Unknown,
    Mesh,
    Texture,
    Material,
    Animation,
    Scene,
    // A .gprefab: a scene-format subtree that scenes instantiate (docs/design/0006).
    Prefab,
    // A cooked .gworld: the cell table of a streamed world (docs/design/0007).
    World,
    // A world cell's Detour navigation tile, cooked offline (docs/design/0014).
    NavTile
};

const char *AssetTypeName(AssetType type);
AssetType AssetTypeFromName(std::string_view name);

// A stable identity for an asset: the 64-bit FNV-1a hash of its logical name, written as
// 16 lowercase hex digits. The logical name is what the cooker records as the source
// (for example "mesh:models/bar.gltf:0:0"), so IDs survive moving a build folder and are
// the same on every platform. A zero value means "no asset".
struct AssetID
{
    uint64_t value = 0;

    bool IsValid() const
    {
        return value != 0;
    }

    static AssetID FromName(std::string_view name);
    static AssetID FromHex(std::string_view hex);
    std::string ToHex() const;

    bool operator==(const AssetID &other) const
    {
        return value == other.value;
    }
    bool operator!=(const AssetID &other) const
    {
        return value != other.value;
    }
    bool operator<(const AssetID &other) const
    {
        return value < other.value;
    }
};

struct AssetIDHash
{
    size_t operator()(const AssetID &id) const
    {
        // The low bits of an FNV hash are already well mixed; fold the high ones in so
        // tables with many assets stay balanced.
        return static_cast<size_t>(id.value ^ (id.value >> 32));
    }
};

} // namespace Geni
