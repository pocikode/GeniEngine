#pragma once

#include "assets/AssetID.h"

#include <cstdint>
#include <glm/ext/vector_double3.hpp>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

// World files (docs/design/0007). Shared by the cooker, the runtime and the editor; depends
// on nothing but JSON and glm so the cooker can compile it on its own.
//
// Source, `<name>.gworld`, edited by hand or by the editor:
//
//     { "format": "geni.world", "version": 1,
//       "cellSize": 256, "origin": [-2048, 0, -2048], "cellsX": 16, "cellsZ": 16,
//       "persistent": "persistent.gscene", "cells": "cells" }
//
// `persistent` and `cells` are relative to the .gworld file. Cell (x, z) is the scene
// `<cells>/cell_<x>_<z>.gscene`; its node positions are relative to the cell's corner,
// origin + (x * cellSize, 0, z * cellSize). A missing file is an empty cell.
//
// Cooked, listed in the manifest as a `world` asset: the same grid plus, per cell with
// content, its scene and HLOD scene IDs, how many objects it creates (prefabs expanded),
// its height range, the prefabs it uses and the GPU bytes of its HLOD meshes.
namespace Geni
{

struct WorldSource
{
    float cellSize = 256.0f;
    glm::dvec3 origin{0.0};
    int cellsX = 1;
    int cellsZ = 1;
    // Relative to the .gworld file's folder.
    std::string persistent = "persistent.gscene";
    std::string cellsFolder = "cells";
};

bool ParseWorldSource(const nlohmann::json &document, WorldSource &out, std::string *error = nullptr);
nlohmann::ordered_json WorldSourceToJson(const WorldSource &world);
// "cell_<x>_<z>.gscene"
std::string WorldCellFileName(int x, int z);

struct WorldCellInfo
{
    int x = 0;
    int z = 0;
    AssetID scene;
    AssetID hlod;
    uint32_t nodeCount = 0;
    float minY = 0.0f;
    float maxY = 0.0f;
    uint64_t hlodBytes = 0;
    std::vector<AssetID> prefabs;
};

struct WorldDescription
{
    std::string name;
    float cellSize = 256.0f;
    glm::dvec3 origin{0.0};
    int cellsX = 1;
    int cellsZ = 1;
    AssetID persistent;
    // Only cells with content; sorted by (z, x).
    std::vector<WorldCellInfo> cells;

    bool Contains(int x, int z) const
    {
        return x >= 0 && z >= 0 && x < cellsX && z < cellsZ;
    }
    int CellIndex(int x, int z) const
    {
        return z * cellsX + x;
    }
    // The world position of a cell's corner (its scene's origin).
    glm::dvec3 CellCorner(int x, int z) const
    {
        return origin + glm::dvec3(x * double(cellSize), 0.0, z * double(cellSize));
    }
    const WorldCellInfo *FindCell(int x, int z) const;
};

bool ParseCookedWorld(const nlohmann::json &document, WorldDescription &out, std::string *error = nullptr);
nlohmann::ordered_json CookedWorldToJson(const WorldDescription &world);

} // namespace Geni
