#include "Cooker.h"
#include "TextureCooker.h"

#include "assets/AssetID.h"
#include "assets/CookedFormats.h"
#include "graphics/VertexLayout.h"
#include "render/Mesh.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <random>
#include <nlohmann/json.hpp>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/matrix_decompose.hpp>

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

namespace Geni::cook
{

namespace
{

using Json = nlohmann::ordered_json;

struct ManifestEntry
{
    AssetID id;
    AssetType type = AssetType::Unknown;
    std::string source;
    std::string path;
    std::string bc;
    std::string astc;
};

// What a source file's `.meta` sidecar says (see Cooker.h).
struct SourceMeta
{
    std::string guid;
    bool hasUsage = false;
    TextureUsage usage = TextureUsage::Color;
};

struct SceneNode
{
    std::string name;
    int parent = -1;
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f};
    bool active = true;
    Json components = Json::array();
};

std::string Normalize(std::string text)
{
    std::replace(text.begin(), text.end(), '\\', '/');
    return text;
}

std::string WithoutExtension(const std::string &path)
{
    const auto dot = path.find_last_of('.');
    return dot == std::string::npos ? path : path.substr(0, dot);
}

std::string Sanitize(const std::string &path)
{
    std::string result;
    result.reserve(path.size());
    for (char c : path)
    {
        const unsigned char u = static_cast<unsigned char>(c);
        if (std::isalnum(u) || c == '/' || c == '_' || c == '-' || c == '.')
        {
            result.push_back(c);
        }
        else
        {
            result.push_back('_');
        }
    }
    return result;
}

// The key of a glTF object inside its file: "@name" when the name is unique among its
// siblings (reordering keeps the key), the index otherwise.
template <typename T> std::string SubKey(const T *items, size_t count, size_t index)
{
    const char *name = items[index].name;
    if (name && *name)
    {
        size_t same = 0;
        for (size_t i = 0; i < count; ++i)
        {
            if (items[i].name && std::strcmp(items[i].name, name) == 0)
            {
                ++same;
            }
        }
        if (same == 1)
        {
            return std::string("@") + name;
        }
    }
    return std::to_string(index);
}

std::string RandomGuid()
{
    std::random_device device;
    std::mt19937_64 generator((uint64_t(device()) << 32) ^ device());
    uint64_t value = 0;
    while (value == 0)
    {
        value = generator();
    }
    return AssetID{value}.ToHex();
}

Json Vec3Json(const glm::vec3 &value)
{
    return Json::array({value.x, value.y, value.z});
}

Json QuatJson(const glm::quat &value)
{
    return Json::array({value.x, value.y, value.z, value.w});
}

Json ComponentJson(const char *type, Json properties)
{
    Json component;
    component["type"] = type;
    component["properties"] = std::move(properties);
    return component;
}

bool ReadFile(const std::filesystem::path &path, std::vector<uint8_t> &out)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
    {
        return false;
    }
    const std::streamoff size = file.tellg();
    if (size < 0)
    {
        return false;
    }
    out.resize(static_cast<size_t>(size));
    file.seekg(0, std::ios::beg);
    if (size > 0 && !file.read(reinterpret_cast<char *>(out.data()), size))
    {
        return false;
    }
    return true;
}

MeshBounds ComputeBounds(const VertexLayout &layout, const std::vector<float> &vertices)
{
    MeshBounds bounds;
    const auto position = std::find_if(layout.elements.begin(), layout.elements.end(), [](const VertexElement &e) {
        return e.index == VertexElement::PositionIndex && e.size >= 3;
    });
    if (position == layout.elements.end() || layout.stride == 0)
    {
        return bounds;
    }
    const size_t vertexCount = (vertices.size() * sizeof(float)) / layout.stride;
    const size_t positionOffset = position->offset / sizeof(float);
    const size_t stride = layout.stride / sizeof(float);
    for (size_t i = 0; i < vertexCount; ++i)
    {
        const float *p = vertices.data() + i * stride + positionOffset;
        const glm::vec3 value(p[0], p[1], p[2]);
        if (i == 0)
        {
            bounds.min = bounds.max = value;
        }
        else
        {
            bounds.min = glm::min(bounds.min, value);
            bounds.max = glm::max(bounds.max, value);
        }
    }
    bounds.center = (bounds.min + bounds.max) * 0.5f;
    float radiusSquared = 0.0f;
    for (size_t i = 0; i < vertexCount; ++i)
    {
        const float *p = vertices.data() + i * stride + positionOffset;
        const glm::vec3 value(p[0], p[1], p[2]);
        const glm::vec3 delta = value - bounds.center;
        radiusSquared = std::max(radiusSquared, glm::dot(delta, delta));
    }
    bounds.radius = std::sqrt(radiusSquared);
    return bounds;
}

} // namespace

struct Cooker::Impl
{
    CookOptions options;
    std::string *error = nullptr;
    std::vector<ManifestEntry> manifest;
    std::set<uint64_t> ids;
    std::unordered_map<std::string, AssetID> textureCache;
    std::unordered_map<std::string, AssetID> materialCache;
    std::unordered_map<std::string, AssetID> meshCache;
    std::map<std::pair<const cgltf_image *, TextureUsage>, AssetID> imageCache;
    std::unordered_map<std::string, SourceMeta> metaCache;
    bool fullCook = false;

    bool Fail(const std::string &message)
    {
        if (error && error->empty())
        {
            *error = message;
        }
        return false;
    }

    bool AddManifest(ManifestEntry entry)
    {
        if (!entry.id.IsValid())
        {
            return Fail("asset has an empty ID: " + entry.source);
        }
        if (!ids.insert(entry.id.value).second)
        {
            return Fail("duplicate asset ID " + entry.id.ToHex() + " for " + entry.source);
        }
        manifest.push_back(std::move(entry));
        return true;
    }

    std::string OutputStem(const std::string &sourceRelative) const
    {
        return "cooked/" + Sanitize(WithoutExtension(Normalize(sourceRelative)));
    }

    std::string OutputPath(const std::string &stem, const std::string &suffix) const
    {
        return stem + suffix;
    }

    bool WriteManifest()
    {
        std::sort(manifest.begin(), manifest.end(), [](const ManifestEntry &a, const ManifestEntry &b) {
            return a.id.value < b.id.value;
        });

        Json document;
        document["format"] = "geni.manifest";
        document["version"] = 1;
        Json assets = Json::array();
        for (const auto &entry : manifest)
        {
            Json item;
            item["id"] = entry.id.ToHex();
            item["type"] = AssetTypeName(entry.type);
            item["source"] = entry.source;
            item["path"] = entry.path;
            if (!entry.bc.empty())
            {
                item["bc"] = entry.bc;
            }
            if (!entry.astc.empty())
            {
                item["astc"] = entry.astc;
            }
            assets.push_back(std::move(item));
        }
        document["assets"] = std::move(assets);

        const auto path = options.outputRoot / "cooked_manifest.json";
        std::error_code ec;
        std::filesystem::create_directories(options.outputRoot, ec);
        std::ofstream file(path);
        if (!file)
        {
            return Fail("cannot write " + path.string());
        }
        file << document.dump(2) << '\n';
        return true;
    }

    std::filesystem::path MetaPath(const std::string &relative) const
    {
        return options.sourceRoot / (relative + ".meta");
    }

    // Reads `<relative>.meta`. A missing file is an empty meta; a malformed one is an error.
    bool ReadMeta(const std::string &relative, SourceMeta &out)
    {
        auto cached = metaCache.find(relative);
        if (cached != metaCache.end())
        {
            out = cached->second;
            return true;
        }
        out = SourceMeta{};
        std::vector<uint8_t> bytes;
        if (ReadFile(MetaPath(relative), bytes))
        {
            try
            {
                const auto document = nlohmann::json::parse(bytes.begin(), bytes.end());
                out.guid = document.value("guid", std::string());
                if (!out.guid.empty() && !AssetID::FromHex(out.guid).IsValid())
                {
                    return Fail(relative + ".meta: \"guid\" must be 16 hex digits");
                }
                std::transform(out.guid.begin(), out.guid.end(), out.guid.begin(), [](unsigned char c) {
                    return static_cast<char>(std::tolower(c));
                });
                if (document.contains("usage"))
                {
                    out.hasUsage = true;
                    if (!TextureUsageFromName(document.value("usage", std::string()), out.usage))
                    {
                        return Fail(relative + ".meta: \"usage\" must be color, linear or normal");
                    }
                }
            }
            catch (const std::exception &e)
            {
                return Fail(relative + ".meta: " + e.what());
            }
        }
        metaCache[relative] = out;
        return true;
    }

    // The name every ID of this source file derives from (see Cooker.h).
    std::string RootName(const std::string &relative)
    {
        SourceMeta meta;
        if (ReadMeta(relative, meta) && !meta.guid.empty())
        {
            return "guid:" + meta.guid;
        }
        return Normalize(relative);
    }

    bool WriteMissingMeta(const std::vector<std::string> &inputs)
    {
        for (const auto &input : inputs)
        {
            std::error_code ec;
            if (std::filesystem::exists(MetaPath(input), ec))
            {
                continue;
            }
            Json meta;
            meta["guid"] = RandomGuid();
            std::ofstream file(MetaPath(input));
            if (!file)
            {
                return Fail("cannot write " + MetaPath(input).string());
            }
            file << meta.dump(2) << '\n';
        }
        return true;
    }

    static std::string UsageSuffix(TextureUsage usage)
    {
        return usage == TextureUsage::Color ? std::string() : std::string("#") + TextureUsageName(usage);
    }

    bool CookImageBytes(
        const std::vector<uint8_t> &bytes, const std::string &logicalName, const std::string &source,
        const std::string &outputStem, TextureUsage usage
    )
    {
        auto existing = textureCache.find(logicalName);
        if (existing != textureCache.end())
        {
            return true;
        }

        CookedTextureFiles files;
        std::string message;
        TextureCookSettings settings;
        settings.usage = usage;
        settings.quality = options.textureQuality;
        if (!CookTexture(bytes, outputStem, options.outputRoot, settings, files, &message))
        {
            return Fail(source + ": " + message);
        }
        const AssetID id = AssetID::FromName(logicalName);
        ManifestEntry entry;
        entry.id = id;
        entry.type = AssetType::Texture;
        entry.source = source;
        entry.path = files.rgba8;
        entry.bc = files.bc;
        entry.astc = files.astc;
        if (!AddManifest(std::move(entry)))
        {
            return false;
        }
        textureCache[logicalName] = id;
        return true;
    }

    bool CookImageFile(const std::string &relative)
    {
        std::vector<uint8_t> bytes;
        const auto fullPath = options.sourceRoot / relative;
        if (!ReadFile(fullPath, bytes))
        {
            return Fail("cannot read " + fullPath.string());
        }
        SourceMeta meta;
        if (!ReadMeta(relative, meta))
        {
            return false;
        }
        const TextureUsage usage = meta.hasUsage ? meta.usage : TextureUsage::Color;
        const std::string logical = "texture:" + RootName(relative) + UsageSuffix(usage);
        const std::string stem =
            OutputStem(relative) + (usage == TextureUsage::Color ? "" : std::string("_") + TextureUsageName(usage));
        return CookImageBytes(bytes, logical, Normalize(relative), stem, usage);
    }

    bool DecodeImage(
        cgltf_image *image, const std::string &gltfRelative, size_t imageIndex, std::vector<uint8_t> &bytes
    )
    {
        if (image->buffer_view)
        {
            const auto *view = image->buffer_view;
            const auto *data = static_cast<const uint8_t *>(view->buffer->data) + view->offset;
            bytes.assign(data, data + view->size);
            return true;
        }
        if (!image->uri)
        {
            return false;
        }
        if (std::strncmp(image->uri, "data:", 5) == 0)
        {
            const char *comma = std::strchr(image->uri, ',');
            if (!comma || comma - image->uri < 7 || std::strncmp(comma - 7, ";base64", 7) != 0)
            {
                return false;
            }
            void *decoded = nullptr;
            cgltf_options opt = {};
            const cgltf_size decodedSize = static_cast<cgltf_size>(std::strlen(comma + 1)) * 3 / 4;
            if (cgltf_load_buffer_base64(&opt, decodedSize, comma + 1, &decoded) != cgltf_result_success || !decoded)
            {
                return false;
            }
            bytes.assign(static_cast<uint8_t *>(decoded), static_cast<uint8_t *>(decoded) + decodedSize);
            std::free(decoded);
            return true;
        }

        std::string uri(image->uri);
        cgltf_decode_uri(uri.data());
        uri.resize(std::strlen(uri.c_str()));
        const auto folder = std::filesystem::path(gltfRelative).parent_path();
        return ReadFile(options.sourceRoot / folder / uri, bytes);
    }

    AssetID EnsureTexture(
        cgltf_data *data, cgltf_image *image, const std::string &gltfRelative, size_t imageIndex, TextureUsage usage
    )
    {
        if (!image)
        {
            return AssetID{};
        }
        auto cached = imageCache.find({image, usage});
        if (cached != imageCache.end())
        {
            return cached->second;
        }

        std::string logical;
        std::string source;
        std::string outputStem;
        if (image->uri && std::strncmp(image->uri, "data:", 5) != 0)
        {
            std::string uri(image->uri);
            cgltf_decode_uri(uri.data());
            uri.resize(std::strlen(uri.c_str()));
            const auto folder = std::filesystem::path(gltfRelative).parent_path();
            // lexically_normal: "models/../textures/a.png" must be the same asset as
            // "textures/a.png" cooked on its own.
            const std::string relative = Normalize((folder / uri).lexically_normal().generic_string());
            logical = "texture:" + RootName(relative);
            source = relative;
            outputStem = OutputStem(relative);
        }
        else
        {
            source = Normalize(gltfRelative) + "#image" + std::to_string(imageIndex);
            logical = "texture:" + RootName(gltfRelative) + "#image" + SubKey(data->images, data->images_count, imageIndex);
            outputStem = "cooked/textures/" + Sanitize(WithoutExtension(Normalize(gltfRelative))) + "_image" +
                         std::to_string(imageIndex);
        }
        // One image used both as colour and as data is cooked once per usage.
        logical += UsageSuffix(usage);
        if (usage != TextureUsage::Color)
        {
            outputStem += std::string("_") + TextureUsageName(usage);
            source += UsageSuffix(usage);
        }

        auto existing = textureCache.find(logical);
        if (existing != textureCache.end())
        {
            imageCache[{image, usage}] = existing->second;
            return existing->second;
        }

        std::vector<uint8_t> bytes;
        if (!DecodeImage(image, gltfRelative, imageIndex, bytes))
        {
            Fail(source + ": cannot decode the embedded image");
            return AssetID{};
        }
        if (!CookImageBytes(bytes, logical, source, outputStem, usage))
        {
            return AssetID{};
        }
        const AssetID id = textureCache[logical];
        imageCache[{image, usage}] = id;
        return id;
    }

    AssetID EnsureMaterial(
        cgltf_data *data, cgltf_material *material, int materialIndex, bool skinned, const std::string &gltfRelative
    )
    {
        const std::string key =
            "material:" + RootName(gltfRelative) + ":" +
            (materialIndex >= 0 ? SubKey(data->materials, data->materials_count, size_t(materialIndex)) : "default") +
            (skinned ? ":skinned" : "");
        auto cached = materialCache.find(key);
        if (cached != materialCache.end())
        {
            return cached->second;
        }

        cooked::CookedMaterialData cooked;
        cooked.shader = skinned ? "builtin:skinned" : "builtin:default";

        auto loadTexture = [&](cgltf_texture *texture, const char *name, TextureUsage usage) {
            if (!texture || !texture->image)
            {
                return;
            }
            // Find the image index for the logical name of embedded images.
            size_t imageIndex = 0;
            for (size_t i = 0; i < data->images_count; ++i)
            {
                if (&data->images[i] == texture->image)
                {
                    imageIndex = i;
                    break;
                }
            }
            const AssetID id = EnsureTexture(data, texture->image, gltfRelative, imageIndex, usage);
            if (id.IsValid())
            {
                cooked.textures[name] = id;
            }
        };

        glm::vec4 baseColor(1.0f, 1.0f, 1.0f, 1.0f);
        glm::vec4 metallicRoughness(0.0f, 1.0f, 0.0f, 0.0f);
        glm::vec4 emissive(0.0f);
        glm::vec4 normalScaleOcclusion(0.0f, 1.0f, 0.0f, 0.0f);
        if (material)
        {
            if (material->has_pbr_metallic_roughness)
            {
                const auto &pbr = material->pbr_metallic_roughness;
                baseColor = glm::vec4(
                    pbr.base_color_factor[0], pbr.base_color_factor[1], pbr.base_color_factor[2],
                    pbr.base_color_factor[3]
                );
                metallicRoughness = glm::vec4(pbr.metallic_factor, pbr.roughness_factor, 0.0f, 0.0f);
                loadTexture(pbr.base_color_texture.texture, "baseColorTexture", TextureUsage::Color);
                loadTexture(
                    pbr.metallic_roughness_texture.texture, "metallicRoughnessTexture", TextureUsage::Linear
                );
            }
            else if (material->has_pbr_specular_glossiness)
            {
                const auto &sg = material->pbr_specular_glossiness;
                baseColor =
                    glm::vec4(sg.diffuse_factor[0], sg.diffuse_factor[1], sg.diffuse_factor[2], sg.diffuse_factor[3]);
                metallicRoughness = glm::vec4(0.0f, 1.0f - sg.glossiness_factor, 0.0f, 0.0f);
                loadTexture(sg.diffuse_texture.texture, "baseColorTexture", TextureUsage::Color);
            }
            emissive = glm::vec4(
                material->emissive_factor[0], material->emissive_factor[1], material->emissive_factor[2], 0.0f
            );
            loadTexture(material->emissive_texture.texture, "emissiveTexture", TextureUsage::Color);
            if (material->normal_texture.texture)
            {
                loadTexture(material->normal_texture.texture, "normalTexture", TextureUsage::Normal);
                normalScaleOcclusion.x = material->normal_texture.scale;
            }
            if (material->occlusion_texture.texture)
            {
                loadTexture(material->occlusion_texture.texture, "occlusionTexture", TextureUsage::Linear);
                normalScaleOcclusion.y = material->occlusion_texture.scale;
            }
        }

        cooked.float4s["uBaseColorFactor"] = baseColor;
        cooked.float4s["uMetallicRoughness"] = metallicRoughness;
        cooked.float4s["uEmissiveFactor"] = emissive;
        cooked.float4s["uNormalScaleOcclusion"] = normalScaleOcclusion;

        std::vector<uint8_t> bytes;
        cooked::WriteMaterial(cooked, bytes);
        const std::string stem = OutputStem(gltfRelative) + ".mat" +
                                 (materialIndex >= 0 ? std::to_string(materialIndex) : "default") +
                                 (skinned ? "_skinned" : "");
        const std::string path = OutputPath(stem, ".gmat");
        if (!cooked::WriteFile(options.outputRoot / path, bytes))
        {
            Fail("cannot write " + path);
            return AssetID{};
        }

        const AssetID id = AssetID::FromName(key);
        ManifestEntry entry;
        entry.id = id;
        entry.type = AssetType::Material;
        entry.source = Normalize(gltfRelative) + "#material" +
                       (materialIndex >= 0 ? std::to_string(materialIndex) : "default") + (skinned ? ":skinned" : "");
        entry.path = path;
        if (!AddManifest(std::move(entry)))
        {
            return AssetID{};
        }
        materialCache[key] = id;
        return id;
    }

    bool CookPrimitive(
        cgltf_node *node, cgltf_mesh *mesh, size_t meshIndex, size_t primitiveIndex, size_t skinIndex, bool skinned,
        const std::string &gltfRelative, const std::string &meshKey, std::string &meshID
    )
    {
        auto &primitive = mesh->primitives[primitiveIndex];
        if (primitive.type != cgltf_primitive_type_triangles)
        {
            return true;
        }

        auto readFloats = [](const cgltf_accessor *accessor, cgltf_size index, float *out, int count) {
            std::fill(out, out + count, 0.0f);
            return cgltf_accessor_read_float(accessor, index, out, count) == 1;
        };
        auto readIndex = [](const cgltf_accessor *accessor, cgltf_size index) {
            cgltf_uint out = 0;
            return cgltf_accessor_read_uint(accessor, index, &out, 1) ? static_cast<uint32_t>(out) : 0u;
        };

        VertexLayout layout;
        cgltf_accessor *accessors[6] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
        for (cgltf_size ai = 0; ai < primitive.attributes_count; ++ai)
        {
            auto &attribute = primitive.attributes[ai];
            auto *accessor = attribute.data;
            if (!accessor)
            {
                continue;
            }
            VertexElement element;
            element.type = VertexElementType::Float;
            switch (attribute.type)
            {
            case cgltf_attribute_type_position:
                accessors[VertexElement::PositionIndex] = accessor;
                element.index = VertexElement::PositionIndex;
                element.size = 3;
                break;
            case cgltf_attribute_type_color:
                if (attribute.index != 0)
                    continue;
                accessors[VertexElement::ColorIndex] = accessor;
                element.index = VertexElement::ColorIndex;
                element.size = 3;
                break;
            case cgltf_attribute_type_texcoord:
                if (attribute.index != 0)
                    continue;
                accessors[VertexElement::UVIndex] = accessor;
                element.index = VertexElement::UVIndex;
                element.size = 2;
                break;
            case cgltf_attribute_type_normal:
                accessors[VertexElement::NormalIndex] = accessor;
                element.index = VertexElement::NormalIndex;
                element.size = 3;
                break;
            case cgltf_attribute_type_joints:
                if (attribute.index != 0)
                    continue;
                accessors[VertexElement::JointsIndex] = accessor;
                element.index = VertexElement::JointsIndex;
                element.size = 4;
                break;
            case cgltf_attribute_type_weights:
                if (attribute.index != 0)
                    continue;
                accessors[VertexElement::WeightsIndex] = accessor;
                element.index = VertexElement::WeightsIndex;
                element.size = 4;
                break;
            default:
                continue;
            }
            if (element.size > 0)
            {
                element.offset = layout.stride;
                layout.stride += element.size * sizeof(float);
                layout.elements.push_back(element);
            }
        }
        if (!accessors[VertexElement::PositionIndex])
        {
            return true;
        }

        const cgltf_size vertexCount = accessors[VertexElement::PositionIndex]->count;
        cooked::CookedMeshData cooked;
        cooked.layout = layout;
        cooked.vertices.resize((layout.stride / sizeof(float)) * vertexCount, 0.0f);
        for (cgltf_size vi = 0; vi < vertexCount; ++vi)
        {
            for (const auto &element : layout.elements)
            {
                if (!accessors[element.index])
                {
                    continue;
                }
                const size_t index = (vi * layout.stride + element.offset) / sizeof(float);
                readFloats(accessors[element.index], vi, &cooked.vertices[index], element.size);
            }
        }
        if (primitive.indices)
        {
            cooked.indices.resize(primitive.indices->count);
            for (cgltf_size i = 0; i < primitive.indices->count; ++i)
            {
                cooked.indices[i] = readIndex(primitive.indices, i);
            }
        }
        cooked.topology = rhi::PrimitiveTopology::TriangleList;
        cooked.bounds = ComputeBounds(layout, cooked.vertices);

        if (skinned && node->skin)
        {
            for (cgltf_size ji = 0; ji < node->skin->joints_count; ++ji)
            {
                cgltf_node *joint = node->skin->joints[ji];
                cooked.jointNames.push_back(joint->name ? joint->name : "node");
                glm::mat4 inverseBind(1.0f);
                if (node->skin->inverse_bind_matrices)
                {
                    float raw[16] = {};
                    cgltf_accessor_read_float(node->skin->inverse_bind_matrices, ji, raw, 16);
                    inverseBind = glm::make_mat4(raw);
                }
                cooked.inverseBindMatrices.push_back(inverseBind);
            }
        }

        std::vector<uint8_t> bytes;
        cooked::WriteMesh(cooked, bytes);
        const std::string stem =
            OutputStem(gltfRelative) + ".mesh" + std::to_string(meshIndex) + "." + std::to_string(primitiveIndex);
        const std::string path = OutputPath(stem, ".gmesh");
        if (!cooked::WriteFile(options.outputRoot / path, bytes))
        {
            return Fail("cannot write " + path);
        }

        const AssetID id = AssetID::FromName(meshKey);
        ManifestEntry entry;
        entry.id = id;
        entry.type = AssetType::Mesh;
        entry.source =
            Normalize(gltfRelative) + "#mesh" + std::to_string(meshIndex) + ":" + std::to_string(primitiveIndex) +
            (skinned ? ":skin" + std::to_string(skinIndex) : "");
        entry.path = path;
        if (!AddManifest(std::move(entry)))
        {
            return false;
        }
        meshID = id.ToHex();
        return true;
    }

    bool CookAnimations(cgltf_data *data, const std::string &gltfRelative, AssetID &animationID, std::string &firstClip)
    {
        if (data->animations_count == 0)
        {
            return true;
        }

        std::vector<cooked::CookedAnimationData> clips;
        clips.reserve(data->animations_count);
        auto readScalar = [](cgltf_accessor *accessor, cgltf_size index) {
            float value = 0.0f;
            cgltf_accessor_read_float(accessor, index, &value, 1);
            return value;
        };
        auto readVec3 = [](cgltf_accessor *accessor, cgltf_size index) {
            glm::vec3 value(0.0f);
            cgltf_accessor_read_float(accessor, index, glm::value_ptr(value), 3);
            return value;
        };
        auto readQuat = [](cgltf_accessor *accessor, cgltf_size index) {
            float value[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            cgltf_accessor_read_float(accessor, index, value, 4);
            return glm::quat(value[3], value[0], value[1], value[2]);
        };

        for (cgltf_size ai = 0; ai < data->animations_count; ++ai)
        {
            auto &animation = data->animations[ai];
            cooked::CookedAnimationData clip;
            clip.name = animation.name ? animation.name : "noname";
            clip.duration = 0.0f;
            clip.looping = true;

            std::unordered_map<cgltf_node *, size_t> trackIndex;
            auto getTrack = [&](cgltf_node *node) -> TransformTrack & {
                auto it = trackIndex.find(node);
                if (it != trackIndex.end())
                {
                    return clip.tracks[it->second];
                }
                TransformTrack track;
                track.targetName = node->name ? node->name : "node";
                clip.tracks.push_back(track);
                trackIndex[node] = clip.tracks.size() - 1;
                return clip.tracks.back();
            };

            for (cgltf_size ci = 0; ci < animation.channels_count; ++ci)
            {
                auto &channel = animation.channels[ci];
                auto *sampler = channel.sampler;
                if (!channel.target_node || !sampler || !sampler->input || !sampler->output)
                {
                    continue;
                }
                std::vector<float> times(sampler->input->count);
                for (cgltf_size i = 0; i < sampler->input->count; ++i)
                {
                    times[i] = readScalar(sampler->input, i);
                }
                auto &track = getTrack(channel.target_node);
                switch (channel.target_path)
                {
                case cgltf_animation_path_type_translation:
                    track.positions.resize(times.size());
                    for (size_t i = 0; i < times.size(); ++i)
                    {
                        track.positions[i].time = times[i];
                        track.positions[i].value = readVec3(sampler->output, i);
                    }
                    break;
                case cgltf_animation_path_type_rotation:
                    track.rotations.resize(times.size());
                    for (size_t i = 0; i < times.size(); ++i)
                    {
                        track.rotations[i].time = times[i];
                        track.rotations[i].value = readQuat(sampler->output, i);
                    }
                    break;
                case cgltf_animation_path_type_scale:
                    track.scales.resize(times.size());
                    for (size_t i = 0; i < times.size(); ++i)
                    {
                        track.scales[i].time = times[i];
                        track.scales[i].value = readVec3(sampler->output, i);
                    }
                    break;
                default:
                    break;
                }
                if (!times.empty())
                {
                    clip.duration = std::max(clip.duration, times.back());
                }
            }
            clips.push_back(std::move(clip));
        }

        std::vector<uint8_t> bytes;
        cooked::WriteAnimations(clips, bytes);
        const std::string path = OutputPath(OutputStem(gltfRelative) + ".anim", ".ganim");
        if (!cooked::WriteFile(options.outputRoot / path, bytes))
        {
            return Fail("cannot write " + path);
        }
        animationID = AssetID::FromName("animation:" + RootName(gltfRelative));
        ManifestEntry entry;
        entry.id = animationID;
        entry.type = AssetType::Animation;
        entry.source = Normalize(gltfRelative) + "#animations";
        entry.path = path;
        if (!AddManifest(std::move(entry)))
        {
            return false;
        }
        firstClip = clips.front().name;
        return true;
    }

    bool AddNode(
        cgltf_data *data, cgltf_node *node, int parentIndex, const std::string &gltfRelative,
        std::vector<SceneNode> &nodes, int &mainCamera
    )
    {
        SceneNode out;
        out.name = node->name ? node->name : "node";
        out.parent = parentIndex;
        if (node->has_matrix)
        {
            const glm::mat4 matrix = glm::make_mat4(node->matrix);
            glm::vec3 translation;
            glm::vec3 scale;
            glm::vec3 skew;
            glm::vec4 perspective;
            glm::quat orientation;
            glm::decompose(matrix, scale, orientation, translation, skew, perspective);
            out.position = translation;
            out.rotation = orientation;
            out.scale = scale;
        }
        else
        {
            if (node->has_translation)
            {
                out.position = glm::vec3(node->translation[0], node->translation[1], node->translation[2]);
            }
            if (node->has_rotation)
            {
                out.rotation = glm::quat(node->rotation[3], node->rotation[0], node->rotation[1], node->rotation[2]);
            }
            if (node->has_scale)
            {
                out.scale = glm::vec3(node->scale[0], node->scale[1], node->scale[2]);
            }
        }

        // Extras: "geni": { "camera", "orbit", "light" }.
        if (node->extras.data)
        {
            try
            {
                const auto extras = nlohmann::json::parse(node->extras.data);
                if (extras.contains("geni") && extras["geni"].is_object())
                {
                    const auto &geni = extras["geni"];
                    if (geni.contains("camera"))
                    {
                        const auto &camera = geni["camera"];
                        Json properties;
                        properties["fov"] = camera.value("fov", 60.0f);
                        properties["near"] = camera.value("near", 0.1f);
                        properties["far"] = camera.value("far", 1000.0f);
                        out.components.push_back(ComponentJson("CameraComponent", properties));
                    }
                    if (geni.contains("orbit"))
                    {
                        const auto &orbit = geni["orbit"];
                        Json properties;
                        if (orbit.contains("target"))
                        {
                            properties["target"] = orbit["target"];
                        }
                        properties["distance"] = orbit.value("distance", 5.0f);
                        properties["yaw"] = orbit.value("yaw", 45.0f);
                        properties["pitch"] = orbit.value("pitch", 30.0f);
                        out.components.push_back(ComponentJson("OrbitCameraComponent", properties));
                    }
                    if (geni.contains("light"))
                    {
                        const auto &light = geni["light"];
                        Json properties;
                        properties["type"] = light.value("type", std::string("point"));
                        if (light.contains("color"))
                        {
                            properties["color"] = light["color"];
                        }
                        properties["intensity"] = light.value("intensity", 1.0f);
                        properties["range"] = light.value("range", 10.0f);
                        properties["innerConeAngle"] = light.value("innerConeAngle", 0.0f);
                        properties["outerConeAngle"] = light.value("outerConeAngle", 0.785398f);
                        out.components.push_back(ComponentJson("LightComponent", properties));
                    }
                }
            }
            catch (const std::exception &)
            {
                // Malformed extras are not a reason to reject the whole scene.
            }
        }

        const int nodeIndex = static_cast<int>(nodes.size());
        nodes.push_back(out);

        if (node->mesh)
        {
            size_t meshIndex = 0;
            for (size_t i = 0; i < data->meshes_count; ++i)
            {
                if (&data->meshes[i] == node->mesh)
                {
                    meshIndex = i;
                    break;
                }
            }
            const bool skinned = node->skin != nullptr;
            for (cgltf_size pi = 0; pi < node->mesh->primitives_count; ++pi)
            {
                auto &primitive = node->mesh->primitives[pi];
                if (primitive.type != cgltf_primitive_type_triangles)
                {
                    continue;
                }
                int materialIndex = -1;
                cgltf_material *material = primitive.material;
                if (material)
                {
                    for (size_t mi = 0; mi < data->materials_count; ++mi)
                    {
                        if (&data->materials[mi] == material)
                        {
                            materialIndex = static_cast<int>(mi);
                            break;
                        }
                    }
                }
                const AssetID materialID = EnsureMaterial(data, material, materialIndex, skinned, gltfRelative);
                if (!materialID.IsValid())
                {
                    return false;
                }
                // A glTF mesh is shared by every node that references it; cook it once.
                // Skinned meshes are keyed by skin as well, because the joint list and
                // bind matrices are per-skin.
                size_t skinIndex = 0;
                if (node->skin)
                {
                    for (size_t si = 0; si < data->skins_count; ++si)
                    {
                        if (&data->skins[si] == node->skin)
                        {
                            skinIndex = si;
                            break;
                        }
                    }
                }
                const std::string meshKey =
                    "mesh:" + RootName(gltfRelative) + ":" + SubKey(data->meshes, data->meshes_count, meshIndex) + ":" +
                    std::to_string(pi) + (skinned ? ":skin" + SubKey(data->skins, data->skins_count, skinIndex) : "");
                std::string meshID;
                auto cachedMesh = meshCache.find(meshKey);
                if (cachedMesh != meshCache.end())
                {
                    meshID = cachedMesh->second.ToHex();
                }
                else
                {
                    if (!CookPrimitive(
                            node, node->mesh, meshIndex, pi, skinIndex, skinned, gltfRelative, meshKey, meshID
                        ))
                    {
                        return false;
                    }
                    meshCache[meshKey] = AssetID::FromHex(meshID);
                }
                Json properties;
                properties["mesh"] = meshID;
                properties["material"] = materialID.ToHex();
                nodes[static_cast<size_t>(nodeIndex)].components.push_back(
                    ComponentJson(skinned ? "SkinnedMeshComponent" : "MeshComponent", properties)
                );
            }
        }

        // The camera is the first node that has one.
        if (mainCamera < 0)
        {
            for (const auto &component : nodes[static_cast<size_t>(nodeIndex)].components)
            {
                if (component["type"] == "CameraComponent")
                {
                    mainCamera = nodeIndex;
                    break;
                }
            }
        }

        for (cgltf_size ci = 0; ci < node->children_count; ++ci)
        {
            if (!AddNode(data, node->children[ci], nodeIndex, gltfRelative, nodes, mainCamera))
            {
                return false;
            }
        }
        return true;
    }

    bool CookGLTF(const std::string &relative)
    {
        std::vector<uint8_t> fileBytes;
        const auto fullPath = options.sourceRoot / relative;
        if (!ReadFile(fullPath, fileBytes))
        {
            return Fail("cannot read " + fullPath.string());
        }

        cgltf_options cgltfOptions = {};
        cgltf_data *data = nullptr;
        if (cgltf_parse(&cgltfOptions, fileBytes.data(), fileBytes.size(), &data) != cgltf_result_success)
        {
            return Fail(Normalize(relative) + ": not a valid glTF file");
        }
        const auto folder = (options.sourceRoot / relative).parent_path();
        if (cgltf_load_buffers(&cgltfOptions, data, folder.string().c_str()) != cgltf_result_success)
        {
            cgltf_free(data);
            return Fail(Normalize(relative) + ": cannot load buffers");
        }

        cgltf_scene *scene = data->scene ? data->scene : (data->scenes_count > 0 ? &data->scenes[0] : nullptr);
        if (!scene)
        {
            cgltf_free(data);
            return Fail(Normalize(relative) + ": no scene");
        }

        AssetID animationID;
        std::string firstClip;
        if (!CookAnimations(data, relative, animationID, firstClip))
        {
            cgltf_free(data);
            return false;
        }

        std::vector<SceneNode> nodes;
        SceneNode root;
        root.name = "Result";
        root.parent = -1;
        if (animationID.IsValid())
        {
            Json properties;
            properties["animation"] = animationID.ToHex();
            properties["clip"] = firstClip;
            properties["playing"] = true;
            properties["looping"] = true;
            root.components.push_back(ComponentJson("AnimationComponent", properties));
        }
        nodes.push_back(std::move(root));

        int mainCamera = -1;
        for (cgltf_size i = 0; i < scene->nodes_count; ++i)
        {
            if (!AddNode(data, scene->nodes[i], 0, relative, nodes, mainCamera))
            {
                cgltf_free(data);
                return false;
            }
        }

        Json document;
        document["format"] = "geni.scene";
        document["version"] = 1;
        Json nodeArray = Json::array();
        for (const auto &node : nodes)
        {
            Json item;
            item["name"] = node.name;
            item["parent"] = node.parent;
            item["position"] = Vec3Json(node.position);
            item["rotation"] = QuatJson(node.rotation);
            item["scale"] = Vec3Json(node.scale);
            item["active"] = node.active;
            item["components"] = node.components;
            nodeArray.push_back(std::move(item));
        }
        document["nodes"] = std::move(nodeArray);
        document["mainCamera"] = mainCamera;

        const std::string scenePath = OutputPath(OutputStem(relative), ".gscene");
        std::error_code ec;
        std::filesystem::create_directories((options.outputRoot / scenePath).parent_path(), ec);
        std::ofstream sceneFile(options.outputRoot / scenePath);
        if (!sceneFile)
        {
            cgltf_free(data);
            return Fail("cannot write " + scenePath);
        }
        sceneFile << cooked::FormatSceneText(document);

        ManifestEntry entry;
        entry.id = AssetID::FromName("scene:" + RootName(relative));
        entry.type = AssetType::Scene;
        entry.source = Normalize(relative);
        entry.path = scenePath;
        const bool ok = AddManifest(std::move(entry));
        cgltf_free(data);
        return ok;
    }

    // Deletes files under <output>/cooked that this cook did not write (the manifest lists
    // every file the cooker writes), then any folders left empty. Without this, a format
    // the cooker stopped producing (the old _bc1 textures) stays in the build folder.
    bool RemoveStaleOutputs()
    {
        const std::filesystem::path cookedRoot = options.outputRoot / "cooked";
        std::error_code ec;
        if (!std::filesystem::is_directory(cookedRoot, ec))
        {
            return true;
        }

        std::set<std::filesystem::path> written;
        for (const auto &entry : manifest)
        {
            for (const std::string *path : {&entry.path, &entry.bc, &entry.astc})
            {
                if (!path->empty())
                {
                    written.insert((options.outputRoot / *path).lexically_normal());
                }
            }
        }

        std::vector<std::filesystem::path> stale;
        std::vector<std::filesystem::path> folders;
        for (auto it = std::filesystem::recursive_directory_iterator(cookedRoot, ec);
             !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec))
        {
            if (it->is_directory(ec))
            {
                folders.push_back(it->path());
            }
            else if (written.find(it->path().lexically_normal()) == written.end())
            {
                stale.push_back(it->path());
            }
        }
        if (ec)
        {
            return Fail("cannot scan " + cookedRoot.string() + ": " + ec.message());
        }

        for (const auto &path : stale)
        {
            if (!std::filesystem::remove(path, ec) && ec)
            {
                return Fail("cannot remove stale " + path.string() + ": " + ec.message());
            }
        }
        // Deepest first, so a parent is empty once its children are gone.
        std::sort(folders.begin(), folders.end(), [](const auto &a, const auto &b) {
            return a.string().size() > b.string().size();
        });
        for (const auto &folder : folders)
        {
            if (std::filesystem::is_empty(folder, ec))
            {
                std::filesystem::remove(folder, ec);
            }
        }
        return true;
    }

    bool Discover(std::vector<std::string> &inputs)
    {
        std::error_code ec;
        if (!std::filesystem::exists(options.sourceRoot, ec))
        {
            return Fail("source root does not exist: " + options.sourceRoot.string());
        }
        for (auto it = std::filesystem::recursive_directory_iterator(options.sourceRoot, ec);
             it != std::filesystem::recursive_directory_iterator(); ++it)
        {
            if (ec)
            {
                return Fail("cannot scan " + options.sourceRoot.string() + ": " + ec.message());
            }
            if (it->is_directory())
            {
                const std::string name = it->path().filename().string();
                if (name == "cooked" || name == ".git")
                {
                    it.disable_recursion_pending();
                }
                continue;
            }
            std::string extension = it->path().extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            if (extension == ".gltf" || extension == ".glb" || extension == ".png" || extension == ".jpg" ||
                extension == ".jpeg")
            {
                inputs.push_back(
                    Normalize(std::filesystem::relative(it->path(), options.sourceRoot, ec).generic_string())
                );
            }
        }
        std::sort(inputs.begin(), inputs.end());
        return true;
    }

    bool Run()
    {
        std::vector<std::string> inputs = options.inputs;
        fullCook = options.cookAll || inputs.empty();
        if (fullCook)
        {
            if (!Discover(inputs))
            {
                return false;
            }
        }
        if (options.writeMeta && !WriteMissingMeta(inputs))
        {
            return false;
        }

        // glTF first: it cooks referenced images, so standalone images are then cache hits.
        for (const auto &input : inputs)
        {
            std::string extension = std::filesystem::path(input).extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            if (extension == ".gltf" || extension == ".glb")
            {
                if (!CookGLTF(input))
                {
                    return false;
                }
            }
        }
        for (const auto &input : inputs)
        {
            std::string extension = std::filesystem::path(input).extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            if (extension == ".png" || extension == ".jpg" || extension == ".jpeg")
            {
                if (!CookImageFile(input))
                {
                    return false;
                }
            }
        }

        if (!WriteManifest())
        {
            return false;
        }
        // Only a full cook knows every asset; a partial (--input) cook leaves other
        // assets' files alone.
        return fullCook ? RemoveStaleOutputs() : true;
    }
};

bool Cooker::Run(const CookOptions &options, std::string *error)
{
    if (error)
    {
        error->clear();
    }
    Impl impl;
    impl.options = options;
    impl.error = error;
    return impl.Run();
}

} // namespace Geni::cook
