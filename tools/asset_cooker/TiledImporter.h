#pragma once

// Tiled maps (mapeditor.org, JSON format .tmj) to tilemaps and objects (docs/design/0021).

#include <nlohmann/json.hpp>

#include <functional>
#include <string>
#include <vector>

namespace Geni::cook
{

struct TiledTileLayer
{
    std::string name;
    int width = 0;
    int height = 0;
    // Row-major from the top row, -1 for empty: TilemapComponent's "tiles" order.
    std::vector<int> tiles;
    // Tileset image, relative to the source root, and its grid.
    std::string image;
    int columns = 1;
    int rows = 1;
    // Tile indices whose Tiled tile has the boolean property "solid".
    std::vector<int> solidTiles;
    bool collision = false;
    bool visible = true;
    float opacity = 1.0f;
};

struct TiledObject
{
    std::string name;
    std::string type;
    // In tiles, from the map's top-left corner, Y down (as Tiled).
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

struct TiledMap
{
    int width = 0;
    int height = 0;
    std::vector<TiledTileLayer> layers;
    std::vector<TiledObject> objects;
    // Notes for the cook log (flip flags dropped, extra tilesets ignored).
    std::vector<std::string> warnings;
};

// `mapPath` is the .tmj's path relative to the source root; `readJson` reads another file
// (an external .tsj tileset) by its path relative to the source root.
bool ImportTiledMap(
    const nlohmann::json &map, const std::string &mapPath,
    const std::function<bool(const std::string &, nlohmann::json &)> &readJson, TiledMap &out,
    std::string *error = nullptr
);

// "1,4-6,9": the compact list TilemapComponent's solidTiles reads.
std::string FormatTileList(std::vector<int> tiles);

} // namespace Geni::cook
