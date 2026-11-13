#include "world/WorldFormat.h"

#include <algorithm>
#include <exception>

namespace Geni
{

namespace
{
bool Fail(std::string *error, const std::string &message)
{
    if (error)
    {
        *error = message;
    }
    return false;
}

glm::dvec3 ReadVec3(const nlohmann::json &value)
{
    return glm::dvec3(value.at(0).get<double>(), value.at(1).get<double>(), value.at(2).get<double>());
}

bool CheckGrid(float cellSize, int cellsX, int cellsZ, std::string *error)
{
    if (!(cellSize >= 1.0f && cellSize <= 65536.0f))
    {
        return Fail(error, "cellSize must be between 1 and 65536 metres");
    }
    if (cellsX < 1 || cellsZ < 1 || cellsX > 4096 || cellsZ > 4096)
    {
        return Fail(error, "cellsX and cellsZ must be between 1 and 4096");
    }
    return true;
}
} // namespace

bool ParseWorldSource(const nlohmann::json &document, WorldSource &out, std::string *error)
{
    out = WorldSource{};
    try
    {
        if (!document.is_object() || document.value("format", std::string()) != "geni.world" ||
            document.value("version", 0) != 1)
        {
            return Fail(error, "not a version 1 geni.world file");
        }
        out.cellSize = document.value("cellSize", out.cellSize);
        if (document.contains("origin"))
        {
            out.origin = ReadVec3(document.at("origin"));
        }
        out.cellsX = document.value("cellsX", out.cellsX);
        out.cellsZ = document.value("cellsZ", out.cellsZ);
        out.persistent = document.value("persistent", out.persistent);
        out.cellsFolder = document.value("cells", out.cellsFolder);
    }
    catch (const std::exception &e)
    {
        return Fail(error, std::string("malformed world: ") + e.what());
    }
    return CheckGrid(out.cellSize, out.cellsX, out.cellsZ, error);
}

nlohmann::ordered_json WorldSourceToJson(const WorldSource &world)
{
    nlohmann::ordered_json document;
    document["format"] = "geni.world";
    document["version"] = 1;
    document["cellSize"] = world.cellSize;
    document["origin"] = {world.origin.x, world.origin.y, world.origin.z};
    document["cellsX"] = world.cellsX;
    document["cellsZ"] = world.cellsZ;
    document["persistent"] = world.persistent;
    document["cells"] = world.cellsFolder;
    return document;
}

std::string WorldCellFileName(int x, int z)
{
    return "cell_" + std::to_string(x) + "_" + std::to_string(z) + ".gscene";
}

const WorldCellInfo *WorldDescription::FindCell(int x, int z) const
{
    const auto it = std::lower_bound(
        cells.begin(), cells.end(), std::make_pair(z, x),
        [](const WorldCellInfo &cell, const std::pair<int, int> &key) { return std::make_pair(cell.z, cell.x) < key; }
    );
    return it != cells.end() && it->x == x && it->z == z ? &*it : nullptr;
}

bool ParseCookedWorld(const nlohmann::json &document, WorldDescription &out, std::string *error)
{
    out = WorldDescription{};
    try
    {
        if (!document.is_object() || document.value("format", std::string()) != "geni.world.cooked" ||
            document.value("version", 0) != 1)
        {
            return Fail(error, "not a version 1 cooked world");
        }
        out.name = document.value("name", std::string());
        out.cellSize = document.at("cellSize").get<float>();
        out.origin = ReadVec3(document.at("origin"));
        out.cellsX = document.at("cellsX").get<int>();
        out.cellsZ = document.at("cellsZ").get<int>();
        out.persistent = AssetID::FromHex(document.value("persistent", std::string()));
        out.navKey = AssetID::FromHex(document.value("navKey", std::string())).value;
        if (!CheckGrid(out.cellSize, out.cellsX, out.cellsZ, error))
        {
            return false;
        }
        for (const auto &item : document.at("cells"))
        {
            WorldCellInfo cell;
            cell.x = item.at("x").get<int>();
            cell.z = item.at("z").get<int>();
            if (!out.Contains(cell.x, cell.z))
            {
                return Fail(
                    error, "cell " + std::to_string(cell.x) + "," + std::to_string(cell.z) + " is outside the grid"
                );
            }
            cell.scene = AssetID::FromHex(item.value("scene", std::string()));
            cell.hlod = AssetID::FromHex(item.value("hlod", std::string()));
            cell.nodeCount = item.value("nodes", 0u);
            cell.minY = item.value("minY", 0.0f);
            cell.maxY = item.value("maxY", 0.0f);
            cell.hlodBytes = item.value("hlodBytes", uint64_t(0));
            cell.nav = AssetID::FromHex(item.value("nav", std::string()));
            if (item.contains("prefabs"))
            {
                for (const auto &prefab : item.at("prefabs"))
                {
                    cell.prefabs.push_back(AssetID::FromHex(prefab.get<std::string>()));
                }
            }
            out.cells.push_back(std::move(cell));
        }
    }
    catch (const std::exception &e)
    {
        return Fail(error, std::string("malformed cooked world: ") + e.what());
    }
    std::sort(out.cells.begin(), out.cells.end(), [](const WorldCellInfo &a, const WorldCellInfo &b) {
        return std::make_pair(a.z, a.x) < std::make_pair(b.z, b.x);
    });
    for (size_t i = 1; i < out.cells.size(); ++i)
    {
        if (out.cells[i].x == out.cells[i - 1].x && out.cells[i].z == out.cells[i - 1].z)
        {
            return Fail(
                error,
                "cell " + std::to_string(out.cells[i].x) + "," + std::to_string(out.cells[i].z) + " is listed twice"
            );
        }
    }
    return true;
}

nlohmann::ordered_json CookedWorldToJson(const WorldDescription &world)
{
    nlohmann::ordered_json document;
    document["format"] = "geni.world.cooked";
    document["version"] = 1;
    document["name"] = world.name;
    document["cellSize"] = world.cellSize;
    document["origin"] = {world.origin.x, world.origin.y, world.origin.z};
    document["cellsX"] = world.cellsX;
    document["cellsZ"] = world.cellsZ;
    document["persistent"] = world.persistent.IsValid() ? world.persistent.ToHex() : std::string();
    document["navKey"] = world.navKey ? AssetID{world.navKey}.ToHex() : std::string();
    nlohmann::ordered_json cells = nlohmann::ordered_json::array();
    for (const auto &cell : world.cells)
    {
        nlohmann::ordered_json item;
        item["x"] = cell.x;
        item["z"] = cell.z;
        item["scene"] = cell.scene.IsValid() ? cell.scene.ToHex() : std::string();
        item["hlod"] = cell.hlod.IsValid() ? cell.hlod.ToHex() : std::string();
        item["nodes"] = cell.nodeCount;
        item["minY"] = cell.minY;
        item["maxY"] = cell.maxY;
        item["hlodBytes"] = cell.hlodBytes;
        nlohmann::ordered_json prefabs = nlohmann::ordered_json::array();
        for (const auto &prefab : cell.prefabs)
        {
            prefabs.push_back(prefab.ToHex());
        }
        item["prefabs"] = std::move(prefabs);
        item["nav"] = cell.nav.IsValid() ? cell.nav.ToHex() : std::string();
        cells.push_back(std::move(item));
    }
    document["cells"] = std::move(cells);
    return document;
}

} // namespace Geni
