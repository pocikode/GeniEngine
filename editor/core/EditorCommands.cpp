#include "EditorCommands.h"

#include "core/Log.h"
#include "scene/GameObject.h"
#include "scene/Reflection.h"
#include "scene/Scene.h"
#include "scene/SceneSerializer.h"

#include <algorithm>
#include <memory>

namespace Geni::Editor
{

namespace
{

GameObject *Find(EditContext &context, uint64_t id)
{
    return context.scene && id != 0 ? context.scene->FindObjectByID(id) : nullptr;
}

SerializationContext Serialization(EditContext &context)
{
    return SerializationContext{context.scene, context.assets};
}

Component *ComponentAt(GameObject *object, int index)
{
    if (!object || index < 0 || static_cast<size_t>(index) >= object->GetComponents().size())
    {
        return nullptr;
    }
    return object->GetComponents()[static_cast<size_t>(index)].get();
}

} // namespace

NodeState NodeState::Capture(const GameObject &object)
{
    NodeState state;
    state.name = object.GetName();
    state.active = object.IsActive();
    state.position = object.GetPosition();
    state.rotation = object.GetRotation();
    state.scale = object.GetScale();
    return state;
}

void NodeState::Apply(GameObject &object) const
{
    object.SetName(name);
    object.SetActive(active);
    object.SetPosition(position);
    object.SetRotation(rotation);
    object.SetScale(scale);
}

bool NodeState::operator==(const NodeState &other) const
{
    return name == other.name && active == other.active && position == other.position && rotation == other.rotation &&
           scale == other.scale;
}

void NodeStateCommand::Do(EditContext &context)
{
    for (const auto &entry : m_entries)
    {
        if (GameObject *object = Find(context, entry.id))
        {
            entry.after.Apply(*object);
        }
    }
}

void NodeStateCommand::Undo(EditContext &context)
{
    for (const auto &entry : m_entries)
    {
        if (GameObject *object = Find(context, entry.id))
        {
            entry.before.Apply(*object);
        }
    }
}

bool PropertyCommand::Set(
    EditContext &context, uint64_t object, int component, const std::string &property, const nlohmann::json &value
)
{
    Component *target = ComponentAt(Find(context, object), component);
    const ComponentTypeDesc *type = target ? TypeRegistry::Get().Find(target->GetTypeName()) : nullptr;
    if (!type)
    {
        return false;
    }
    SerializationContext serialization = Serialization(context);
    for (const auto &desc : type->properties)
    {
        if (desc.name != property || !desc.set)
        {
            continue;
        }
        try
        {
            if (!desc.set(*target, value, serialization))
            {
                return false;
            }
        }
        catch (const std::exception &e)
        {
            GENI_LOG_WARN("property '%s': %s", property.c_str(), e.what());
            return false;
        }
        target->ResolveAssets(serialization);
        return true;
    }
    return false;
}

void PropertyCommand::Apply(EditContext &context, const nlohmann::json &value)
{
    Set(context, m_object, m_component, m_property, value);
}

void AddComponentCommand::Do(EditContext &context)
{
    GameObject *object = Find(context, m_object);
    std::unique_ptr<Component> component(TypeRegistry::Get().Create(m_type));
    if (!object || !component)
    {
        return;
    }
    SerializationContext serialization = Serialization(context);
    TypeRegistry::Get().Deserialize(m_type, *component, m_properties, serialization);
    if (m_index < 0)
    {
        m_index = static_cast<int>(object->GetComponents().size());
    }
    Component *added = component.release();
    object->AddComponent(added, m_index);
    added->ResolveAssets(serialization);
}

void AddComponentCommand::Undo(EditContext &context)
{
    GameObject *object = Find(context, m_object);
    if (Component *component = ComponentAt(object, m_index))
    {
        object->RemoveComponent(component);
    }
}

void RemoveComponentCommand::Do(EditContext &context)
{
    GameObject *object = Find(context, m_object);
    Component *component = ComponentAt(object, m_index);
    if (!component)
    {
        return;
    }
    m_type = component->GetTypeName();
    m_properties = TypeRegistry::Get().Serialize(m_type, *component);
    object->RemoveComponent(component);
}

void RemoveComponentCommand::Undo(EditContext &context)
{
    AddComponentCommand restore(m_object, m_type, m_properties, m_index);
    restore.Do(context);
}

void ReparentCommand::Do(EditContext &context)
{
    for (const auto &entry : m_entries)
    {
        if (GameObject *object = Find(context, entry.id))
        {
            context.scene->MoveObject(object, Find(context, entry.newParent), entry.newIndex);
        }
    }
}

void ReparentCommand::Undo(EditContext &context)
{
    for (auto it = m_entries.rbegin(); it != m_entries.rend(); ++it)
    {
        if (GameObject *object = Find(context, it->id))
        {
            context.scene->MoveObject(object, Find(context, it->oldParent), it->oldIndex);
        }
    }
}

SubtreeSnapshot SubtreeSnapshot::Capture(GameObject &root)
{
    SubtreeSnapshot snapshot;
    snapshot.root = root.GetID();
    snapshot.parent = root.GetParent() ? root.GetParent()->GetID() : 0;
    snapshot.index = root.GetSiblingIndex();
    snapshot.document = SceneSerializer::SaveSubtree({&root}, true);
    return snapshot;
}

bool SubtreeSnapshot::Restore(EditContext &context) const
{
    if (!context.scene)
    {
        return false;
    }
    GameObject *parentObject = Find(context, parent);
    if (parent != 0 && !parentObject)
    {
        GENI_LOG_WARN("undo: the parent of a restored object no longer exists; restoring it at the root");
    }
    SerializationContext serialization = Serialization(context);
    std::vector<GameObject *> roots;
    std::string error;
    if (!SceneSerializer::InstantiateSubtree(*context.scene, document, serialization, parentObject, &roots, &error, true))
    {
        GENI_LOG_ERROR("undo: could not restore an object: %s", error.c_str());
        return false;
    }
    for (GameObject *object : roots)
    {
        context.scene->MoveObject(object, parentObject, index);
    }
    return true;
}

CreateObjectsCommand::CreateObjectsCommand(std::string name, const std::vector<GameObject *> &roots)
    : m_name(std::move(name))
{
    for (GameObject *root : roots)
    {
        if (root)
        {
            m_snapshots.push_back(SubtreeSnapshot::Capture(*root));
        }
    }
}

void CreateObjectsCommand::Do(EditContext &context)
{
    for (const auto &snapshot : m_snapshots)
    {
        if (!Find(context, snapshot.root))
        {
            snapshot.Restore(context);
        }
    }
}

void CreateObjectsCommand::Undo(EditContext &context)
{
    for (auto it = m_snapshots.rbegin(); it != m_snapshots.rend(); ++it)
    {
        if (GameObject *object = Find(context, it->root))
        {
            // Redo recreates what is here now, not what was first created.
            *it = SubtreeSnapshot::Capture(*object);
            context.scene->DestroyObject(object);
        }
    }
}

DeleteObjectsCommand::DeleteObjectsCommand(EditContext &context, const std::vector<GameObject *> &roots)
{
    for (GameObject *root : roots)
    {
        if (root)
        {
            m_snapshots.push_back(SubtreeSnapshot::Capture(*root));
        }
    }
    // Restore lower sibling indices first, so every index is valid when it is used.
    std::stable_sort(m_snapshots.begin(), m_snapshots.end(), [](const SubtreeSnapshot &a, const SubtreeSnapshot &b) {
        return a.index < b.index;
    });
    if (context.scene && context.scene->GetMainCamera())
    {
        m_mainCamera = context.scene->GetMainCamera()->GetID();
    }
}

void DeleteObjectsCommand::Do(EditContext &context)
{
    for (auto it = m_snapshots.rbegin(); it != m_snapshots.rend(); ++it)
    {
        if (GameObject *object = Find(context, it->root))
        {
            *it = SubtreeSnapshot::Capture(*object);
            context.scene->DestroyObject(object);
        }
    }
}

void DeleteObjectsCommand::Undo(EditContext &context)
{
    for (const auto &snapshot : m_snapshots)
    {
        snapshot.Restore(context);
    }
    if (context.scene && !context.scene->GetMainCamera())
    {
        if (GameObject *camera = Find(context, m_mainCamera))
        {
            context.scene->SetMainCamera(camera);
        }
    }
}

void ReplaceObjectCommand::Swap(EditContext &context, const SubtreeSnapshot &from, const SubtreeSnapshot &to)
{
    const bool wasMainCamera =
        context.scene && context.scene->GetMainCamera() && context.scene->GetMainCamera()->GetID() == from.root;
    if (GameObject *object = Find(context, from.root))
    {
        context.scene->DestroyObject(object);
    }
    to.Restore(context);
    if (wasMainCamera)
    {
        context.scene->SetMainCamera(Find(context, to.root));
    }
}

void SetMainCameraCommand::Do(EditContext &context)
{
    if (context.scene)
    {
        context.scene->SetMainCamera(Find(context, m_after));
    }
}

void SetMainCameraCommand::Undo(EditContext &context)
{
    if (context.scene)
    {
        context.scene->SetMainCamera(Find(context, m_before));
    }
}

} // namespace Geni::Editor
