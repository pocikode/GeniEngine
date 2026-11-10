#include "scene/SceneSerializer.h"
#include "assets/AssetManager.h"
#include "assets/CookedFormats.h"
#include "core/Log.h"
#include "scene/BuiltinComponents.h"
#include "scene/GameObject.h"
#include "scene/Scene.h"

#include <algorithm>
#include <fstream>
#include <memory>
#include <unordered_map>
#include <vector>

namespace Geni
{

namespace
{

void Fail(std::string *error, const std::string &message)
{
    if (error)
    {
        *error = message;
    }
}

nlohmann::ordered_json Vec3Json(const glm::vec3 &value)
{
    return nlohmann::ordered_json::array({value.x, value.y, value.z});
}

nlohmann::ordered_json QuatJson(const glm::quat &value)
{
    return nlohmann::ordered_json::array({value.x, value.y, value.z, value.w});
}

glm::vec3 Vec3FromJson(const nlohmann::json &value, const glm::vec3 &fallback)
{
    if (!value.is_array() || value.size() != 3)
    {
        return fallback;
    }
    return glm::vec3(value[0].get<float>(), value[1].get<float>(), value[2].get<float>());
}

glm::quat QuatFromJson(const nlohmann::json &value, const glm::quat &fallback)
{
    if (!value.is_array() || value.size() != 4)
    {
        return fallback;
    }
    return glm::quat(value[3].get<float>(), value[0].get<float>(), value[1].get<float>(), value[2].get<float>());
}

void CollectNodes(
    GameObject *object, int parentIndex, std::vector<GameObject *> &nodes,
    std::unordered_map<const GameObject *, int> &indices, std::vector<int> &parents
)
{
    const int index = static_cast<int>(nodes.size());
    nodes.push_back(object);
    indices[object] = index;
    parents.push_back(parentIndex);
    for (const auto &child : object->GetChildren())
    {
        CollectNodes(child.get(), index, nodes, indices, parents);
    }
}

} // namespace

nlohmann::ordered_json SceneSerializer::SaveToJson(const Scene &scene)
{
    std::vector<GameObject *> nodes;
    std::unordered_map<const GameObject *, int> indices;
    std::vector<int> parents;
    for (const auto &root : scene.GetObjects())
    {
        CollectNodes(root.get(), -1, nodes, indices, parents);
    }

    nlohmann::ordered_json document;
    document["format"] = "geni.scene";
    document["version"] = 1;

    nlohmann::ordered_json nodeArray = nlohmann::ordered_json::array();
    for (size_t i = 0; i < nodes.size(); ++i)
    {
        GameObject *object = nodes[i];
        nlohmann::ordered_json node;
        node["name"] = object->GetName();
        node["parent"] = parents[i];
        node["position"] = Vec3Json(object->GetPosition());
        node["rotation"] = QuatJson(object->GetRotation());
        node["scale"] = Vec3Json(object->GetScale());
        node["active"] = object->IsActive();

        nlohmann::ordered_json components = nlohmann::ordered_json::array();
        for (const auto &component : object->GetComponents())
        {
            const char *typeName = component->GetTypeName();
            nlohmann::ordered_json entry;
            entry["type"] = typeName;
            entry["properties"] = TypeRegistry::Get().Serialize(typeName, *component);
            components.push_back(std::move(entry));
        }
        node["components"] = std::move(components);
        nodeArray.push_back(std::move(node));
    }
    document["nodes"] = std::move(nodeArray);

    const auto cameraIt = indices.find(scene.GetMainCamera());
    document["mainCamera"] = cameraIt == indices.end() ? -1 : cameraIt->second;
    return document;
}

std::string SceneSerializer::SaveToText(const Scene &scene)
{
    return cooked::FormatSceneText(SaveToJson(scene));
}

bool SceneSerializer::Save(const Scene &scene, const std::filesystem::path &path, std::string *error)
{
    std::error_code ec;
    if (!path.parent_path().empty())
    {
        std::filesystem::create_directories(path.parent_path(), ec);
    }
    std::ofstream file(path);
    if (!file)
    {
        Fail(error, "cannot open " + path.string());
        return false;
    }
    file << SaveToText(scene);
    if (!file.good())
    {
        Fail(error, "could not write " + path.string());
        return false;
    }
    return true;
}

SceneInstantiation::SceneInstantiation(
    Scene &scene, const nlohmann::json &document, const SerializationContext &context, bool clear
)
    : m_scene(scene), m_document(document), m_context(context), m_clear(clear)
{
}

float SceneInstantiation::GetProgress() const
{
    if (m_phase == Phase::Finished)
    {
        return 1.0f;
    }
    if (!m_nodes || m_nodes->empty())
    {
        return 0.0f;
    }
    const float fraction = static_cast<float>(m_cursor) / static_cast<float>(m_nodes->size());
    return m_phase == Phase::Components ? 0.5f + 0.5f * fraction : 0.5f * fraction;
}

SceneInstantiation::Status SceneInstantiation::Step(size_t maxNodes)
{
    if (m_status != Status::InProgress)
    {
        return m_status;
    }
    size_t budget = maxNodes == 0 ? 1 : maxNodes;
    // A wrongly typed value (a string where a number belongs, a short vector) makes
    // nlohmann::json throw. The file is data, so that is a load error, not a crash.
    try
    {
        while (m_status == Status::InProgress && budget > 0)
        {
            switch (m_phase)
            {
            case Phase::Validate:
                Begin();
                break;
            case Phase::Objects:
                CreateObjects(budget);
                break;
            case Phase::Components:
                CreateComponents(budget);
                break;
            case Phase::Finished:
                Finish();
                break;
            }
        }
        if (m_status == Status::InProgress && m_phase == Phase::Finished)
        {
            Finish();
        }
    }
    catch (const std::exception &e)
    {
        Abort(std::string("malformed scene: ") + e.what());
    }
    return m_status;
}

void SceneInstantiation::Begin()
{
    // A game may load a scene before Engine::Init. Referencing the built-in registration
    // here also keeps those translation units in the static library's link set.
    RegisterBuiltinComponents();

    if (!m_document.is_object() || m_document.value("format", std::string()) != "geni.scene")
    {
        Abort("not a GeniEngine scene");
        return;
    }
    if (m_document.value("version", 0) != 1)
    {
        Abort("unsupported scene version");
        return;
    }
    auto nodesIt = m_document.find("nodes");
    if (nodesIt == m_document.end() || !nodesIt->is_array())
    {
        Abort("scene has no nodes array");
        return;
    }
    m_nodes = &*nodesIt;
    if (m_clear)
    {
        m_scene.Clear();
    }
    m_objects.clear();
    m_objects.reserve(m_nodes->size());
    m_cursor = 0;
    m_phase = Phase::Objects;
}

void SceneInstantiation::CreateObjects(size_t &budget)
{
    // Pass 1: objects and hierarchy. Parents are expected before children (the cooker and
    // SaveToJson both write a depth-first order); a parent index is still validated.
    const auto &nodes = *m_nodes;
    for (; m_cursor < nodes.size() && budget > 0; ++m_cursor, --budget)
    {
        const auto &node = nodes[m_cursor];
        if (!node.is_object())
        {
            Abort("node " + std::to_string(m_cursor) + " is not an object");
            return;
        }
        const int parentIndex = node.value("parent", -1);
        if (parentIndex >= static_cast<int>(m_cursor))
        {
            Abort("node " + std::to_string(m_cursor) + " has a parent that comes later in the file");
            return;
        }
        GameObject *parent = parentIndex >= 0 ? m_objects[static_cast<size_t>(parentIndex)] : nullptr;
        GameObject *object = m_scene.CreateObject(node.value("name", std::string("node")), parent);
        m_objects.push_back(object);
        object->SetPosition(Vec3FromJson(node.value("position", nlohmann::json()), glm::vec3(0.0f)));
        object->SetRotation(QuatFromJson(node.value("rotation", nlohmann::json()), glm::quat(1, 0, 0, 0)));
        object->SetScale(Vec3FromJson(node.value("scale", nlohmann::json()), glm::vec3(1.0f)));
        object->SetActive(node.value("active", true));
    }
    if (m_cursor == nodes.size())
    {
        m_cursor = 0;
        m_phase = Phase::Components;
    }
}

void SceneInstantiation::CreateComponents(size_t &budget)
{
    // Pass 2: components, after every object exists so skeleton joints can be resolved.
    const auto &nodes = *m_nodes;
    for (; m_cursor < nodes.size() && budget > 0; ++m_cursor, --budget)
    {
        auto componentsIt = nodes[m_cursor].find("components");
        if (componentsIt == nodes[m_cursor].end() || !componentsIt->is_array())
        {
            continue;
        }
        for (const auto &entry : *componentsIt)
        {
            const std::string typeName = entry.value("type", std::string());
            // Held by a unique_ptr until it is added, so a throwing setter cannot leak it.
            // Properties are set before AddComponent because AddComponent runs Init.
            std::unique_ptr<Component> component(TypeRegistry::Get().Create(typeName));
            if (!component)
            {
                GENI_LOG_WARN("scene: unknown component type '%s'", typeName.c_str());
                continue;
            }
            TypeRegistry::Get().Deserialize(
                typeName, *component, entry.value("properties", nlohmann::json::object()), m_context
            );
            Component *added = component.release();
            m_objects[m_cursor]->AddComponent(added);
            added->ResolveAssets(m_context);
        }
    }
    if (m_cursor == nodes.size())
    {
        m_phase = Phase::Finished;
    }
}

void SceneInstantiation::Finish()
{
    const int cameraIndex = m_document.value("mainCamera", -1);
    if (cameraIndex >= 0 && cameraIndex < static_cast<int>(m_objects.size()))
    {
        m_scene.SetMainCamera(m_objects[static_cast<size_t>(cameraIndex)]);
    }
    m_status = Status::Done;
}

void SceneInstantiation::Abort(const std::string &message)
{
    m_error = message;
    m_status = Status::Failed;
    m_phase = Phase::Finished;
    if (m_clear)
    {
        m_scene.Clear();
    }
    else
    {
        // Remove only what this document added; children go with their roots.
        for (GameObject *object : m_objects)
        {
            if (object && (!object->GetParent() ||
                           std::find(m_objects.begin(), m_objects.end(), object->GetParent()) == m_objects.end()))
            {
                object->MarkForDestroy();
            }
        }
    }
    m_objects.clear();
}

bool SceneSerializer::Load(
    Scene &scene, const nlohmann::json &document, SerializationContext &context, std::string *error, bool clear
)
{
    SceneInstantiation instantiation(scene, document, context, clear);
    if (instantiation.Step(static_cast<size_t>(-1)) != SceneInstantiation::Status::Done)
    {
        Fail(error, instantiation.GetError());
        return false;
    }
    return true;
}

bool SceneSerializer::LoadFromFile(
    Scene &scene, const std::filesystem::path &path, SerializationContext &context, std::string *error
)
{
    std::ifstream file(path);
    if (!file)
    {
        Fail(error, "cannot open " + path.string());
        return false;
    }
    nlohmann::json document;
    try
    {
        file >> document;
    }
    catch (const std::exception &e)
    {
        Fail(error, e.what());
        return false;
    }
    return Load(scene, document, context, error);
}

bool SceneSerializer::Instantiate(
    Scene &scene, const SceneAsset &asset, SerializationContext &context, std::string *error
)
{
    return Load(scene, asset.document, context, error);
}

} // namespace Geni
