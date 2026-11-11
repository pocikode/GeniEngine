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

constexpr int kMaxPrefabDepth = 8;

// Depth-first list of what gets written: every root, and below it everything that did
// not come from a prefab (a prefab's contents live in the prefab file).
void CollectNodes(GameObject *object, int parentIndex, std::vector<GameObject *> &nodes, std::vector<int> &parents)
{
    const int index = static_cast<int>(nodes.size());
    nodes.push_back(object);
    parents.push_back(parentIndex);
    for (const auto &child : object->GetChildren())
    {
        if (!child->IsFromPrefab())
        {
            CollectNodes(child.get(), index, nodes, parents);
        }
    }
}

nlohmann::ordered_json WriteNodes(const std::vector<GameObject *> &nodes, const std::vector<int> &parents, bool includeIds)
{
    nlohmann::ordered_json nodeArray = nlohmann::ordered_json::array();
    for (size_t i = 0; i < nodes.size(); ++i)
    {
        GameObject *object = nodes[i];
        nlohmann::ordered_json node;
        node["name"] = object->GetName();
        node["parent"] = parents[i];
        if (includeIds)
        {
            node["id"] = object->GetID();
        }
        node["position"] = Vec3Json(object->GetPosition());
        node["rotation"] = QuatJson(object->GetRotation());
        node["scale"] = Vec3Json(object->GetScale());
        node["active"] = object->IsActive();

        nlohmann::ordered_json components = nlohmann::ordered_json::array();
        if (object->IsPrefabInstance())
        {
            // An instance's components come from the prefab every time it is loaded.
            node["prefab"] = object->GetPrefab().ToHex();
        }
        else
        {
            for (const auto &component : object->GetComponents())
            {
                const char *typeName = component->GetTypeName();
                nlohmann::ordered_json entry;
                entry["type"] = typeName;
                entry["properties"] = TypeRegistry::Get().Serialize(typeName, *component);
                components.push_back(std::move(entry));
            }
        }
        node["components"] = std::move(components);
        nodeArray.push_back(std::move(node));
    }
    return nodeArray;
}

bool CheckDocument(const nlohmann::json &document, std::string &why)
{
    const std::string format = document.is_object() ? document.value("format", std::string()) : std::string();
    if (format != "geni.scene" && format != "geni.prefab")
    {
        why = "not a GeniEngine scene";
        return false;
    }
    if (document.value("version", 0) != 1)
    {
        why = "unsupported scene version";
        return false;
    }
    auto nodes = document.find("nodes");
    if (nodes == document.end() || !nodes->is_array())
    {
        why = "scene has no nodes array";
        return false;
    }
    return true;
}

GameObject *CreateNodeObject(Scene &scene, const nlohmann::json &node, GameObject *parent, bool preserveIds)
{
    GameObject *object = scene.CreateObject(node.value("name", std::string("node")), parent);
    try
    {
        object->SetPosition(Vec3FromJson(node.value("position", nlohmann::json()), glm::vec3(0.0f)));
        object->SetRotation(QuatFromJson(node.value("rotation", nlohmann::json()), glm::quat(1, 0, 0, 0)));
        object->SetScale(Vec3FromJson(node.value("scale", nlohmann::json()), glm::vec3(1.0f)));
        object->SetActive(node.value("active", true));
        object->SetPrefab(AssetID::FromHex(node.value("prefab", std::string())));
        if (preserveIds)
        {
            const uint64_t id = node.value("id", static_cast<uint64_t>(0));
            if (id != 0 && !scene.SetObjectID(object, id))
            {
                GENI_LOG_WARN("scene: object ID %llu is already in use", static_cast<unsigned long long>(id));
            }
        }
    }
    catch (...)
    {
        // A wrongly typed value: the caller reports it; this half-made object must not stay.
        scene.DestroyObject(object);
        throw;
    }
    return object;
}

void CreateNodeComponents(GameObject &object, const nlohmann::json &node, SerializationContext &context)
{
    auto componentsIt = node.find("components");
    if (componentsIt == node.end() || !componentsIt->is_array())
    {
        return;
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
        TypeRegistry::Get().Deserialize(typeName, *component, entry.value("properties", nlohmann::json::object()), context);
        Component *added = component.release();
        object.AddComponent(added);
        added->ResolveAssets(context);
    }
}

bool ExpandPrefab(GameObject &instance, SerializationContext &context, int depth);

// Fills `instance` from a prefab document: the document's first node is the instance
// itself (only its components are taken; the instance keeps its own name and transform),
// every other node becomes a descendant flagged FromPrefab.
void MergePrefabDocument(GameObject &instance, const nlohmann::json &document, SerializationContext &context, int depth)
{
    Scene &scene = *instance.GetScene();
    const auto &nodes = document.at("nodes");
    std::vector<GameObject *> objects;
    objects.reserve(nodes.size());
    for (size_t i = 0; i < nodes.size(); ++i)
    {
        const auto &node = nodes[i];
        if (i == 0)
        {
            objects.push_back(&instance);
            continue;
        }
        const int parentIndex = node.value("parent", -1);
        GameObject *parent =
            parentIndex >= 0 && parentIndex < static_cast<int>(i) ? objects[static_cast<size_t>(parentIndex)] : &instance;
        GameObject *object = CreateNodeObject(scene, node, parent, false);
        object->SetFromPrefab(true);
        objects.push_back(object);
    }
    for (size_t i = 0; i < nodes.size(); ++i)
    {
        if (i > 0 && objects[i]->IsPrefabInstance())
        {
            ExpandPrefab(*objects[i], context, depth + 1);
        }
        else
        {
            CreateNodeComponents(*objects[i], nodes[i], context);
        }
    }
}

bool ExpandPrefab(GameObject &instance, SerializationContext &context, int depth)
{
    const std::string id = instance.GetPrefab().ToHex();
    if (depth > kMaxPrefabDepth)
    {
        GENI_LOG_ERROR("prefab %s: nested more than %d deep (a prefab that contains itself?)", id.c_str(), kMaxPrefabDepth);
        return false;
    }
    if (!context.assets || !instance.GetScene())
    {
        GENI_LOG_WARN("prefab %s: no asset manager to load it from", id.c_str());
        return false;
    }
    // Synchronous: a prefab is a small text file, parsed once and then cached.
    const auto prefab = context.assets->LoadPrefab(instance.GetPrefab(), false);
    if (!prefab.IsReady())
    {
        GENI_LOG_WARN("prefab %s for '%s' did not load", id.c_str(), instance.GetName().c_str());
        return false;
    }
    std::string why;
    if (!CheckDocument(prefab->document, why) || prefab->document.at("nodes").empty())
    {
        GENI_LOG_WARN("prefab %s: %s", id.c_str(), why.empty() ? "no nodes" : why.c_str());
        return false;
    }
    try
    {
        MergePrefabDocument(instance, prefab->document, context, depth);
    }
    catch (const std::exception &e)
    {
        GENI_LOG_WARN("prefab %s is malformed: %s", id.c_str(), e.what());
        return false;
    }
    return true;
}

} // namespace

nlohmann::ordered_json SceneSerializer::SaveToJson(const Scene &scene)
{
    std::vector<GameObject *> nodes;
    std::vector<int> parents;
    for (const auto &root : scene.GetObjects())
    {
        CollectNodes(root.get(), -1, nodes, parents);
    }

    nlohmann::ordered_json document;
    document["format"] = "geni.scene";
    document["version"] = 1;
    document["nodes"] = WriteNodes(nodes, parents, false);

    const auto cameraIt = std::find(nodes.begin(), nodes.end(), scene.GetMainCamera());
    document["mainCamera"] = cameraIt == nodes.end() ? -1 : static_cast<int>(cameraIt - nodes.begin());
    return document;
}

nlohmann::ordered_json SceneSerializer::SaveSubtree(
    const std::vector<GameObject *> &roots, bool includeIds, const char *format
)
{
    std::vector<GameObject *> nodes;
    std::vector<int> parents;
    for (GameObject *root : roots)
    {
        if (root)
        {
            CollectNodes(root, -1, nodes, parents);
        }
    }
    nlohmann::ordered_json document;
    document["format"] = format;
    document["version"] = 1;
    document["nodes"] = WriteNodes(nodes, parents, includeIds);
    document["mainCamera"] = -1;
    return document;
}

std::string SceneSerializer::SaveToText(const Scene &scene)
{
    return cooked::FormatSceneText(SaveToJson(scene));
}

namespace
{
bool WriteTextFile(const std::filesystem::path &path, const std::string &text, std::string *error)
{
    std::error_code ec;
    if (!path.parent_path().empty())
    {
        std::filesystem::create_directories(path.parent_path(), ec);
    }
    std::ofstream file(path, std::ios::binary);
    if (!file)
    {
        Fail(error, "cannot open " + path.string());
        return false;
    }
    file << text;
    if (!file.good())
    {
        Fail(error, "could not write " + path.string());
        return false;
    }
    return true;
}
} // namespace

bool SceneSerializer::Save(const Scene &scene, const std::filesystem::path &path, std::string *error)
{
    return WriteTextFile(path, SaveToText(scene), error);
}

bool SceneSerializer::SavePrefab(GameObject &root, const std::filesystem::path &path, std::string *error)
{
    if (root.IsPrefabInstance())
    {
        Fail(error, "'" + root.GetName() + "' is a prefab instance; unpack it before making a prefab of it");
        return false;
    }
    return WriteTextFile(path, cooked::FormatSceneText(SaveSubtree({&root}, false, "geni.prefab")), error);
}

GameObject *SceneSerializer::InstantiatePrefab(
    Scene &scene, AssetID prefab, SerializationContext &context, GameObject *parent
)
{
    if (!context.assets)
    {
        return nullptr;
    }
    const auto asset = context.assets->LoadPrefab(prefab, false);
    if (!asset.IsReady())
    {
        return nullptr;
    }
    std::string name = "Prefab";
    try
    {
        name = asset->document.at("nodes").at(0).value("name", name);
    }
    catch (const std::exception &)
    {
        return nullptr;
    }
    GameObject *instance = scene.CreateObject(name, parent);
    instance->SetPrefab(prefab);
    if (!ExpandPrefab(*instance, context, 0))
    {
        scene.DestroyObject(instance);
        return nullptr;
    }
    return instance;
}

bool SceneSerializer::RefreshPrefabInstance(GameObject &instance, SerializationContext &context)
{
    Scene *scene = instance.GetScene();
    if (!scene || !instance.IsPrefabInstance())
    {
        return false;
    }
    while (!instance.GetComponents().empty())
    {
        instance.RemoveComponent(instance.GetComponents().back().get());
    }
    std::vector<GameObject *> stale;
    for (const auto &child : instance.GetChildren())
    {
        if (child->IsFromPrefab())
        {
            stale.push_back(child.get());
        }
    }
    for (GameObject *child : stale)
    {
        scene->DestroyObject(child);
    }
    return ExpandPrefab(instance, context, 0);
}

namespace
{
void ClearFromPrefab(GameObject &object)
{
    for (const auto &child : object.GetChildren())
    {
        if (child->IsFromPrefab())
        {
            child->SetFromPrefab(false);
            // A nested instance stays an instance; its contents still belong to its prefab.
            if (!child->IsPrefabInstance())
            {
                ClearFromPrefab(*child);
            }
        }
    }
}
} // namespace

void SceneSerializer::UnpackPrefabInstance(GameObject &instance)
{
    instance.SetPrefab(AssetID{});
    ClearFromPrefab(instance);
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

    std::string why;
    if (!CheckDocument(m_document, why))
    {
        Abort(why);
        return;
    }
    m_nodes = &*m_document.find("nodes");
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
        GameObject *parent = parentIndex >= 0 ? m_objects[static_cast<size_t>(parentIndex)] : m_attachParent;
        m_objects.push_back(CreateNodeObject(m_scene, node, parent, m_preserveIds));
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
    // A prefab instance gets its components and children from the prefab instead.
    const auto &nodes = *m_nodes;
    for (; m_cursor < nodes.size() && budget > 0; ++m_cursor, --budget)
    {
        GameObject &object = *m_objects[m_cursor];
        if (object.IsPrefabInstance())
        {
            ExpandPrefab(object, m_context, 0);
        }
        else
        {
            CreateNodeComponents(object, nodes[m_cursor], m_context);
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
    else if (m_nodes)
    {
        // Remove only what this document added; children go with their roots.
        for (size_t i = 0; i < m_objects.size(); ++i)
        {
            if ((*m_nodes)[i].value("parent", -1) < 0)
            {
                m_scene.DestroyObject(m_objects[i]);
            }
        }
    }
    m_objects.clear();
}

bool SceneSerializer::InstantiateSubtree(
    Scene &scene, const nlohmann::json &document, SerializationContext &context, GameObject *parent,
    std::vector<GameObject *> *roots, std::string *error, bool preserveIds
)
{
    SceneInstantiation instantiation(scene, document, context, false);
    instantiation.m_attachParent = parent;
    instantiation.m_preserveIds = preserveIds;
    // The main camera belongs to a whole scene; a subtree must not move it.
    GameObject *camera = scene.GetMainCamera();
    const bool done = instantiation.Step(static_cast<size_t>(-1)) == SceneInstantiation::Status::Done;
    scene.SetMainCamera(camera);
    if (!done)
    {
        Fail(error, instantiation.GetError());
        return false;
    }
    if (roots)
    {
        roots->clear();
        const auto &nodes = document.at("nodes");
        for (size_t i = 0; i < nodes.size(); ++i)
        {
            if (nodes[i].value("parent", -1) < 0)
            {
                roots->push_back(instantiation.m_objects[i]);
            }
        }
    }
    return true;
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
