#include "MaterialEditing.h"

#include <algorithm>

namespace Geni::Editor
{

namespace
{

nlohmann::json Numbers(const glm::vec4 &value, int count)
{
    nlohmann::json array = nlohmann::json::array();
    for (int i = 0; i < count; ++i)
    {
        array.push_back(value[i]);
    }
    return array;
}

glm::vec4 ReadNumbers(const nlohmann::json &value)
{
    glm::vec4 out(0.0f);
    if (value.is_number())
    {
        out.x = value.get<float>();
        return out;
    }
    for (size_t i = 0; i < std::min<size_t>(4, value.size()); ++i)
    {
        out[static_cast<int>(i)] = value[i].get<float>();
    }
    return out;
}

} // namespace

std::vector<MaterialParam> MaterialSource::StandardParams()
{
    return {
        {"uBaseColorFactor", MaterialParamKind::Float4, glm::vec4(1.0f), {}},
        {"uMetallicRoughness", MaterialParamKind::Float4, glm::vec4(0.0f, 1.0f, 0.0f, 0.0f), {}},
        {"uEmissiveFactor", MaterialParamKind::Float4, glm::vec4(0.0f), {}},
        {"uNormalScaleOcclusion", MaterialParamKind::Float4, glm::vec4(0.0f, 1.0f, 0.0f, 0.0f), {}},
        {"baseColorTexture", MaterialParamKind::Texture, glm::vec4(0.0f), {}},
        {"metallicRoughnessTexture", MaterialParamKind::Texture, glm::vec4(0.0f), {}},
        {"normalTexture", MaterialParamKind::Texture, glm::vec4(0.0f), {}},
        {"occlusionTexture", MaterialParamKind::Texture, glm::vec4(0.0f), {}},
        {"emissiveTexture", MaterialParamKind::Texture, glm::vec4(0.0f), {}},
    };
}

MaterialParam *MaterialSource::Find(const std::string &name)
{
    for (MaterialParam &param : params)
    {
        if (param.name == name)
        {
            return &param;
        }
    }
    return nullptr;
}

const MaterialParam *MaterialSource::Find(const std::string &name) const
{
    return const_cast<MaterialSource *>(this)->Find(name);
}

bool MaterialSource::Set(const std::string &name, MaterialParamKind kind, const glm::vec4 &value)
{
    if (MaterialParam *param = Find(name))
    {
        if (param->kind != kind)
        {
            return false;
        }
        param->value = value;
        return true;
    }
    params.push_back({name, kind, value, {}});
    return true;
}

bool MaterialSource::SetTexture(const std::string &name, const std::string &path)
{
    if (MaterialParam *param = Find(name))
    {
        if (param->kind != MaterialParamKind::Texture)
        {
            return false;
        }
        param->texture = path;
        return true;
    }
    params.push_back({name, MaterialParamKind::Texture, glm::vec4(0.0f), path});
    return true;
}

bool MaterialSource::Load(const nlohmann::json &document, std::string *error)
{
    MaterialSource loaded;
    try
    {
        const auto shaderValue = document.value("shader", nlohmann::json("builtin:default"));
        if (shaderValue.is_string())
        {
            loaded.shader = shaderValue.get<std::string>();
        }
        else
        {
            loaded.shader = shaderValue.value("fragment", shaderValue.value("vertex", std::string()));
        }
        if (loaded.IsStandard())
        {
            loaded.params = StandardParams();
        }
        const auto params = document.value("params", nlohmann::json::object());
        const auto floats = params.value("float", nlohmann::json::object());
        const auto float2s = params.value("float2", nlohmann::json::object());
        const auto float4s = params.value("float4", nlohmann::json::object());
        const auto textures = params.value("textures", nlohmann::json::array());
        for (const auto &[name, value] : floats.items())
        {
            loaded.Set(name, MaterialParamKind::Float, ReadNumbers(value));
        }
        for (const auto &[name, value] : float2s.items())
        {
            // A float2 of the standard shader (uMetallicRoughness) is a float4 member.
            MaterialParam *existing = loaded.Find(name);
            const MaterialParamKind kind = existing ? existing->kind : MaterialParamKind::Float2;
            glm::vec4 v = existing ? existing->value : glm::vec4(0.0f);
            const glm::vec4 read = ReadNumbers(value);
            v.x = read.x;
            v.y = read.y;
            loaded.Set(name, kind, v);
        }
        for (const auto &[name, value] : float4s.items())
        {
            loaded.Set(name, MaterialParamKind::Float4, ReadNumbers(value));
        }
        for (const auto &texture : textures)
        {
            loaded.SetTexture(texture.at("name").get<std::string>(), texture.value("path", std::string()));
        }
    }
    catch (const std::exception &e)
    {
        if (error)
        {
            *error = std::string("malformed material: ") + e.what();
        }
        return false;
    }
    *this = std::move(loaded);
    return true;
}

nlohmann::json MaterialSource::ToJson() const
{
    nlohmann::json floats = nlohmann::json::object();
    nlohmann::json float2s = nlohmann::json::object();
    nlohmann::json float4s = nlohmann::json::object();
    nlohmann::json textures = nlohmann::json::array();
    for (const MaterialParam &param : params)
    {
        switch (param.kind)
        {
        case MaterialParamKind::Float:
            floats[param.name] = param.value.x;
            break;
        case MaterialParamKind::Float2:
            float2s[param.name] = Numbers(param.value, 2);
            break;
        case MaterialParamKind::Float4:
            float4s[param.name] = Numbers(param.value, 4);
            break;
        case MaterialParamKind::Texture:
            if (!param.texture.empty())
            {
                textures.push_back({{"name", param.name}, {"path", param.texture}});
            }
            break;
        }
    }
    nlohmann::json params = nlohmann::json::object();
    if (!floats.empty())
    {
        params["float"] = floats;
    }
    if (!float2s.empty())
    {
        params["float2"] = float2s;
    }
    if (!float4s.empty())
    {
        params["float4"] = float4s;
    }
    if (!textures.empty())
    {
        params["textures"] = textures;
    }
    nlohmann::json document;
    if (shader.rfind("builtin:", 0) == 0)
    {
        document["shader"] = shader;
    }
    else
    {
        document["shader"] = {{"vertex", shader}, {"fragment", shader}};
    }
    document["params"] = params;
    return document;
}

MaterialSource MaterialSource::FromCooked(
    const cooked::CookedMaterialData &material, const std::function<std::string(AssetID)> &texturePath
)
{
    MaterialSource source;
    source.shader = material.shader;
    if (source.IsStandard())
    {
        source.params = StandardParams();
    }
    for (const auto &[name, value] : material.floats)
    {
        source.Set(name, MaterialParamKind::Float, glm::vec4(value, 0.0f, 0.0f, 0.0f));
    }
    for (const auto &[name, value] : material.float2s)
    {
        MaterialParam *existing = source.Find(name);
        glm::vec4 v = existing ? existing->value : glm::vec4(0.0f);
        v.x = value.x;
        v.y = value.y;
        source.Set(name, existing ? existing->kind : MaterialParamKind::Float2, v);
    }
    for (const auto &[name, value] : material.float4s)
    {
        source.Set(name, MaterialParamKind::Float4, value);
    }
    for (const auto &[name, id] : material.textures)
    {
        const std::string path = texturePath ? texturePath(id) : std::string();
        if (!path.empty())
        {
            source.SetTexture(name, path);
        }
    }
    // Stable order for the file: standard order first, then by name.
    std::stable_sort(source.params.begin(), source.params.end(), [](const MaterialParam &a, const MaterialParam &b) {
        return static_cast<int>(a.kind == MaterialParamKind::Texture) <
               static_cast<int>(b.kind == MaterialParamKind::Texture);
    });
    return source;
}

} // namespace Geni::Editor
