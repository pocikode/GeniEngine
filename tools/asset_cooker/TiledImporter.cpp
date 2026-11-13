#include "TiledImporter.h"

#include <algorithm>
#include <filesystem>
#include <set>

namespace Geni::cook
{

namespace
{

using Json = nlohmann::json;

constexpr uint32_t kGidMask = 0x1FFFFFFFu; // the top three bits are flip flags

bool Fail(std::string *error, const std::string &message)
{
    if (error)
    {
        *error = message;
    }
    return false;
}

std::string Join(const std::string &relativeTo, const std::string &path)
{
    const std::filesystem::path folder = std::filesystem::path(relativeTo).parent_path();
    return (folder / path).lexically_normal().generic_string();
}

bool BoolProperty(const Json &object, const char *name)
{
    if (!object.contains("properties"))
    {
        return false;
    }
    for (const auto &property : object.at("properties"))
    {
        if (property.value("name", std::string()) == name && property.value("type", std::string()) == "bool")
        {
            return property.value("value", false);
        }
    }
    return false;
}

struct Tileset
{
    uint32_t firstGid = 1;
    uint32_t count = 0;
    std::string image;
    int columns = 1;
    int rows = 1;
    std::vector<int> solid;
};

bool ReadTileset(
    const Json &entry, const std::string &mapPath, const std::function<bool(const std::string &, Json &)> &readJson,
    Tileset &out, std::string *error
)
{
    out.firstGid = entry.value("firstgid", 1u);
    Json document = entry;
    std::string base = mapPath;
    if (entry.contains("source"))
    {
        base = Join(mapPath, entry.at("source").get<std::string>());
        if (!readJson(base, document))
        {
            return Fail(error, "cannot read the tileset " + base);
        }
    }
    if (!document.contains("image"))
    {
        return Fail(error, "tileset without a single image (image collections are not supported)");
    }
    out.image = Join(base, document.at("image").get<std::string>());
    const int tileWidth = document.value("tilewidth", 0);
    const int tileHeight = document.value("tileheight", 0);
    const int imageWidth = document.value("imagewidth", 0);
    const int imageHeight = document.value("imageheight", 0);
    if (tileWidth <= 0 || tileHeight <= 0 || imageWidth <= 0 || imageHeight <= 0)
    {
        return Fail(error, "tileset " + out.image + " without tile and image sizes");
    }
    if (document.value("margin", 0) != 0 || document.value("spacing", 0) != 0)
    {
        return Fail(error, "tileset " + out.image + " has a margin or spacing; export it without");
    }
    out.columns = std::max(1, document.value("columns", imageWidth / tileWidth));
    out.rows = std::max(1, imageHeight / tileHeight);
    out.count = document.value("tilecount", static_cast<uint32_t>(out.columns * out.rows));
    if (document.contains("tiles"))
    {
        for (const auto &tile : document.at("tiles"))
        {
            if (BoolProperty(tile, "solid"))
            {
                out.solid.push_back(tile.value("id", 0));
            }
        }
    }
    return true;
}

} // namespace

bool ImportTiledMap(
    const Json &map, const std::string &mapPath, const std::function<bool(const std::string &, Json &)> &readJson,
    TiledMap &out, std::string *error
)
{
    try
    {
        if (map.value("orientation", std::string()) != "orthogonal")
        {
            return Fail(error, "only orthogonal maps are supported");
        }
        if (map.value("infinite", false))
        {
            return Fail(error, "infinite maps are not supported; untick Infinite in the map properties");
        }
        out.width = map.at("width").get<int>();
        out.height = map.at("height").get<int>();
        const float tileWidth = map.at("tilewidth").get<float>();
        const float tileHeight = map.at("tileheight").get<float>();

        std::vector<Tileset> tilesets;
        for (const auto &entry : map.at("tilesets"))
        {
            Tileset tileset;
            if (!ReadTileset(entry, mapPath, readJson, tileset, error))
            {
                return false;
            }
            tilesets.push_back(std::move(tileset));
        }
        std::sort(tilesets.begin(), tilesets.end(), [](const Tileset &a, const Tileset &b) {
            return a.firstGid < b.firstGid;
        });
        auto tilesetOf = [&](uint32_t gid) -> const Tileset * {
            const Tileset *found = nullptr;
            for (const auto &tileset : tilesets)
            {
                if (gid >= tileset.firstGid)
                {
                    found = &tileset;
                }
            }
            return found;
        };

        bool flipWarned = false;
        // Group layers are flattened in order.
        std::function<bool(const Json &)> visit = [&](const Json &layers) -> bool {
            for (const auto &layer : layers)
            {
                const std::string type = layer.value("type", std::string());
                if (type == "group")
                {
                    if (!visit(layer.at("layers")))
                    {
                        return false;
                    }
                }
                else if (type == "tilelayer")
                {
                    if (!layer.contains("data") || !layer.at("data").is_array())
                    {
                        return Fail(
                            error, "layer " + layer.value("name", std::string()) +
                                       ": tile data must be a CSV array (not base64 or compressed)"
                        );
                    }
                    TiledTileLayer tiles;
                    tiles.name = layer.value("name", std::string("Tiles"));
                    tiles.width = layer.value("width", out.width);
                    tiles.height = layer.value("height", out.height);
                    tiles.collision = BoolProperty(layer, "collision");
                    tiles.visible = layer.value("visible", true);
                    tiles.opacity = layer.value("opacity", 1.0f);
                    const Tileset *used = nullptr;
                    std::set<const Tileset *> others;
                    for (const auto &value : layer.at("data"))
                    {
                        const uint32_t raw = value.get<uint32_t>();
                        const uint32_t gid = raw & kGidMask;
                        if (raw != gid && !flipWarned)
                        {
                            out.warnings.push_back("flipped tiles are drawn unflipped");
                            flipWarned = true;
                        }
                        const Tileset *tileset = gid ? tilesetOf(gid) : nullptr;
                        if (tileset && !used)
                        {
                            used = tileset;
                        }
                        if (!tileset || tileset != used)
                        {
                            if (tileset)
                            {
                                others.insert(tileset);
                            }
                            tiles.tiles.push_back(-1);
                            continue;
                        }
                        tiles.tiles.push_back(static_cast<int>(gid - tileset->firstGid));
                    }
                    if (!others.empty())
                    {
                        out.warnings.push_back(
                            "layer " + tiles.name + " uses more than one tileset; only the first is drawn"
                        );
                    }
                    if (used)
                    {
                        tiles.image = used->image;
                        tiles.columns = used->columns;
                        tiles.rows = used->rows;
                        tiles.solidTiles = used->solid;
                    }
                    out.layers.push_back(std::move(tiles));
                }
                else if (type == "objectgroup")
                {
                    for (const auto &object : layer.at("objects"))
                    {
                        TiledObject result;
                        result.name = object.value("name", std::string());
                        result.type = object.value("type", object.value("class", std::string()));
                        result.width = object.value("width", 0.0f) / tileWidth;
                        result.height = object.value("height", 0.0f) / tileHeight;
                        result.x = object.value("x", 0.0f) / tileWidth;
                        // Tile objects are anchored at their bottom-left, others at the top-left:
                        // report the bottom-left for both.
                        const bool tileObject = object.contains("gid");
                        result.y = object.value("y", 0.0f) / tileHeight + (tileObject ? 0.0f : result.height);
                        if (result.name.empty())
                        {
                            result.name = result.type.empty() ? "Object" : result.type;
                        }
                        out.objects.push_back(std::move(result));
                    }
                }
                // Image layers are skipped.
            }
            return true;
        };
        if (!visit(map.at("layers")))
        {
            return false;
        }
    }
    catch (const std::exception &e)
    {
        return Fail(error, std::string("malformed Tiled map: ") + e.what());
    }
    return true;
}

std::string FormatTileList(std::vector<int> tiles)
{
    std::sort(tiles.begin(), tiles.end());
    tiles.erase(std::unique(tiles.begin(), tiles.end()), tiles.end());
    std::string text;
    for (size_t i = 0; i < tiles.size();)
    {
        size_t j = i;
        while (j + 1 < tiles.size() && tiles[j + 1] == tiles[j] + 1)
        {
            ++j;
        }
        if (!text.empty())
        {
            text += ",";
        }
        text += std::to_string(tiles[i]);
        if (j > i)
        {
            text += "-" + std::to_string(tiles[j]);
        }
        i = j + 1;
    }
    return text;
}

} // namespace Geni::cook
