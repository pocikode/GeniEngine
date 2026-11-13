#include "render/SpriteAtlas.h"

#include <nlohmann/json.hpp>

namespace Geni
{

bool SpriteAtlasData::FindRegion(const std::string &name, glm::vec2 &uvTopLeft, glm::vec2 &uvBottomRight) const
{
    const auto it = regions.find(name);
    if (it == regions.end() || width <= 0 || height <= 0)
    {
        return false;
    }
    const glm::vec2 size(static_cast<float>(width), static_cast<float>(height));
    uvTopLeft = glm::vec2(it->second.x, it->second.y) / size;
    uvBottomRight = glm::vec2(it->second.x + it->second.z, it->second.y + it->second.w) / size;
    return true;
}

bool ParseSpriteAtlas(const nlohmann::json &document, SpriteAtlasData &out, std::string *error)
{
    auto fail = [error](const std::string &message) {
        if (error)
        {
            *error = message;
        }
        return false;
    };
    try
    {
        if (document.value("format", std::string()) != "geni.atlas.cooked" || document.value("version", 0) != 1)
        {
            return fail("not a version 1 cooked atlas");
        }
        out.texture = AssetID::FromHex(document.at("texture").get<std::string>());
        out.width = document.at("width").get<int>();
        out.height = document.at("height").get<int>();
        out.regions.clear();
        for (const auto &[name, rect] : document.at("regions").items())
        {
            out.regions[name] =
                glm::ivec4(rect.at(0).get<int>(), rect.at(1).get<int>(), rect.at(2).get<int>(), rect.at(3).get<int>());
        }
    }
    catch (const std::exception &e)
    {
        return fail(std::string("malformed atlas: ") + e.what());
    }
    if (!out.texture.IsValid() || out.width <= 0 || out.height <= 0)
    {
        return fail("an atlas needs a texture and a size");
    }
    return true;
}

} // namespace Geni
