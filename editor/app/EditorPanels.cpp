// Hierarchy, inspector, assets and console panels of the editor (docs/design/0006).

#include "EditorApp.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <glm/gtc/quaternion.hpp>
#include <imgui.h>

namespace Geni::Editor
{

namespace
{

// Drag and drop payloads.
constexpr const char *kObjectPayload = "GENI_OBJECT";
constexpr const char *kAssetPayload = "GENI_ASSET";

bool ContainsIgnoreCase(const std::string &text, const char *needle)
{
    if (!needle || needle[0] == '\0')
    {
        return true;
    }
    const size_t length = std::strlen(needle);
    auto it = std::search(text.begin(), text.end(), needle, needle + length, [](char a, char b) {
        return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
    });
    return it != text.end();
}

struct HierarchyRow
{
    GameObject *object = nullptr;
    int depth = 0;
};

void Flatten(GameObject &object, int depth, const std::unordered_set<uint64_t> &expanded, std::vector<HierarchyRow> &rows)
{
    rows.push_back({&object, depth});
    if (object.GetChildren().empty() || expanded.find(object.GetID()) == expanded.end())
    {
        return;
    }
    for (const auto &child : object.GetChildren())
    {
        Flatten(*child, depth + 1, expanded, rows);
    }
}

void CollectMatches(GameObject &object, const char *filter, std::vector<HierarchyRow> &rows)
{
    if (ContainsIgnoreCase(object.GetName(), filter))
    {
        rows.push_back({&object, 0});
    }
    for (const auto &child : object.GetChildren())
    {
        CollectMatches(*child, filter, rows);
    }
}

const char *const kAssetTypeNames[] = {"All", "Mesh", "Texture", "Material", "Animation", "Scene", "Prefab"};
const AssetType kAssetTypes[] = {AssetType::Unknown, AssetType::Mesh,      AssetType::Texture, AssetType::Material,
                                 AssetType::Animation, AssetType::Scene, AssetType::Prefab};

} // namespace

// --- Hierarchy ---------------------------------------------------------------------------

void EditorApp::DrawHierarchy()
{
    if (!ImGui::Begin("Hierarchy"))
    {
        ImGui::End();
        return;
    }
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##filter", "Search by name", m_hierarchyFilter, sizeof(m_hierarchyFilter));

    // A selection made in the viewport is brought into view: open its ancestors, scroll.
    GameObject *primary = m_document->GetPrimary();
    const uint64_t primaryID = primary ? primary->GetID() : 0;
    if (primaryID != m_lastPrimary)
    {
        m_lastPrimary = primaryID;
        m_scrollToSelection = primary != nullptr;
        for (GameObject *node = primary ? primary->GetParent() : nullptr; node; node = node->GetParent())
        {
            m_expanded.insert(node->GetID());
        }
    }

    std::vector<HierarchyRow> rows;
    const bool filtering = m_hierarchyFilter[0] != '\0';
    for (const auto &root : m_scene->GetObjects())
    {
        if (filtering)
        {
            CollectMatches(*root, m_hierarchyFilter, rows);
        }
        else
        {
            Flatten(*root, 0, m_expanded, rows);
        }
    }

    ImGui::BeginChild("##rows");
    const float rowHeight = ImGui::GetTextLineHeightWithSpacing();
    if (m_scrollToSelection)
    {
        m_scrollToSelection = false;
        for (size_t i = 0; i < rows.size(); ++i)
        {
            if (rows[i].object == primary)
            {
                const float y = static_cast<float>(i) * rowHeight;
                if (y < ImGui::GetScrollY() || y > ImGui::GetScrollY() + ImGui::GetWindowHeight() - rowHeight)
                {
                    ImGui::SetScrollY(std::max(0.0f, y - ImGui::GetWindowHeight() * 0.5f));
                }
                break;
            }
        }
    }

    // Only the rows on screen are submitted: the city block has 10,000 of them.
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(rows.size()), rowHeight);
    while (clipper.Step())
    {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
        {
            DrawHierarchyRow(*rows[static_cast<size_t>(i)].object, rows[static_cast<size_t>(i)].depth);
        }
    }
    clipper.End();

    // The empty space below the rows: click to deselect, drop to move to the root.
    const ImVec2 space = ImGui::GetContentRegionAvail();
    ImGui::InvisibleButton("##background", ImVec2(std::max(space.x, 1.0f), std::max(space.y, 40.0f)));
    if (ImGui::IsItemClicked())
    {
        m_document->ClearSelection();
    }
    if (ImGui::BeginDragDropTarget())
    {
        if (ImGui::AcceptDragDropPayload(kObjectPayload))
        {
            m_document->Reparent(m_document->GetSelectedRoots(), nullptr);
        }
        if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(kAssetPayload))
        {
            ActivateAsset(AssetID{*static_cast<const uint64_t *>(payload->Data)});
        }
        ImGui::EndDragDropTarget();
    }
    if (ImGui::BeginPopupContextItem("##backgroundMenu"))
    {
        if (ImGui::MenuItem("Create Empty", nullptr, false, !m_document->IsPlaying()))
        {
            m_document->CreateEmpty();
        }
        ImGui::EndPopup();
    }
    ImGui::EndChild();
    ImGui::End();
}

void EditorApp::DrawHierarchyRow(GameObject &object, int depth)
{
    const uint64_t id = object.GetID();
    ImGui::PushID(static_cast<int>(id & 0x7fffffff));
    const float indent = static_cast<float>(depth) * 14.0f;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + indent);

    const bool hasChildren = !object.GetChildren().empty() && m_hierarchyFilter[0] == '\0';
    const bool expanded = m_expanded.find(id) != m_expanded.end();
    if (hasChildren)
    {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2.0f, 0.0f));
        if (ImGui::ArrowButton("##open", expanded ? ImGuiDir_Down : ImGuiDir_Right))
        {
            if (expanded)
            {
                m_expanded.erase(id);
            }
            else
            {
                m_expanded.insert(id);
            }
        }
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
        ImGui::SameLine(0.0f, 4.0f);
    }
    else
    {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetFrameHeight() - 2.0f);
    }

    std::string label = object.GetName().empty() ? std::string("(unnamed)") : object.GetName();
    if (object.IsPrefabInstance())
    {
        label += "  [prefab]";
    }
    if (m_scene->GetMainCamera() == &object)
    {
        label += "  (main camera)";
    }
    // Prefab contents and inactive objects are dimmed.
    const bool dim = object.IsFromPrefab() || !object.IsActive();
    if (dim)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    }
    const bool selected = m_document->IsSelected(id);
    if (ImGui::Selectable(label.c_str(), selected, ImGuiSelectableFlags_AllowDoubleClick))
    {
        const ImGuiIO &io = ImGui::GetIO();
        if (io.KeyCtrl || io.KeySuper)
        {
            m_document->ToggleSelected(id);
        }
        else
        {
            m_document->Select(id, io.KeyShift);
        }
        m_lastPrimary = m_document->GetPrimary() ? m_document->GetPrimary()->GetID() : 0;
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        {
            FocusSelection();
        }
    }
    if (dim)
    {
        ImGui::PopStyleColor();
    }

    if (!m_document->IsPlaying() && ImGui::BeginDragDropSource())
    {
        // Dragging a selected row moves the whole selection.
        if (!selected)
        {
            m_document->Select(id);
        }
        ImGui::SetDragDropPayload(kObjectPayload, &id, sizeof(id));
        ImGui::Text("%zu object(s)", m_document->GetSelection().size());
        ImGui::EndDragDropSource();
    }
    if (ImGui::BeginDragDropTarget())
    {
        if (ImGui::AcceptDragDropPayload(kObjectPayload))
        {
            if (m_document->Reparent(m_document->GetSelectedRoots(), &object))
            {
                m_expanded.insert(id);
            }
        }
        ImGui::EndDragDropTarget();
    }
    if (ImGui::BeginPopupContextItem("##menu"))
    {
        if (!selected)
        {
            m_document->Select(id);
        }
        DrawHierarchyContextMenu(object);
        ImGui::EndPopup();
    }
    ImGui::PopID();
}

void EditorApp::DrawHierarchyContextMenu(GameObject &object)
{
    const bool editable = m_document->CanEdit(&object);
    if (ImGui::MenuItem("Create Empty Child", nullptr, false, editable))
    {
        m_expanded.insert(object.GetID());
        m_document->CreateEmpty("GameObject", &object);
    }
    if (ImGui::MenuItem("Duplicate", nullptr, false, editable))
    {
        m_document->DuplicateSelection();
    }
    if (ImGui::MenuItem("Delete", nullptr, false, editable))
    {
        m_document->DeleteSelection();
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Move to Root", nullptr, false, editable && object.GetParent() != nullptr))
    {
        m_document->Reparent(m_document->GetSelectedRoots(), nullptr);
    }
    if (ImGui::MenuItem("Set as Main Camera", nullptr, false, editable && object.GetComponent<CameraComponent>() != nullptr))
    {
        m_document->SetMainCamera(&object);
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Create Prefab...", nullptr, false, editable && !object.IsPrefabInstance()))
    {
        m_prefabObject = object.GetID();
        std::snprintf(m_pathBuffer, sizeof(m_pathBuffer), "prefabs/%s.gprefab", object.GetName().c_str());
        m_openPrefab = true;
    }
    if (ImGui::MenuItem("Unpack Prefab", nullptr, false, editable && object.IsPrefabInstance()))
    {
        m_document->UnpackPrefab(object);
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Focus"))
    {
        FocusSelection();
    }
}

// --- Inspector -----------------------------------------------------------------------------

void EditorApp::BeginNodeEdit(const GameObject &object, const NodeState &before)
{
    if (!m_nodeEditActive)
    {
        m_nodeEditActive = true;
        m_nodeEditBefore = {object.GetID(), before};
    }
}

void EditorApp::EndNodeEdit()
{
    if (m_nodeEditActive)
    {
        m_nodeEditActive = false;
        m_document->CommitNodeStates("Edit object", {m_nodeEditBefore});
    }
}

void EditorApp::EndPropertyEdit()
{
    if (m_propertyEditActive)
    {
        m_propertyEditActive = false;
        m_document->CommitProperty(m_propertyObject, m_propertyComponent, m_propertyName, m_propertyBefore);
    }
}

void EditorApp::DrawInspector()
{
    if (!ImGui::Begin("Inspector"))
    {
        ImGui::End();
        return;
    }
    GameObject *object = m_document->GetPrimary();
    // An edit belongs to the object it started on.
    if (m_nodeEditActive && (!object || object->GetID() != m_nodeEditBefore.first))
    {
        EndNodeEdit();
    }
    if (m_propertyEditActive && (!object || object->GetID() != m_propertyObject))
    {
        EndPropertyEdit();
    }

    if (!object)
    {
        ImGui::TextDisabled("Nothing selected");
        ImGui::Separator();
        const std::string path =
            m_document->GetPath().empty() ? std::string("(not saved yet)") : m_project.ToAssetPath(m_document->GetPath());
        ImGui::Text("%s: %s", m_document->IsPrefabDocument() ? "Prefab" : "Scene", path.c_str());
        ImGui::Text("Root objects: %zu", m_scene->GetObjects().size());
        ImGui::Text("Meshes: %zu", m_scene->CountComponents<MeshComponent>() + m_scene->CountComponents<SkinnedMeshComponent>());
        ImGui::Text("Lights: %zu", m_scene->CountComponents<LightComponent>());
        GameObject *camera = m_scene->GetMainCamera();
        ImGui::Text("Main camera: %s", camera ? camera->GetName().c_str() : "(none)");
        ImGui::End();
        return;
    }

    const size_t count = m_document->GetSelection().size();
    if (count > 1)
    {
        ImGui::TextDisabled("%zu objects selected; showing the last one", count);
    }
    const bool editable = m_document->CanEdit(object);
    if (object->IsFromPrefab())
    {
        ImGui::TextWrapped("Part of a prefab instance. Open the prefab to change it, or unpack the instance.");
    }
    if (m_document->IsPlaying())
    {
        ImGui::TextDisabled("Playing: changes are not kept");
    }

    ImGui::BeginDisabled(!editable);
    const NodeState before = NodeState::Capture(*object);

    char name[256];
    std::snprintf(name, sizeof(name), "%s", object->GetName().c_str());
    ImGui::SetNextItemWidth(-70.0f);
    if (ImGui::InputText("##name", name, sizeof(name)))
    {
        BeginNodeEdit(*object, before);
        object->SetName(name);
    }
    if (ImGui::IsItemDeactivatedAfterEdit())
    {
        EndNodeEdit();
    }
    ImGui::SameLine();
    bool active = object->IsActive();
    if (ImGui::Checkbox("Active", &active))
    {
        NodeState state = before;
        state.active = active;
        m_document->SetNodeState(*object, state, active ? "Activate" : "Deactivate");
    }
    ImGui::EndDisabled();

    if (object->IsPrefabInstance())
    {
        ImGui::Text("Prefab: %s", AssetLabel(object->GetPrefab()).c_str());
        ImGui::BeginDisabled(!editable);
        if (ImGui::SmallButton("Unpack"))
        {
            m_document->UnpackPrefab(*object);
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        const AssetManifestEntry *entry = Engine::GetInstance().GetAssetManager().GetManifest().Find(object->GetPrefab());
        if (ImGui::SmallButton("Open Prefab") && entry)
        {
            RequestAction(PendingAction::OpenScene, entry->source);
            ImGui::End();
            return;
        }
    }

    if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
    {
        DrawTransform(*object, editable);
    }

    // A prefab instance's components belong to the prefab file.
    const bool componentsEditable = editable && !object->IsPrefabInstance();
    for (int i = 0; i < static_cast<int>(object->GetComponents().size()); ++i)
    {
        const size_t sizeBefore = object->GetComponents().size();
        DrawComponent(*object, i, componentsEditable);
        if (object->GetComponents().size() != sizeBefore)
        {
            break; // a component was removed; the list is drawn again next frame
        }
    }

    ImGui::Separator();
    ImGui::BeginDisabled(!componentsEditable);
    if (ImGui::Button("Add Component", ImVec2(-1.0f, 0.0f)))
    {
        ImGui::OpenPopup("##addComponent");
    }
    ImGui::EndDisabled();
    if (ImGui::BeginPopup("##addComponent"))
    {
        for (const std::string &type : TypeRegistry::Get().GetComponentTypes())
        {
            if (ImGui::MenuItem(type.c_str()))
            {
                m_document->AddComponent(*object, type);
            }
        }
        ImGui::EndPopup();
    }
    ImGui::End();
}

void EditorApp::DrawTransform(GameObject &object, bool editable)
{
    ImGui::BeginDisabled(!editable);
    const NodeState before = NodeState::Capture(object);

    glm::vec3 position = before.position;
    if (ImGui::DragFloat3("Position", &position.x, 0.05f))
    {
        BeginNodeEdit(object, before);
        object.SetPosition(position);
    }
    if (ImGui::IsItemDeactivatedAfterEdit())
    {
        EndNodeEdit();
    }

    // Euler angles are a view of the quaternion. They are recomputed only when the
    // rotation was changed by something else, so typing an angle does not make the
    // other two jump to an equivalent triple.
    const glm::quat rotation = before.rotation;
    if (m_eulerObject != object.GetID() || std::abs(glm::dot(rotation, m_eulerRotation)) < 0.999999f)
    {
        m_eulerObject = object.GetID();
        m_eulerRotation = rotation;
        m_eulerDegrees = glm::degrees(glm::eulerAngles(rotation));
        for (int i = 0; i < 3; ++i)
        {
            // No "-0.000" in the fields.
            if (std::abs(m_eulerDegrees[i]) < 1e-4f)
            {
                m_eulerDegrees[i] = 0.0f;
            }
        }
    }
    if (ImGui::DragFloat3("Rotation", &m_eulerDegrees.x, 0.5f))
    {
        BeginNodeEdit(object, before);
        m_eulerRotation = glm::normalize(glm::quat(glm::radians(m_eulerDegrees)));
        object.SetRotation(m_eulerRotation);
    }
    if (ImGui::IsItemDeactivatedAfterEdit())
    {
        EndNodeEdit();
    }

    glm::vec3 scale = before.scale;
    if (ImGui::DragFloat3("Scale", &scale.x, 0.02f))
    {
        BeginNodeEdit(object, before);
        object.SetScale(scale);
    }
    if (ImGui::IsItemDeactivatedAfterEdit())
    {
        EndNodeEdit();
    }
    ImGui::EndDisabled();
}

void EditorApp::DrawComponent(GameObject &object, int index, bool editable)
{
    Component &component = *object.GetComponents()[static_cast<size_t>(index)];
    const ComponentTypeDesc *type = TypeRegistry::Get().Find(component.GetTypeName());
    ImGui::PushID(index);

    bool keep = true;
    const bool open = ImGui::CollapsingHeader(component.GetTypeName(), editable ? &keep : nullptr, ImGuiTreeNodeFlags_DefaultOpen);
    if (!keep)
    {
        EndPropertyEdit();
        m_document->RemoveComponent(object, index);
        ImGui::PopID();
        return;
    }
    if (open)
    {
        if (!type)
        {
            ImGui::TextDisabled("This component type has no reflected properties");
        }
        else
        {
            ImGui::BeginDisabled(!editable);
            for (const PropertyDesc &property : type->properties)
            {
                if (property.get && property.set)
                {
                    DrawProperty(object, index, component, property);
                }
            }
            ImGui::EndDisabled();
        }
    }
    ImGui::PopID();
}

// One reflected property as a widget. A widget that is dragged or typed into previews
// the value on the live component and records one command when it is released; a
// checkbox or a combo records its command at once.
bool EditorApp::DrawProperty(GameObject &object, int index, Component &component, const PropertyDesc &property)
{
    const nlohmann::json before = property.get(component);
    nlohmann::json value = before;
    bool changed = false;
    bool immediate = false;
    const char *label = property.name.c_str();

    try
    {
        switch (property.type)
        {
        case PropertyType::Bool:
        {
            bool flag = value.get<bool>();
            if (ImGui::Checkbox(label, &flag))
            {
                value = flag;
                changed = immediate = true;
            }
            break;
        }
        case PropertyType::Int:
        {
            int number = value.get<int>();
            if (ImGui::DragInt(label, &number))
            {
                value = number;
                changed = true;
            }
            break;
        }
        case PropertyType::Float:
        {
            float number = value.get<float>();
            if (ImGui::DragFloat(label, &number, 0.05f))
            {
                value = number;
                changed = true;
            }
            break;
        }
        case PropertyType::Vec2:
        case PropertyType::Vec3:
        case PropertyType::Vec4:
        case PropertyType::Quat:
        {
            const int count = property.type == PropertyType::Vec2 ? 2 : property.type == PropertyType::Vec3 ? 3 : 4;
            float numbers[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            for (int i = 0; i < count; ++i)
            {
                numbers[i] = value.at(static_cast<size_t>(i)).get<float>();
            }
            bool edited = false;
            if (property.type == PropertyType::Vec3 && ContainsIgnoreCase(property.name, "color"))
            {
                edited = ImGui::ColorEdit3(label, numbers, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
            }
            else if (count == 2)
            {
                edited = ImGui::DragFloat2(label, numbers, 0.05f);
            }
            else if (count == 3)
            {
                edited = ImGui::DragFloat3(label, numbers, 0.05f);
            }
            else
            {
                edited = ImGui::DragFloat4(label, numbers, 0.05f);
            }
            if (edited)
            {
                value = nlohmann::json::array();
                for (int i = 0; i < count; ++i)
                {
                    value.push_back(numbers[i]);
                }
                changed = true;
            }
            break;
        }
        case PropertyType::String:
        {
            char text[256];
            std::snprintf(text, sizeof(text), "%s", value.get<std::string>().c_str());
            if (ImGui::InputText(label, text, sizeof(text)))
            {
                value = std::string(text);
                changed = true;
            }
            break;
        }
        case PropertyType::Enum:
        {
            const std::string current = value.get<std::string>();
            if (ImGui::BeginCombo(label, current.c_str()))
            {
                for (const std::string &name : property.enumNames)
                {
                    if (ImGui::Selectable(name.c_str(), name == current) && name != current)
                    {
                        value = name;
                        changed = immediate = true;
                    }
                }
                ImGui::EndCombo();
            }
            break;
        }
        case PropertyType::Asset:
        {
            const AssetID current = AssetID::FromHex(value.get<std::string>());
            if (ImGui::BeginCombo(label, AssetLabel(current).c_str()))
            {
                if (ImGui::Selectable("(none)", !current.IsValid()) && current.IsValid())
                {
                    value = std::string();
                    changed = immediate = true;
                }
                for (const auto &entry : Engine::GetInstance().GetAssetManager().GetManifest().GetEntries())
                {
                    if (entry.type != property.assetType)
                    {
                        continue;
                    }
                    ImGui::PushID(static_cast<int>(entry.id.value & 0x7fffffff));
                    if (ImGui::Selectable(entry.source.c_str(), entry.id == current) && entry.id != current)
                    {
                        value = entry.id.ToHex();
                        changed = immediate = true;
                    }
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }
            // An asset of the right kind can also be dropped on the field.
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(kAssetPayload))
                {
                    const AssetID dropped{*static_cast<const uint64_t *>(payload->Data)};
                    const AssetManifestEntry *entry = Engine::GetInstance().GetAssetManager().GetManifest().Find(dropped);
                    if (entry && entry->type == property.assetType && dropped != current)
                    {
                        value = dropped.ToHex();
                        changed = immediate = true;
                    }
                }
                ImGui::EndDragDropTarget();
            }
            break;
        }
        }
    }
    catch (const std::exception &)
    {
        ImGui::TextDisabled("%s: (unreadable value)", label);
        return false;
    }
    const bool released = ImGui::IsItemDeactivatedAfterEdit();

    if (changed && immediate)
    {
        EndPropertyEdit();
        m_document->SetProperty(object, index, property.name, value);
    }
    else if (changed)
    {
        if (!m_propertyEditActive)
        {
            m_propertyEditActive = true;
            m_propertyObject = object.GetID();
            m_propertyComponent = index;
            m_propertyName = property.name;
            m_propertyBefore = before;
        }
        PropertyCommand::Set(m_document->GetContext(), object.GetID(), index, property.name, value);
    }
    if (released && m_propertyEditActive && m_propertyName == property.name && m_propertyComponent == index)
    {
        EndPropertyEdit();
    }
    return changed;
}

// --- Assets ----------------------------------------------------------------------------------

// What double-clicking or dropping an asset does: a scene file opens, a model's scene is
// added to the current scene, a prefab is instantiated, a mesh becomes an object.
void EditorApp::ActivateAsset(AssetID id, const glm::vec3 *position)
{
    auto &assets = Engine::GetInstance().GetAssetManager();
    const AssetManifestEntry *found = assets.GetManifest().Find(id);
    if (!found)
    {
        return;
    }
    const AssetManifestEntry entry = *found; // loading may grow the manifest
    GameObject *created = nullptr;
    switch (entry.type)
    {
    case AssetType::Scene:
    {
        const bool sceneFile = entry.source.size() > 7 && entry.source.rfind(".gscene") == entry.source.size() - 7;
        if (sceneFile && !position)
        {
            RequestAction(PendingAction::OpenScene, entry.source);
            return;
        }
        const auto scene = assets.LoadScene(id, false);
        std::string error;
        if (!scene.IsReady() || !m_document->ImportScene(scene->document, &error))
        {
            m_document->SetStatus("Could not import " + entry.source);
            return;
        }
        created = m_document->GetPrimary();
        break;
    }
    case AssetType::Prefab:
        created = m_document->InstantiatePrefab(id);
        break;
    case AssetType::Mesh:
    {
        // A mesh needs a material: take the first one cooked from the same file.
        const std::string file = entry.source.substr(0, entry.source.find('#'));
        AssetID material;
        for (const auto &candidate : assets.GetManifest().GetEntries())
        {
            if (candidate.type == AssetType::Material && candidate.source.rfind(file, 0) == 0)
            {
                material = candidate.id;
                break;
            }
        }
        const size_t slash = file.find_last_of('/');
        created = m_document->CreateMeshObject(id, material, slash == std::string::npos ? file : file.substr(slash + 1));
        break;
    }
    default:
        m_document->SetStatus("Drop a material or texture on a component's field in the inspector");
        return;
    }
    if (created && position)
    {
        // Placed where it was dropped. This needs no command of its own: undoing the
        // creation records the object as it is then, so redo brings it back in place.
        created->SetPosition(*position);
    }
}

void EditorApp::DrawAssets()
{
    if (!ImGui::Begin("Assets"))
    {
        ImGui::End();
        return;
    }
    ImGui::SetNextItemWidth(120.0f);
    ImGui::Combo("##type", &m_assetTypeFilter, kAssetTypeNames, IM_ARRAYSIZE(kAssetTypeNames));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##assetFilter", "Search assets", m_assetFilter, sizeof(m_assetFilter));

    const auto &entries = Engine::GetInstance().GetAssetManager().GetManifest().GetEntries();
    std::vector<const AssetManifestEntry *> shown;
    for (const auto &entry : entries)
    {
        if (m_assetTypeFilter != 0 && entry.type != kAssetTypes[m_assetTypeFilter])
        {
            continue;
        }
        if (ContainsIgnoreCase(entry.source, m_assetFilter))
        {
            shown.push_back(&entry);
        }
    }
    std::sort(shown.begin(), shown.end(), [](const AssetManifestEntry *a, const AssetManifestEntry *b) {
        return a->type != b->type ? a->type < b->type : a->source < b->source;
    });

    ImGui::BeginChild("##assetList");
    if (shown.empty())
    {
        ImGui::TextDisabled(entries.empty() ? "No cooked assets. Put glTF or image files in the assets folder." : "No assets match");
    }
    AssetID activate;
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(shown.size()));
    while (clipper.Step())
    {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
        {
            const AssetManifestEntry &entry = *shown[static_cast<size_t>(i)];
            ImGui::PushID(static_cast<int>(entry.id.value & 0x7fffffff));
            char label[512];
            std::snprintf(label, sizeof(label), "%-9s  %s", AssetTypeName(entry.type), entry.source.c_str());
            if (ImGui::Selectable(label, false, ImGuiSelectableFlags_AllowDoubleClick) &&
                ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            {
                activate = entry.id;
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetItemTooltip("%s\n%s\nDouble-click or drag into the scene", entry.id.ToHex().c_str(), entry.path.c_str());
            }
            if (ImGui::BeginDragDropSource())
            {
                ImGui::SetDragDropPayload(kAssetPayload, &entry.id.value, sizeof(entry.id.value));
                ImGui::TextUnformatted(entry.source.c_str());
                ImGui::EndDragDropSource();
            }
            ImGui::PopID();
        }
    }
    clipper.End();
    ImGui::EndChild();
    ImGui::End();

    // After the list is drawn: activating an asset can change the manifest it iterates.
    if (activate.IsValid() && !m_document->IsPlaying())
    {
        ActivateAsset(activate);
    }
}

// --- Console -----------------------------------------------------------------------------------

void EditorApp::DrawConsole()
{
    if (!ImGui::Begin("Console"))
    {
        ImGui::End();
        return;
    }
    static std::vector<LogMessage> messages;
    static size_t seenCount = 0;
    const size_t count = Log::GetMessageCount();
    const bool changed = count != seenCount;
    if (changed)
    {
        messages = Log::GetRecent(256);
        seenCount = count;
    }
    ImGui::BeginChild("##log", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
    for (const auto &message : messages)
    {
        ImVec4 color(0.85f, 0.85f, 0.85f, 1.0f);
        if (message.level >= LogLevel::Error)
        {
            color = ImVec4(1.0f, 0.4f, 0.4f, 1.0f);
        }
        else if (message.level == LogLevel::Warn)
        {
            color = ImVec4(1.0f, 0.8f, 0.3f, 1.0f);
        }
        ImGui::PushStyleColor(ImGuiCol_Text, color);
        ImGui::TextUnformatted(Log::FormatLine(message).c_str());
        ImGui::PopStyleColor();
    }
    if (changed)
    {
        ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();
    ImGui::End();
}

} // namespace Geni::Editor
