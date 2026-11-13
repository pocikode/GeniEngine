#include "assets/AssetID.h"

#include <array>
#include <cstdio>

namespace Geni
{

const char *AssetTypeName(AssetType type)
{
    switch (type)
    {
    case AssetType::Mesh:
        return "mesh";
    case AssetType::Texture:
        return "texture";
    case AssetType::Material:
        return "material";
    case AssetType::Animation:
        return "animation";
    case AssetType::Scene:
        return "scene";
    case AssetType::Prefab:
        return "prefab";
    case AssetType::World:
        return "world";
    case AssetType::NavTile:
        return "navtile";
    case AssetType::Sound:
        return "sound";
    case AssetType::Unknown:
        break;
    }
    return "unknown";
}

AssetType AssetTypeFromName(std::string_view name)
{
    if (name == "mesh")
        return AssetType::Mesh;
    if (name == "texture")
        return AssetType::Texture;
    if (name == "material")
        return AssetType::Material;
    if (name == "animation")
        return AssetType::Animation;
    if (name == "scene")
        return AssetType::Scene;
    if (name == "prefab")
        return AssetType::Prefab;
    if (name == "world")
        return AssetType::World;
    if (name == "navtile")
        return AssetType::NavTile;
    if (name == "sound")
        return AssetType::Sound;
    return AssetType::Unknown;
}

AssetID AssetID::FromName(std::string_view name)
{
    // FNV-1a, 64-bit. The offset basis and prime are the published constants.
    uint64_t hash = 14695981039346656037ull;
    for (unsigned char c : name)
    {
        hash ^= c;
        hash *= 1099511628211ull;
    }
    // Zero is reserved for "no asset"; a real hash of zero is vanishingly unlikely but
    // remapping it keeps the invariant true for every input.
    return AssetID{hash == 0 ? 1 : hash};
}

AssetID AssetID::FromHex(std::string_view hex)
{
    if (hex.size() != 16)
    {
        return AssetID{};
    }

    uint64_t value = 0;
    for (char c : hex)
    {
        uint32_t digit = 0;
        if (c >= '0' && c <= '9')
            digit = static_cast<uint32_t>(c - '0');
        else if (c >= 'a' && c <= 'f')
            digit = static_cast<uint32_t>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F')
            digit = static_cast<uint32_t>(c - 'A' + 10);
        else
            return AssetID{};
        value = (value << 4) | digit;
    }
    return AssetID{value};
}

std::string AssetID::ToHex() const
{
    std::array<char, 17> text{};
    std::snprintf(text.data(), text.size(), "%016llx", static_cast<unsigned long long>(value));
    return std::string(text.data());
}

} // namespace Geni
