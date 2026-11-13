#pragma once

// A packed sprite atlas (docs/design/0021): one sprite texture and named pixel rectangles,
// cooked from a .gatlas file.

#include "assets/AssetID.h"

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>
#include <nlohmann/json_fwd.hpp>

#include <map>
#include <string>

namespace Geni
{

struct SpriteAtlasData
{
    AssetID texture;
    int width = 0;
    int height = 0;
    // x, y (top-left, pixels from the image's top), width, height.
    std::map<std::string, glm::ivec4> regions;

    // The region's texture coordinates (V points down the image). False when it does not
    // exist.
    bool FindRegion(const std::string &name, glm::vec2 &uvTopLeft, glm::vec2 &uvBottomRight) const;
};

// Reads the cooker's atlas JSON ("geni.atlas.cooked" version 1).
bool ParseSpriteAtlas(const nlohmann::json &document, SpriteAtlasData &out, std::string *error = nullptr);

} // namespace Geni
