#include "EditorDocument.h"

#include "assets/AssetManager.h"
#include "core/Log.h"
#include "scene/GameObject.h"
#include "scene/Reflection.h"
#include "scene/Scene.h"
#include "scene/SceneSerializer.h"
#include "scene/components/CameraComponent.h"
#include "scene/components/LightComponent.h"
#include "scene/components/MeshComponent.h"

#include <algorithm>
#include <fstream>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <memory>

namespace Geni::Editor
{

namespace
{

void SetError(std::string *error, const std::string &message)
{
    if (error)
    {
        *error = message;
    }
}

bool HasSelectedAncestor(GameObject *object, const std::vector<GameObject *> &selected)
{
    for (GameObject *node = object->GetParent(); node; node = node->GetParent())
    {
        if (std::find(selected.begin(), selected.end(), node) != selected.end())
        {
            return true;
        }
    }
    return false;
}

void CollectPrefabInstances(GameObject &object, AssetID prefab, std::vector<GameObject *> &out)
{
    if (object.GetPrefab() == prefab)
    {
        out.push_back(&object);
        return; // its contents are rebuilt with it
    }
    for (const auto &child : object.GetChildren())
    {
        CollectPrefabInstances(*child, prefab, out);
    }
}

} // namespace

EditorDocument::EditorDocument(Scene &scene, AssetManager *assets) : m_scene(scene)
{
    m_context.scene = &scene;
    m_context.assets = assets;
}

void EditorDocument::ResetDocument()
{
    m_commands.Clear();
    m_selection.clear();
    m_playing = false;
    m_playSnapshot = nlohmann::json();
}

void EditorDocument::NewScene()
{
    m_scene.Clear();
    ResetDocument();
    m_path.clear();
    m_isPrefab = false;
    m_status = "New scene";
}

bool EditorDocument::Open(const std::filesystem::path &path, std::string *error)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        SetError(error, "cannot open " + path.string());
        return false;
    }
    nlohmann::json document;
    try
    {
        file >> document;
    }
    catch (const std::exception &e)
    {
        SetError(error, path.string() + ": " + e.what());
        return false;
    }

    SerializationContext serialization{&m_scene, m_context.assets};
    std::string loadError;
    if (!SceneSerializer::Load(m_scene, document, serialization, &loadError))
    {
        SetError(error, path.string() + ": " + loadError);
        ResetDocument();
        m_path.clear();
        return false;
    }
    ResetDocument();
    m_path = path;
    m_isPrefab = document.value("format", std::string()) == "geni.prefab";
    m_status = "Opened " + path.filename().string();
    return true;
}

bool EditorDocument::Save(std::string *error)
{
    if (m_path.empty())
    {
        SetError(error, "the scene has no file yet; use Save As");
        return false;
    }
    return SaveAs(m_path, error);
}

bool EditorDocument::SaveAs(const std::filesystem::path &path, std::string *error)
{
    if (m_playing)
    {
        SetError(error, "stop playing before saving");
        return false;
    }
    const bool prefab = path.extension() == ".gprefab";
    if (prefab)
    {
        if (m_scene.GetObjects().size() != 1)
        {
            SetError(error, "a prefab has exactly one root object");
            return false;
        }
        if (!SceneSerializer::SavePrefab(*m_scene.GetObjects().front(), path, error))
        {
            return false;
        }
    }
    else if (!SceneSerializer::Save(m_scene, path, error))
    {
        return false;
    }
    m_path = path;
    m_isPrefab = prefab;
    m_commands.MarkSaved();
    m_status = "Saved " + path.filename().string();
    return true;
}

// --- Selection ---------------------------------------------------------------------

void EditorDocument::Select(uint64_t id, bool additive)
{
    if (!additive)
    {
        m_selection.clear();
    }
    if (id == 0)
    {
        return;
    }
    m_selection.erase(std::remove(m_selection.begin(), m_selection.end(), id), m_selection.end());
    m_selection.push_back(id);
}

void EditorDocument::ToggleSelected(uint64_t id)
{
    auto it = std::find(m_selection.begin(), m_selection.end(), id);
    if (it != m_selection.end())
    {
        m_selection.erase(it);
    }
    else if (id != 0)
    {
        m_selection.push_back(id);
    }
}

void EditorDocument::ClearSelection()
{
    m_selection.clear();
}

bool EditorDocument::IsSelected(uint64_t id) const
{
    return std::find(m_selection.begin(), m_selection.end(), id) != m_selection.end();
}

GameObject *EditorDocument::GetPrimary()
{
    return m_selection.empty() ? nullptr : m_scene.FindObjectByID(m_selection.back());
}

std::vector<GameObject *> EditorDocument::GetSelectedObjects()
{
    std::vector<GameObject *> objects;
    for (uint64_t id : m_selection)
    {
        if (GameObject *object = m_scene.FindObjectByID(id))
        {
            objects.push_back(object);
        }
    }
    return objects;
}

std::vector<GameObject *> EditorDocument::GetSelectedRoots()
{
    const std::vector<GameObject *> selected = GetSelectedObjects();
    std::vector<GameObject *> roots;
    for (GameObject *object : selected)
    {
        if (!HasSelectedAncestor(object, selected))
        {
            roots.push_back(object);
        }
    }
    return roots;
}

void EditorDocument::PruneSelection()
{
    m_selection.erase(
        std::remove_if(
            m_selection.begin(), m_selection.end(), [this](uint64_t id) { return !m_scene.FindObjectByID(id); }
        ),
        m_selection.end()
    );
}

bool EditorDocument::CanEdit(const GameObject *object) const
{
    return object && !m_playing && !object->IsFromPrefab();
}

// --- Creating ----------------------------------------------------------------------

void EditorDocument::RecordCreated(const char *name, const std::vector<GameObject *> &roots, bool select)
{
    m_commands.Push(std::make_unique<CreateObjectsCommand>(name, roots));
    if (select)
    {
        m_selection.clear();
        for (GameObject *root : roots)
        {
            m_selection.push_back(root->GetID());
        }
    }
}

GameObject *EditorDocument::CreateEmpty(const std::string &name, GameObject *parent)
{
    if (m_playing || (parent && parent->IsFromPrefab()))
    {
        return nullptr;
    }
    GameObject *object = m_scene.CreateObject(name, parent);
    RecordCreated("Create object", {object}, true);
    return object;
}

GameObject *EditorDocument::CreateLight(LightType type)
{
    if (m_playing)
    {
        return nullptr;
    }
    const char *name = type == LightType::Directional ? "Directional Light" : type == LightType::Spot ? "Spot Light" : "Point Light";
    GameObject *object = m_scene.CreateObject(name);
    auto *light = new LightComponent();
    light->SetType(type);
    if (type == LightType::Directional)
    {
        // Down and a little sideways, so a new sun lights the tops and sides of things.
        object->SetRotation(glm::quatLookAt(glm::normalize(glm::vec3(-0.4f, -0.8f, -0.4f)), glm::vec3(0, 1, 0)));
    }
    else
    {
        object->SetPosition(glm::vec3(0.0f, 3.0f, 0.0f));
        if (type == LightType::Spot)
        {
            object->SetRotation(glm::quatLookAt(glm::vec3(0.0f, -1.0f, 0.0f), glm::vec3(0, 0, -1)));
        }
    }
    object->AddComponent(light);
    RecordCreated("Create light", {object}, true);
    return object;
}

GameObject *EditorDocument::CreateCamera()
{
    if (m_playing)
    {
        return nullptr;
    }
    GameObject *object = m_scene.CreateObject("Camera");
    object->SetPosition(glm::vec3(0.0f, 2.0f, 8.0f));
    object->AddComponent(new CameraComponent());
    const bool first = m_scene.GetMainCamera() == nullptr;
    if (first)
    {
        m_scene.SetMainCamera(object);
    }
    auto command = std::make_unique<CompositeCommand>("Create camera");
    command->Add(std::make_unique<CreateObjectsCommand>("Create camera", std::vector<GameObject *>{object}));
    if (first)
    {
        command->Add(std::make_unique<SetMainCameraCommand>(0, object->GetID()));
    }
    m_commands.Push(std::move(command));
    Select(object->GetID());
    return object;
}

GameObject *EditorDocument::CreateMeshObject(AssetID mesh, AssetID material, const std::string &name)
{
    if (m_playing)
    {
        return nullptr;
    }
    GameObject *object = m_scene.CreateObject(name);
    auto *component = new MeshComponent();
    component->SetMeshID(mesh);
    component->SetMaterialID(material);
    object->AddComponent(component);
    SerializationContext serialization{&m_scene, m_context.assets};
    component->ResolveAssets(serialization);
    RecordCreated("Create mesh", {object}, true);
    return object;
}

GameObject *EditorDocument::InstantiatePrefab(AssetID prefab, GameObject *parent)
{
    if (m_playing || (parent && parent->IsFromPrefab()))
    {
        return nullptr;
    }
    SerializationContext serialization{&m_scene, m_context.assets};
    GameObject *instance = SceneSerializer::InstantiatePrefab(m_scene, prefab, serialization, parent);
    if (!instance)
    {
        m_status = "Could not instantiate prefab " + prefab.ToHex();
        return nullptr;
    }
    RecordCreated("Instantiate prefab", {instance}, true);
    return instance;
}

bool EditorDocument::ImportScene(const nlohmann::json &document, std::string *error)
{
    if (m_playing)
    {
        SetError(error, "stop playing before importing");
        return false;
    }
    SerializationContext serialization{&m_scene, m_context.assets};
    std::vector<GameObject *> roots;
    if (!SceneSerializer::InstantiateSubtree(m_scene, document, serialization, nullptr, &roots, error))
    {
        return false;
    }
    auto command = std::make_unique<CompositeCommand>("Import");
    command->Add(std::make_unique<CreateObjectsCommand>("Import", roots));
    // A scene without a camera takes the imported one, as loading the file would.
    if (!m_scene.GetMainCamera())
    {
        const int cameraIndex = document.value("mainCamera", -1);
        if (cameraIndex >= 0)
        {
            // Node order is depth-first, the same order the objects were created in.
            std::vector<GameObject *> flat;
            std::vector<GameObject *> stack(roots.rbegin(), roots.rend());
            while (!stack.empty())
            {
                GameObject *object = stack.back();
                stack.pop_back();
                flat.push_back(object);
                const auto &children = object->GetChildren();
                for (auto it = children.rbegin(); it != children.rend(); ++it)
                {
                    stack.push_back(it->get());
                }
            }
            if (static_cast<size_t>(cameraIndex) < flat.size())
            {
                m_scene.SetMainCamera(flat[static_cast<size_t>(cameraIndex)]);
                command->Add(std::make_unique<SetMainCameraCommand>(0, flat[static_cast<size_t>(cameraIndex)]->GetID()));
            }
        }
    }
    m_commands.Push(std::move(command));
    m_selection.clear();
    for (GameObject *root : roots)
    {
        m_selection.push_back(root->GetID());
    }
    return true;
}

// --- Structure ---------------------------------------------------------------------

bool EditorDocument::DeleteSelection()
{
    if (m_playing)
    {
        return false;
    }
    std::vector<GameObject *> roots = GetSelectedRoots();
    roots.erase(
        std::remove_if(roots.begin(), roots.end(), [](GameObject *object) { return object->IsFromPrefab(); }),
        roots.end()
    );
    if (roots.empty())
    {
        return false;
    }
    m_commands.Execute(std::make_unique<DeleteObjectsCommand>(m_context, roots), m_context);
    PruneSelection();
    return true;
}

std::vector<GameObject *> EditorDocument::DuplicateSelection()
{
    return DuplicateGrid(2, 1, glm::vec3(0.0f));
}

std::vector<GameObject *> EditorDocument::DuplicateGrid(int countX, int countZ, const glm::vec3 &spacing)
{
    std::vector<GameObject *> created;
    if (m_playing || countX < 1 || countZ < 1)
    {
        return created;
    }
    std::vector<GameObject *> roots = GetSelectedRoots();
    roots.erase(
        std::remove_if(roots.begin(), roots.end(), [](GameObject *object) { return object->IsFromPrefab(); }),
        roots.end()
    );
    if (roots.empty())
    {
        return created;
    }

    SerializationContext serialization{&m_scene, m_context.assets};
    for (GameObject *root : roots)
    {
        const nlohmann::json document = SceneSerializer::SaveSubtree({root});
        GameObject *parent = root->GetParent();
        // The offset is given in world axes; a copy lives in its parent's space.
        const glm::mat3 toLocal = parent ? glm::mat3(glm::inverse(parent->GetWorldTransform())) : glm::mat3(1.0f);
        for (int z = 0; z < countZ; ++z)
        {
            for (int x = 0; x < countX; ++x)
            {
                if (x == 0 && z == 0)
                {
                    continue; // the original
                }
                std::vector<GameObject *> copies;
                if (!SceneSerializer::InstantiateSubtree(m_scene, document, serialization, parent, &copies))
                {
                    continue;
                }
                const glm::vec3 offset(spacing.x * static_cast<float>(x), 0.0f, spacing.z * static_cast<float>(z));
                for (GameObject *copy : copies)
                {
                    copy->SetPosition(copy->GetPosition() + toLocal * offset);
                    created.push_back(copy);
                }
            }
        }
    }
    if (!created.empty())
    {
        RecordCreated(countX * countZ > 2 ? "Duplicate as grid" : "Duplicate", created, true);
    }
    return created;
}

bool EditorDocument::Reparent(
    const std::vector<GameObject *> &objects, GameObject *newParent, int index, bool keepWorldTransform
)
{
    if (m_playing || (newParent && newParent->IsFromPrefab()))
    {
        return false;
    }
    std::vector<ReparentCommand::Entry> moves;
    std::vector<NodeStateCommand::Entry> states;
    for (GameObject *object : objects)
    {
        if (!object || object->IsFromPrefab() || object == newParent)
        {
            continue;
        }
        // An object cannot move under itself or its own descendants.
        bool cycle = false;
        for (GameObject *node = newParent; node; node = node->GetParent())
        {
            cycle = cycle || node == object;
        }
        if (cycle)
        {
            continue;
        }

        ReparentCommand::Entry move;
        move.id = object->GetID();
        move.oldParent = object->GetParent() ? object->GetParent()->GetID() : 0;
        move.oldIndex = object->GetSiblingIndex();
        move.newParent = newParent ? newParent->GetID() : 0;
        move.newIndex = index;

        const NodeState before = NodeState::Capture(*object);
        const glm::mat4 world = object->GetWorldTransform();
        if (!m_scene.MoveObject(object, newParent, index))
        {
            continue;
        }
        move.newIndex = object->GetSiblingIndex();
        moves.push_back(move);

        if (keepWorldTransform)
        {
            const glm::mat4 parentWorld = newParent ? newParent->GetWorldTransform() : glm::mat4(1.0f);
            const glm::mat4 local = glm::inverse(parentWorld) * world;
            glm::vec3 scale;
            glm::quat rotation;
            glm::vec3 translation;
            glm::vec3 skew;
            glm::vec4 perspective;
            if (glm::decompose(local, scale, rotation, translation, skew, perspective))
            {
                object->SetPosition(translation);
                object->SetRotation(glm::normalize(rotation));
                object->SetScale(scale);
            }
            const NodeState after = NodeState::Capture(*object);
            if (after != before)
            {
                states.push_back({object->GetID(), before, after});
            }
        }
    }
    if (moves.empty())
    {
        return false;
    }
    auto command = std::make_unique<CompositeCommand>("Move in hierarchy");
    command->Add(std::make_unique<ReparentCommand>(std::move(moves)));
    if (!states.empty())
    {
        command->Add(std::make_unique<NodeStateCommand>("Keep world transform", std::move(states)));
    }
    m_commands.Push(std::move(command));
    return true;
}

// --- Object and component state ------------------------------------------------------

bool EditorDocument::CommitNodeStates(const char *name, const std::vector<std::pair<uint64_t, NodeState>> &before)
{
    std::vector<NodeStateCommand::Entry> entries;
    for (const auto &[id, state] : before)
    {
        GameObject *object = m_scene.FindObjectByID(id);
        if (!object)
        {
            continue;
        }
        const NodeState after = NodeState::Capture(*object);
        if (after != state)
        {
            entries.push_back({id, state, after});
        }
    }
    if (entries.empty())
    {
        return false;
    }
    m_commands.Push(std::make_unique<NodeStateCommand>(name, std::move(entries)));
    return true;
}

bool EditorDocument::SetNodeState(GameObject &object, const NodeState &state, const char *name)
{
    if (!CanEdit(&object))
    {
        return false;
    }
    const NodeState before = NodeState::Capture(object);
    if (before == state)
    {
        return false;
    }
    m_commands.Execute(
        std::make_unique<NodeStateCommand>(name, std::vector<NodeStateCommand::Entry>{{object.GetID(), before, state}}),
        m_context
    );
    return true;
}

bool EditorDocument::SetProperty(
    GameObject &object, int component, const std::string &property, const nlohmann::json &value
)
{
    if (!CanEdit(&object) || object.IsPrefabInstance() || component < 0 ||
        static_cast<size_t>(component) >= object.GetComponents().size())
    {
        return false;
    }
    const Component &target = *object.GetComponents()[static_cast<size_t>(component)];
    const nlohmann::json properties = TypeRegistry::Get().Serialize(target.GetTypeName(), target);
    auto before = properties.find(property);
    if (before == properties.end())
    {
        return false;
    }
    if (!PropertyCommand::Set(m_context, object.GetID(), component, property, value))
    {
        return false;
    }
    return CommitProperty(object.GetID(), component, property, *before);
}

bool EditorDocument::CommitProperty(
    uint64_t object, int component, const std::string &property, const nlohmann::json &before
)
{
    GameObject *target = m_scene.FindObjectByID(object);
    if (!target || component < 0 || static_cast<size_t>(component) >= target->GetComponents().size())
    {
        return false;
    }
    const Component &live = *target->GetComponents()[static_cast<size_t>(component)];
    const nlohmann::json properties = TypeRegistry::Get().Serialize(live.GetTypeName(), live);
    auto after = properties.find(property);
    if (after == properties.end() || *after == before)
    {
        return false;
    }
    m_commands.Push(std::make_unique<PropertyCommand>(object, component, property, before, *after));
    return true;
}

bool EditorDocument::AddComponent(GameObject &object, const std::string &type)
{
    if (!CanEdit(&object) || object.IsPrefabInstance() || !TypeRegistry::Get().Find(type))
    {
        return false;
    }
    m_commands.Execute(
        std::make_unique<AddComponentCommand>(object.GetID(), type, nlohmann::json::object()), m_context
    );
    return true;
}

bool EditorDocument::RemoveComponent(GameObject &object, int index)
{
    if (!CanEdit(&object) || object.IsPrefabInstance() || index < 0 ||
        static_cast<size_t>(index) >= object.GetComponents().size())
    {
        return false;
    }
    m_commands.Execute(std::make_unique<RemoveComponentCommand>(object.GetID(), index), m_context);
    return true;
}

bool EditorDocument::SetMainCamera(GameObject *camera)
{
    if (m_playing || camera == m_scene.GetMainCamera())
    {
        return false;
    }
    const uint64_t before = m_scene.GetMainCamera() ? m_scene.GetMainCamera()->GetID() : 0;
    m_commands.Execute(std::make_unique<SetMainCameraCommand>(before, camera ? camera->GetID() : 0), m_context);
    return true;
}

// --- Prefabs -----------------------------------------------------------------------

bool EditorDocument::CreatePrefab(
    GameObject &object, const std::filesystem::path &path, AssetID prefab, std::string *error
)
{
    if (!CanEdit(&object))
    {
        SetError(error, "this object cannot be edited");
        return false;
    }
    if (!prefab.IsValid())
    {
        SetError(error, "the prefab has no asset ID");
        return false;
    }
    if (!SceneSerializer::SavePrefab(object, path, error))
    {
        return false;
    }
    if (m_context.assets)
    {
        // The file may replace an older prefab that is already loaded.
        m_context.assets->Reload(prefab);
    }

    const SubtreeSnapshot before = SubtreeSnapshot::Capture(object);
    const uint64_t id = object.GetID();
    // The object becomes an instance: what it had now comes from the prefab file.
    nlohmann::json instance = before.document;
    instance["nodes"] = nlohmann::json::array({before.document["nodes"][0]});
    instance["nodes"][0]["prefab"] = prefab.ToHex();
    instance["nodes"][0]["components"] = nlohmann::json::array();
    SubtreeSnapshot after = before;
    after.document = std::move(instance);

    auto command = std::make_unique<ReplaceObjectCommand>("Create prefab", before, after);
    m_commands.Execute(std::move(command), m_context);
    Select(id);
    m_status = "Created prefab " + path.filename().string();
    return true;
}

bool EditorDocument::UnpackPrefab(GameObject &instance)
{
    if (!CanEdit(&instance) || !instance.IsPrefabInstance())
    {
        return false;
    }
    const SubtreeSnapshot before = SubtreeSnapshot::Capture(instance);
    SceneSerializer::UnpackPrefabInstance(instance);
    const SubtreeSnapshot after = SubtreeSnapshot::Capture(instance);
    m_commands.Push(std::make_unique<ReplaceObjectCommand>("Unpack prefab", before, after));
    return true;
}

int EditorDocument::RefreshPrefabInstances(AssetID prefab)
{
    std::vector<GameObject *> instances;
    for (const auto &root : m_scene.GetObjects())
    {
        CollectPrefabInstances(*root, prefab, instances);
    }
    SerializationContext serialization{&m_scene, m_context.assets};
    for (GameObject *instance : instances)
    {
        SceneSerializer::RefreshPrefabInstance(*instance, serialization);
    }
    return static_cast<int>(instances.size());
}

// --- Undo --------------------------------------------------------------------------

bool EditorDocument::Undo()
{
    if (m_playing || !m_commands.CanUndo())
    {
        return false;
    }
    m_status = std::string("Undo: ") + m_commands.GetUndoName();
    m_commands.Undo(m_context);
    PruneSelection();
    return true;
}

bool EditorDocument::Redo()
{
    if (m_playing || !m_commands.CanRedo())
    {
        return false;
    }
    m_status = std::string("Redo: ") + m_commands.GetRedoName();
    m_commands.Redo(m_context);
    PruneSelection();
    return true;
}

// --- Play --------------------------------------------------------------------------

bool EditorDocument::RestoreSnapshot()
{
    m_scene.Clear();
    SerializationContext serialization{&m_scene, m_context.assets};
    std::string error;
    if (!SceneSerializer::InstantiateSubtree(m_scene, m_playSnapshot, serialization, nullptr, nullptr, &error, true))
    {
        GENI_LOG_ERROR("editor: could not restore the scene: %s", error.c_str());
        m_status = "Could not restore the scene: " + error;
        return false;
    }
    m_scene.SetMainCamera(m_scene.FindObjectByID(m_playMainCamera));
    return true;
}

bool EditorDocument::Play()
{
    if (m_playing)
    {
        return false;
    }
    std::vector<GameObject *> roots;
    for (const auto &root : m_scene.GetObjects())
    {
        roots.push_back(root.get());
    }
    m_playSnapshot = SceneSerializer::SaveSubtree(roots, true);
    m_playMainCamera = m_scene.GetMainCamera() ? m_scene.GetMainCamera()->GetID() : 0;
    // Restart from the snapshot: components such as rigid bodies take their start state
    // when they are created, and the edited values may be newer than that.
    if (!RestoreSnapshot())
    {
        return false;
    }
    m_playing = true;
    m_status = "Playing";
    return true;
}

bool EditorDocument::Stop()
{
    if (!m_playing)
    {
        return false;
    }
    m_playing = false;
    const bool restored = RestoreSnapshot();
    m_playSnapshot = nlohmann::json();
    PruneSelection();
    if (restored)
    {
        m_status = "Stopped";
    }
    return restored;
}

} // namespace Geni::Editor
