#include "WorldCooker.h"

#include "MeshProcessing.h"
#include "assets/CookedFormats.h"
#include "world/WorldFormat.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <fstream>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/mat3x3.hpp>
#include <glm/mat4x4.hpp>
#include <map>
#include <set>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace Geni::cook
{

namespace
{

using Json = nlohmann::json;
using OrderedJson = nlohmann::ordered_json;

constexpr int kMaxPrefabDepth = 8;

glm::vec3 ReadVec3(const Json &node, const char *key, const glm::vec3 &fallback)
{
    const auto it = node.find(key);
    if (it == node.end() || !it->is_array() || it->size() != 3)
    {
        return fallback;
    }
    return glm::vec3((*it)[0].get<float>(), (*it)[1].get<float>(), (*it)[2].get<float>());
}

glm::mat4 LocalTransform(const Json &node)
{
    const glm::vec3 position = ReadVec3(node, "position", glm::vec3(0.0f));
    const glm::vec3 scale = ReadVec3(node, "scale", glm::vec3(1.0f));
    glm::quat rotation(1.0f, 0.0f, 0.0f, 0.0f);
    const auto it = node.find("rotation");
    if (it != node.end() && it->is_array() && it->size() == 4)
    {
        // Scene files store quaternions as [x, y, z, w].
        rotation =
            glm::quat((*it)[3].get<float>(), (*it)[0].get<float>(), (*it)[1].get<float>(), (*it)[2].get<float>());
    }
    return glm::translate(glm::mat4(1.0f), position) * glm::mat4_cast(rotation) * glm::scale(glm::mat4(1.0f), scale);
}

// A mesh drawn by the cell, with its transform relative to the cell corner.
struct PlacedMesh
{
    AssetID mesh;
    AssetID material;
    glm::mat4 transform{1.0f};
};

struct CellContents
{
    uint32_t nodes = 0;
    std::set<uint64_t> prefabs;
    std::vector<PlacedMesh> meshes;
};

// Walks a scene or prefab document the way SceneSerializer instantiates it: node 0 of a
// prefab is the instance itself, prefab instances expand recursively, inactive subtrees
// are skipped for drawing (they still count as nodes).
class CellWalker
{
  public:
    explicit CellWalker(WorldCookHost &host) : m_host(host)
    {
    }

    bool Walk(const Json &document, const glm::mat4 &parent, bool asPrefab, int depth, CellContents &out, bool active)
    {
        if (depth > kMaxPrefabDepth)
        {
            return m_host.Fail("prefabs nested more than " + std::to_string(kMaxPrefabDepth) + " deep");
        }
        const auto nodesIt = document.find("nodes");
        if (nodesIt == document.end() || !nodesIt->is_array())
        {
            return m_host.Fail("scene without a node array");
        }
        const Json &nodes = *nodesIt;
        std::vector<glm::mat4> world(nodes.size(), glm::mat4(1.0f));
        std::vector<bool> enabled(nodes.size(), active);
        for (size_t i = 0; i < nodes.size(); ++i)
        {
            const Json &node = nodes[i];
            const int parentIndex = node.value("parent", -1);
            const bool hasParent = parentIndex >= 0 && parentIndex < static_cast<int>(i);
            const glm::mat4 base = hasParent ? world[static_cast<size_t>(parentIndex)] : parent;
            const bool parentEnabled = hasParent ? enabled[static_cast<size_t>(parentIndex)] : active;
            // A prefab's root node is the instance: the instance's own transform applies.
            world[i] = (asPrefab && i == 0) ? parent : base * LocalTransform(node);
            enabled[i] = parentEnabled && ((asPrefab && i == 0) || node.value("active", true));
            if (!(asPrefab && i == 0))
            {
                ++out.nodes;
            }

            const std::string prefab = node.value("prefab", std::string());
            if (!prefab.empty() && !(asPrefab && i == 0))
            {
                const AssetID id = AssetID::FromHex(prefab);
                const Json *prefabDocument = m_host.FindPrefab(id);
                if (!prefabDocument)
                {
                    return m_host.Fail("prefab " + prefab + " is not part of this cook");
                }
                out.prefabs.insert(id.value);
                if (!Walk(*prefabDocument, world[i], true, depth + 1, out, enabled[i]))
                {
                    return false;
                }
                continue;
            }

            const auto components = node.find("components");
            if (components == node.end() || !components->is_array() || !enabled[i])
            {
                continue;
            }
            for (const auto &component : *components)
            {
                if (component.value("type", std::string()) != "MeshComponent")
                {
                    continue;
                }
                const auto &properties = component.value("properties", Json::object());
                PlacedMesh placed;
                placed.mesh = AssetID::FromHex(properties.value("mesh", std::string()));
                placed.material = AssetID::FromHex(properties.value("material", std::string()));
                placed.transform = world[i];
                if (placed.mesh.IsValid() && placed.material.IsValid())
                {
                    out.meshes.push_back(placed);
                }
            }
        }
        return true;
    }

  private:
    WorldCookHost &m_host;
};

uint64_t LayoutKey(const VertexLayout &layout)
{
    uint64_t key = layout.stride;
    for (const auto &element : layout.elements)
    {
        key = key * 1099511628211ull ^ (element.index * 131u + element.size * 17u + element.offset);
    }
    return key;
}

int AttributeOffset(const VertexLayout &layout, uint32_t index)
{
    for (const auto &element : layout.elements)
    {
        if (element.index == index && element.size >= 3)
        {
            return static_cast<int>(element.offset / sizeof(float));
        }
    }
    return -1;
}

// Appends `source` (its coarsest LOD: an HLOD is only seen from far away) to `target`,
// transformed.
void AppendTransformed(cooked::CookedMeshData &target, const cooked::CookedMeshData &source, const glm::mat4 &transform)
{
    const size_t stride = source.layout.stride / sizeof(float);
    const size_t firstVertex = target.vertices.size() / stride;
    const int position = AttributeOffset(source.layout, VertexElement::PositionIndex);
    const int normal = AttributeOffset(source.layout, VertexElement::NormalIndex);
    const glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(transform)));
    const size_t vertexCount = source.vertices.size() / stride;

    target.vertices.reserve(target.vertices.size() + source.vertices.size());
    for (size_t v = 0; v < vertexCount; ++v)
    {
        const float *in = source.vertices.data() + v * stride;
        const size_t at = target.vertices.size();
        target.vertices.insert(target.vertices.end(), in, in + stride);
        float *out = target.vertices.data() + at;
        if (position >= 0)
        {
            const glm::vec3 p =
                glm::vec3(transform * glm::vec4(in[position], in[position + 1], in[position + 2], 1.0f));
            out[position] = p.x;
            out[position + 1] = p.y;
            out[position + 2] = p.z;
        }
        if (normal >= 0)
        {
            glm::vec3 n = normalMatrix * glm::vec3(in[normal], in[normal + 1], in[normal + 2]);
            const float length = glm::length(n);
            n = length > 0.0f ? n / length : glm::vec3(0.0f, 1.0f, 0.0f);
            out[normal] = n.x;
            out[normal + 1] = n.y;
            out[normal + 2] = n.z;
        }
    }

    const uint32_t count =
        source.lods.empty() ? static_cast<uint32_t>(source.indices.size()) : source.lods.back().indexCount;
    const uint32_t first = source.lods.empty() ? 0u : source.lods.back().firstIndex;
    // A mirroring transform turns triangles inside out; keep them counter-clockwise.
    const bool mirrored = glm::determinant(glm::mat3(transform)) < 0.0f;
    for (uint32_t i = 0; i + 2 < count; i += 3)
    {
        const uint32_t a = source.indices[first + i];
        const uint32_t b = source.indices[first + i + 1];
        const uint32_t c = source.indices[first + i + 2];
        target.indices.push_back(static_cast<uint32_t>(firstVertex) + a);
        target.indices.push_back(static_cast<uint32_t>(firstVertex) + (mirrored ? c : b));
        target.indices.push_back(static_cast<uint32_t>(firstVertex) + (mirrored ? b : c));
    }
}

// A unit box (-0.5..0.5) in `layout`: positions and normals filled, other attributes
// white (colour) or zero.
cooked::CookedMeshData UnitBox(const VertexLayout &layout)
{
    cooked::CookedMeshData box;
    box.layout = layout;
    box.topology = rhi::PrimitiveTopology::TriangleList;
    const size_t stride = layout.stride / sizeof(float);
    const int position = AttributeOffset(layout, VertexElement::PositionIndex);
    const int normal = AttributeOffset(layout, VertexElement::NormalIndex);
    const int color = AttributeOffset(layout, VertexElement::ColorIndex);
    const glm::vec3 axes[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    for (int axis = 0; axis < 3; ++axis)
    {
        for (float sign : {1.0f, -1.0f})
        {
            const glm::vec3 n = axes[axis] * sign;
            const glm::vec3 u = axes[(axis + 1) % 3];
            const glm::vec3 v = axes[(axis + 2) % 3];
            const uint32_t first = static_cast<uint32_t>(box.vertices.size() / stride);
            const glm::vec2 corners[4] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
            for (const auto &c : corners)
            {
                const glm::vec3 p = 0.5f * (n + c.x * u + c.y * v);
                std::vector<float> vertex(stride, 0.0f);
                if (position >= 0)
                {
                    vertex[position] = p.x;
                    vertex[position + 1] = p.y;
                    vertex[position + 2] = p.z;
                }
                if (normal >= 0)
                {
                    vertex[normal] = n.x;
                    vertex[normal + 1] = n.y;
                    vertex[normal + 2] = n.z;
                }
                if (color >= 0)
                {
                    vertex[color] = vertex[color + 1] = vertex[color + 2] = 1.0f;
                }
                box.vertices.insert(box.vertices.end(), vertex.begin(), vertex.end());
            }
            // u x v = n for a positive face, so (0,1,2) is counter-clockwise seen from
            // outside; a negative face winds the other way.
            if (sign > 0.0f)
            {
                box.indices.insert(box.indices.end(), {first, first + 1, first + 2, first, first + 2, first + 3});
            }
            else
            {
                box.indices.insert(box.indices.end(), {first, first + 2, first + 1, first, first + 3, first + 2});
            }
        }
    }
    return box;
}

MeshBounds BoundsOf(const cooked::CookedMeshData &mesh)
{
    MeshBounds bounds;
    const int position = AttributeOffset(mesh.layout, VertexElement::PositionIndex);
    const size_t stride = mesh.layout.stride / sizeof(float);
    if (position < 0 || stride == 0 || mesh.vertices.empty())
    {
        return bounds;
    }
    const size_t count = mesh.vertices.size() / stride;
    for (size_t v = 0; v < count; ++v)
    {
        const float *p = mesh.vertices.data() + v * stride + position;
        const glm::vec3 value(p[0], p[1], p[2]);
        bounds.min = v == 0 ? value : glm::min(bounds.min, value);
        bounds.max = v == 0 ? value : glm::max(bounds.max, value);
    }
    bounds.center = (bounds.min + bounds.max) * 0.5f;
    float radius = 0.0f;
    for (size_t v = 0; v < count; ++v)
    {
        const float *p = mesh.vertices.data() + v * stride + position;
        radius = std::max(radius, glm::length(glm::vec3(p[0], p[1], p[2]) - bounds.center));
    }
    bounds.radius = radius;
    return bounds;
}

std::string Normalize(std::string text)
{
    std::replace(text.begin(), text.end(), '\\', '/');
    return text;
}

std::string Join(const std::filesystem::path &folder, const std::string &name)
{
    return (folder / name).lexically_normal().generic_string();
}

} // namespace

bool CookWorld(WorldCookHost &host, const std::string &relative)
{
    std::vector<uint8_t> bytes;
    if (!cooked::ReadFile(host.SourceRoot() / relative, bytes))
    {
        return host.Fail("cannot read " + relative);
    }
    WorldSource source;
    std::string error;
    try
    {
        if (!ParseWorldSource(Json::parse(bytes.begin(), bytes.end()), source, &error))
        {
            return host.Fail(relative + ": " + error);
        }
    }
    catch (const std::exception &e)
    {
        return host.Fail(relative + ": " + e.what());
    }

    const std::filesystem::path worldFolder = std::filesystem::path(relative).parent_path();
    const std::string worldRoot = host.RootName(relative);
    const std::string stem = std::filesystem::path(relative).stem().string();
    const std::string outputFolder = "cooked/" + Join(worldFolder, stem);

    WorldDescription description;
    description.name = stem;
    description.cellSize = source.cellSize;
    description.origin = source.origin;
    description.cellsX = source.cellsX;
    description.cellsZ = source.cellsZ;
    const std::string persistentPath = Join(worldFolder, source.persistent);
    std::error_code ec;
    if (std::filesystem::exists(host.SourceRoot() / persistentPath, ec))
    {
        description.persistent = AssetID::FromName("scene:" + host.RootName(persistentPath));
    }

    std::unordered_map<uint64_t, cooked::CookedMeshData> meshCache;
    auto loadMesh = [&](AssetID id) -> const cooked::CookedMeshData * {
        auto it = meshCache.find(id.value);
        if (it != meshCache.end())
        {
            return it->second.vertices.empty() ? nullptr : &it->second;
        }
        cooked::CookedMeshData &data = meshCache[id.value];
        const std::string path = host.FindCookedPath(id);
        std::vector<uint8_t> meshBytes;
        if (path.empty() || !cooked::ReadFile(host.OutputRoot() / path, meshBytes) ||
            !cooked::ReadMesh(meshBytes, data) || data.IsSkinned() ||
            data.topology != rhi::PrimitiveTopology::TriangleList || data.indices.empty())
        {
            data = cooked::CookedMeshData{};
            return nullptr;
        }
        return &data;
    };

    CellWalker walker(host);
    for (int z = 0; z < source.cellsZ; ++z)
    {
        for (int x = 0; x < source.cellsX; ++x)
        {
            const std::string cellPath = Join(worldFolder / source.cellsFolder, WorldCellFileName(x, z));
            if (!std::filesystem::exists(host.SourceRoot() / cellPath, ec))
            {
                continue;
            }
            std::vector<uint8_t> cellBytes;
            if (!cooked::ReadFile(host.SourceRoot() / cellPath, cellBytes))
            {
                return host.Fail("cannot read " + cellPath);
            }
            CellContents contents;
            try
            {
                const Json document = Json::parse(cellBytes.begin(), cellBytes.end());
                if (!walker.Walk(document, glm::mat4(1.0f), false, 0, contents, true))
                {
                    return host.Fail(cellPath + ": cannot be cooked into the world");
                }
            }
            catch (const std::exception &e)
            {
                return host.Fail(cellPath + ": " + e.what());
            }

            WorldCellInfo cell;
            cell.x = x;
            cell.z = z;
            cell.scene = AssetID::FromName("scene:" + host.RootName(cellPath));
            cell.nodeCount = contents.nodes;
            for (uint64_t prefab : contents.prefabs)
            {
                cell.prefabs.push_back(AssetID{prefab});
            }

            // HLOD: everything big enough, merged per material and vertex layout.
            std::map<std::pair<uint64_t, uint64_t>, cooked::CookedMeshData> groups;
            bool anyBounds = false;
            for (const PlacedMesh &placed : contents.meshes)
            {
                const cooked::CookedMeshData *mesh = loadMesh(placed.mesh);
                if (!mesh)
                {
                    continue;
                }
                const glm::vec3 scale(
                    glm::length(glm::vec3(placed.transform[0])), glm::length(glm::vec3(placed.transform[1])),
                    glm::length(glm::vec3(placed.transform[2]))
                );
                const float worldRadius = mesh->bounds.radius * std::max({scale.x, scale.y, scale.z});
                for (int corner = 0; corner < 8; ++corner)
                {
                    const glm::vec3 local(
                        (corner & 1) ? mesh->bounds.max.x : mesh->bounds.min.x,
                        (corner & 2) ? mesh->bounds.max.y : mesh->bounds.min.y,
                        (corner & 4) ? mesh->bounds.max.z : mesh->bounds.min.z
                    );
                    const float y = (placed.transform * glm::vec4(local, 1.0f)).y;
                    cell.minY = anyBounds ? std::min(cell.minY, y) : y;
                    cell.maxY = anyBounds ? std::max(cell.maxY, y) : y;
                    anyBounds = true;
                }
                if (2.0f * worldRadius < HlodMinimumSize)
                {
                    continue;
                }
                auto &group = groups[{placed.material.value, LayoutKey(mesh->layout)}];
                group.layout = mesh->layout;
                group.topology = rhi::PrimitiveTopology::TriangleList;
                const size_t triangles =
                    mesh->lods.empty() ? mesh->indices.size() / 3 : mesh->lods.back().indexCount / 3;
                if (2.0f * worldRadius < HlodProxySize && triangles > 12)
                {
                    const glm::vec3 extent = mesh->bounds.max - mesh->bounds.min;
                    const glm::mat4 proxy =
                        placed.transform *
                        glm::translate(glm::mat4(1.0f), (mesh->bounds.min + mesh->bounds.max) * 0.5f) *
                        glm::scale(glm::mat4(1.0f), glm::max(extent, glm::vec3(0.01f)));
                    AppendTransformed(group, UnitBox(mesh->layout), proxy);
                }
                else
                {
                    AppendTransformed(group, *mesh, placed.transform);
                }
            }

            if (!groups.empty())
            {
                OrderedJson hlod;
                hlod["format"] = "geni.scene";
                hlod["version"] = 1;
                hlod["mainCamera"] = -1;
                OrderedJson nodes = OrderedJson::array();
                const std::string cellKey = std::to_string(x) + "_" + std::to_string(z);
                int index = 0;
                for (auto &[key, group] : groups)
                {
                    // About a tenth of the triangles, with up to 3% of the cell's extent as
                    // error (about 8 m on a 256 m cell); merged boxes may fuse together.
                    SimplifyInPlace(group, 0.1f, 0.03f);
                    group.bounds = BoundsOf(group);
                    group.lods.clear();
                    std::vector<uint8_t> meshBytes;
                    cooked::WriteMesh(group, meshBytes);
                    const std::string meshPath =
                        outputFolder + "/hlod/cell_" + cellKey + "." + std::to_string(index) + ".gmesh";
                    if (!cooked::WriteFile(host.OutputRoot() / meshPath, meshBytes))
                    {
                        return host.Fail("cannot write " + meshPath);
                    }
                    WorldCookAsset meshAsset;
                    meshAsset.id = AssetID::FromName(
                        "hlod:" + worldRoot + ":" + cellKey + ":" + AssetID{key.first}.ToHex() + ":" +
                        std::to_string(key.second)
                    );
                    meshAsset.type = AssetType::Mesh;
                    meshAsset.source = Normalize(relative) + "#hlod" + cellKey + "." + std::to_string(index);
                    meshAsset.path = meshPath;
                    if (!host.AddAsset(meshAsset))
                    {
                        return false;
                    }
                    cell.hlodBytes += group.vertices.size() * sizeof(float) + group.indices.size() * sizeof(uint32_t);

                    OrderedJson properties;
                    properties["material"] = AssetID{key.first}.ToHex();
                    properties["mesh"] = meshAsset.id.ToHex();
                    OrderedJson component;
                    component["type"] = "MeshComponent";
                    component["properties"] = properties;
                    OrderedJson node;
                    node["name"] = "HLOD" + std::to_string(index);
                    node["parent"] = -1;
                    node["position"] = {0.0f, 0.0f, 0.0f};
                    node["rotation"] = {0.0f, 0.0f, 0.0f, 1.0f};
                    node["scale"] = {1.0f, 1.0f, 1.0f};
                    node["active"] = true;
                    node["components"] = OrderedJson::array({component});
                    nodes.push_back(std::move(node));
                    ++index;
                }
                hlod["nodes"] = std::move(nodes);
                const std::string scenePath = outputFolder + "/hlod/cell_" + cellKey + ".gscene";
                const std::string text = cooked::FormatSceneText(hlod);
                if (!cooked::WriteFile(host.OutputRoot() / scenePath, std::vector<uint8_t>(text.begin(), text.end())))
                {
                    return host.Fail("cannot write " + scenePath);
                }
                WorldCookAsset sceneAsset;
                sceneAsset.id = AssetID::FromName("hlodscene:" + worldRoot + ":" + cellKey);
                sceneAsset.type = AssetType::Scene;
                sceneAsset.source = Normalize(relative) + "#hlod" + cellKey;
                sceneAsset.path = scenePath;
                if (!host.AddAsset(sceneAsset))
                {
                    return false;
                }
                cell.hlod = sceneAsset.id;
            }
            description.cells.push_back(std::move(cell));
        }
    }

    const std::string worldPath = outputFolder + ".gworld.json";
    const std::string text = CookedWorldToJson(description).dump(1) + "\n";
    if (!cooked::WriteFile(host.OutputRoot() / worldPath, std::vector<uint8_t>(text.begin(), text.end())))
    {
        return host.Fail("cannot write " + worldPath);
    }
    WorldCookAsset worldAsset;
    worldAsset.id = AssetID::FromName("world:" + worldRoot);
    worldAsset.type = AssetType::World;
    worldAsset.source = Normalize(relative);
    worldAsset.path = worldPath;
    return host.AddAsset(worldAsset);
}

} // namespace Geni::cook
