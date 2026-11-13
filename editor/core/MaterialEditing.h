#pragma once

// Editing .mat files (docs/design/0027): pure, tested without a window. The Material
// panel shows these parameters, applies them live to the loaded Material and saves them.

#include "assets/AssetID.h"
#include "assets/CookedFormats.h"

#include <glm/vec4.hpp>
#include <nlohmann/json.hpp>

#include <functional>
#include <string>
#include <vector>

namespace Geni::Editor
{

enum class MaterialParamKind
{
    Float,
    Float2,
    Float4,
    Texture
};

struct MaterialParam
{
    std::string name;
    MaterialParamKind kind = MaterialParamKind::Float4;
    // Float uses x, Float2 x and y.
    glm::vec4 value{0.0f};
    // Texture: an image path relative to the assets folder; empty: none (white).
    std::string texture;
};

class MaterialSource
{
  public:
    // "builtin:default", "builtin:skinned", "builtin:terrain" or a .slang path.
    std::string shader = "builtin:default";
    std::vector<MaterialParam> params;

    bool IsStandard() const
    {
        return shader == "builtin:default" || shader == "builtin:skinned";
    }
    // Reads Material::Load's JSON. For the standard shader every standard parameter is
    // listed (with glTF defaults when the file leaves it out).
    bool Load(const nlohmann::json &document, std::string *error = nullptr);
    nlohmann::json ToJson() const;

    MaterialParam *Find(const std::string &name);
    const MaterialParam *Find(const std::string &name) const;
    // Adds the parameter when missing. False when a parameter of another kind has the name.
    bool Set(const std::string &name, MaterialParamKind kind, const glm::vec4 &value);
    bool SetTexture(const std::string &name, const std::string &path);

    // The standard shader's parameters with glTF defaults, in display order.
    static std::vector<MaterialParam> StandardParams();
    // A source with the values of a cooked material; `texturePath` gives each texture's
    // image path (empty: leave the slot out).
    static MaterialSource FromCooked(
        const cooked::CookedMaterialData &material, const std::function<std::string(AssetID)> &texturePath
    );
};

} // namespace Geni::Editor
