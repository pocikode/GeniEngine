// Hierarchy, inspector, assets and console panels of the editor (docs/design/0006).

#include "EditorApp.h"
#include "EditorTheme.h"

#include <algorithm>
#include <cfloat>
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


// "SkinnedMeshComponent" -> "Skinned Mesh", "outerConeAngle" -> "Outer Cone Angle".
std::string PrettyName(const std::string &name)
{
    std::string base = name;
    const std::string suffix = "Component";
    if (base.size() > suffix.size() && base.compare(base.size() - suffix.size(), suffix.size(), suffix) == 0)
    {
        base.resize(base.size() - suffix.size());
    }
    std::string out;
    for (size_t i = 0; i < base.size(); ++i)
    {
        const char c = base[i];
        const bool upper = std::isupper(static_cast<unsigned char>(c)) != 0;
        const bool previousLower = i > 0 && std::islower(static_cast<unsigned char>(base[i - 1])) != 0;
        if (i > 0 && upper && previousLower)
        {
            out += ' ';
        }
        out += i == 0 ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : c;
    }
    return out;
}

Icon ComponentIcon(const std::string &type)
{
    if (type == "CameraComponent")
    {
        return Icon::Camera;
    }
    if (type == "LightComponent")
    {
        return Icon::Sun;
    }
    if (type == "SkinnedMeshComponent")
    {
        return Icon::Person;
    }
    if (type == "MeshComponent")
    {
        return Icon::Mesh;
    }
    if (type == "AnimationComponent")
    {
        return Icon::Animation;
    }
    if (type.find("Physics") != std::string::npos || type.find("Collider") != std::string::npos ||
        type.find("Body") != std::string::npos)
    {
        return Icon::Physics;
    }
    return Icon::Component;
}

Icon ObjectIcon(GameObject &object, bool cellRoot)
{
    if (cellRoot)
    {
        return Icon::Grid;
    }
    if (object.IsPrefabInstance())
    {
        return Icon::Prefab;
    }
    if (object.GetComponent<CameraComponent>())
    {
        return Icon::Camera;
    }
    if (auto *light = object.GetComponent<LightComponent>())
    {
        return light->GetType() == LightType::Directional ? Icon::Sun
               : light->GetType() == LightType::Spot      ? Icon::Spot
                                                          : Icon::Bulb;
    }
    if (object.GetComponent<SkinnedMeshComponent>())
    {
        return Icon::Person;
    }
    if (object.GetComponent<MeshComponent>())
    {
        return Icon::Mesh;
    }
    return Icon::Empty;
}

// A section header in the inspector: a framed collapsing header with an icon drawn in
// front of the label.
bool SectionHeader(const char *label, Icon icon, bool *keep = nullptr)
{
    char text[160];
    std::snprintf(text, sizeof(text), "      %s", label);
    ImGui::PushStyleColor(ImGuiCol_Header, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImGui::GetStyleColorVec4(ImGuiCol_FrameBgHovered));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImGui::GetStyleColorVec4(ImGuiCol_FrameBgActive));
    const bool open = ImGui::CollapsingHeader(text, keep, ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
    ImGui::PopStyleColor(3);
    const ImVec2 low = ImGui::GetItemRectMin();
    const ImVec2 high = ImGui::GetItemRectMax();
    const float size = high.y - low.y;
    DrawIcon(*ImGui::GetWindowDrawList(), icon, ImVec2(low.x + ImGui::GetTreeNodeToLabelSpacing() + size * 0.45f, (low.y + high.y) * 0.5f), size * 0.62f, IconColor(icon));
    return open;
}

bool BeginPropertyTable(const char *id)
{
    if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_PadOuterX))
    {
        return false;
    }
    ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthStretch, 0.36f);
    ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch, 0.64f);
    return true;
}

} // namespace

// --- Hierarchy ---------------------------------------------------------------------------

void EditorApp::DrawHierarchy()
{
    if (!m_showHierarchy)
    {
        return;
    }
    if (m_focusHierarchy > 0)
    {
        // A world's panel docks with the hierarchy and would come to the front.
        --m_focusHierarchy;
        ImGui::SetNextWindowFocus();
    }
    if (!ImGui::Begin("Hierarchy", &m_showHierarchy))
    {
        ImGui::End();
        return;
    }
    const bool playing = m_document->IsPlaying();
    const bool ready = m_stage == Stage::Ready;

    // Toolbar: create, then search.
    ImGui::BeginDisabled(playing || !ready);
    if (IconButton("##create", Icon::Plus, false, "Create"))
    {
        ImGui::OpenPopup("##createMenu");
    }
    ImGui::EndDisabled();
    if (ImGui::BeginPopup("##createMenu"))
    {
        GameObject *parent = m_document->CanEdit(m_document->GetPrimary()) ? m_document->GetPrimary() : nullptr;
        if (ImGui::MenuItem("Empty"))
        {
            m_document->CreateEmpty();
        }
        if (ImGui::MenuItem("Empty Child", nullptr, false, parent != nullptr))
        {
            m_expanded.insert(parent->GetID());
            m_document->CreateEmpty("GameObject", parent);
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Camera"))
        {
            m_document->CreateCamera();
        }
        if (ImGui::MenuItem("Directional Light"))
        {
            m_document->CreateLight(LightType::Directional);
        }
        if (ImGui::MenuItem("Point Light"))
        {
            m_document->CreateLight(LightType::Point);
        }
        if (ImGui::MenuItem("Spot Light"))
        {
            m_document->CreateLight(LightType::Spot);
        }
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##filter", "Search", m_hierarchyFilter, sizeof(m_hierarchyFilter));

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

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::ColorConvertU32ToFloat4(Theme::PanelDark));
    ImGui::BeginChild("##rows", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None);
    ImGui::PopStyleColor();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
    const float rowHeight = ImGui::GetFrameHeight() - 2.0f;
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

    if (rows.empty())
    {
        ImGui::Dummy(ImVec2(0.0f, 8.0f));
        ImGui::TextDisabled(filtering ? "  No objects match" : "  Empty scene. Use + or drag a model here.");
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
    ImGui::PopStyleVar();

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
        if (ImGui::MenuItem("Create Empty", nullptr, false, !playing))
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
    ImDrawList &draw = *ImGui::GetWindowDrawList();
    const float rowHeight = ImGui::GetFrameHeight() - 2.0f;
    const ImVec2 rowMin = ImGui::GetCursorScreenPos();
    const float indent = 6.0f + static_cast<float>(depth) * 16.0f;
    const bool selected = m_document->IsSelected(id);
    const bool cellRoot = m_document->IsCellRoot(id);

    // The row itself: select, drag, drop, context menu.
    const bool clicked = ImGui::Selectable(
        "##row", selected, ImGuiSelectableFlags_AllowDoubleClick | ImGuiSelectableFlags_AllowOverlap,
        ImVec2(0.0f, rowHeight)
    );
    const bool rowHovered = ImGui::IsItemHovered();
    if (clicked)
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
    if (!m_document->IsPlaying() && ImGui::BeginDragDropSource())
    {
        // Dragging a selected row moves the whole selection.
        if (!selected)
        {
            m_document->Select(id);
        }
        ImGui::SetDragDropPayload(kObjectPayload, &id, sizeof(id));
        ImGui::Text("%s", m_document->GetSelection().size() > 1 ? (std::to_string(m_document->GetSelection().size()) + " objects").c_str() : object.GetName().c_str());
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
    const ImVec2 after = ImGui::GetCursorScreenPos();
    const float rowWidth = ImGui::GetContentRegionAvail().x + (after.x - rowMin.x);
    const float middle = rowMin.y + rowHeight * 0.5f;

    // Expand arrow.
    const bool hasChildren = !object.GetChildren().empty() && m_hierarchyFilter[0] == '\0';
    const bool expanded = m_expanded.find(id) != m_expanded.end();
    if (hasChildren)
    {
        ImGui::SetCursorScreenPos(ImVec2(rowMin.x + indent, rowMin.y));
        if (ImGui::InvisibleButton("##open", ImVec2(16.0f, rowHeight)))
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
        const ImU32 arrow = ImGui::IsItemHovered() ? Theme::Text : Theme::TextDim;
        const ImVec2 c(rowMin.x + indent + 7.0f, middle);
        if (expanded)
        {
            draw.AddTriangleFilled(ImVec2(c.x - 4.0f, c.y - 2.0f), ImVec2(c.x + 4.0f, c.y - 2.0f), ImVec2(c.x, c.y + 3.0f), arrow);
        }
        else
        {
            draw.AddTriangleFilled(ImVec2(c.x - 2.0f, c.y - 4.0f), ImVec2(c.x + 3.0f, c.y), ImVec2(c.x - 2.0f, c.y + 4.0f), arrow);
        }
    }

    // Icon and name. Prefab instances are blue, their contents and inactive objects dim.
    const Icon icon = ObjectIcon(object, cellRoot);
    const bool dim = object.IsFromPrefab() || !object.IsActive();
    ImU32 iconColor = IconColor(icon);
    ImU32 textColor = object.IsPrefabInstance() ? Theme::Prefab : Theme::Text;
    if (icon == Icon::Empty || icon == Icon::Mesh)
    {
        iconColor = icon == Icon::Mesh ? IconColor(Icon::Mesh) : Theme::TextDim;
    }
    if (dim)
    {
        iconColor = (iconColor & 0x00ffffffu) | 0x80000000u;
        textColor = (textColor & 0x00ffffffu) | 0x88000000u;
    }
    const float iconX = rowMin.x + indent + 18.0f;
    DrawIcon(draw, icon, ImVec2(iconX + 7.0f, middle), rowHeight * 0.62f, iconColor);
    const float textX = iconX + 20.0f;

    if (m_renameID == id)
    {
        ImGui::SetCursorScreenPos(ImVec2(textX - 4.0f, rowMin.y));
        ImGui::SetNextItemWidth(std::max(80.0f, rowMin.x + rowWidth - textX - 8.0f));
        if (m_renameFocus)
        {
            ImGui::SetKeyboardFocusHere();
            m_renameFocus = false;
        }
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 1.0f));
        const bool done = ImGui::InputText("##rename", m_renameBuffer, sizeof(m_renameBuffer), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
        ImGui::PopStyleVar();
        if (done || ImGui::IsItemDeactivated())
        {
            if (!ImGui::IsKeyPressed(ImGuiKey_Escape) && object.GetName() != m_renameBuffer && m_document->CanEdit(&object))
            {
                NodeState state = NodeState::Capture(object);
                state.name = m_renameBuffer;
                m_document->SetNodeState(object, state, "Rename");
            }
            m_renameID = 0;
        }
    }
    else
    {
        const std::string &name = object.GetName();
        draw.AddText(ImVec2(textX, middle - ImGui::GetTextLineHeight() * 0.5f), textColor, name.empty() ? "(unnamed)" : name.c_str());
        if (m_scene->GetMainCamera() == &object)
        {
            const float nameWidth = ImGui::CalcTextSize(name.c_str()).x;
            draw.AddText(ImVec2(textX + nameWidth + 8.0f, middle - ImGui::GetTextLineHeight() * 0.5f), Theme::TextDim, "Main Camera");
        }
    }

    // Visibility: shown on hover, and always when the object is inactive.
    if ((rowHovered || !object.IsActive()) && !cellRoot && m_renameID != id)
    {
        const float eyeX = rowMin.x + rowWidth - rowHeight - 4.0f;
        ImGui::SetCursorScreenPos(ImVec2(eyeX, rowMin.y));
        if (ImGui::InvisibleButton("##eye", ImVec2(rowHeight, rowHeight)) && m_document->CanEdit(&object) && !m_document->IsPlaying())
        {
            NodeState state = NodeState::Capture(object);
            state.active = !state.active;
            m_document->SetNodeState(object, state, state.active ? "Activate" : "Deactivate");
        }
        ImGui::SetItemTooltip(object.IsActive() ? "Deactivate" : "Activate");
        const ImU32 eye = ImGui::IsItemHovered() ? Theme::Text : Theme::TextDim;
        DrawIcon(draw, object.IsActive() ? Icon::Eye : Icon::EyeOff, ImVec2(eyeX + rowHeight * 0.5f, middle), rowHeight * 0.6f, eye);
    }
    ImGui::SetCursorScreenPos(after);
    ImGui::PopID();
}

void EditorApp::DrawHierarchyContextMenu(GameObject &object)
{
    const bool editable = m_document->CanEdit(&object);
    if (ImGui::MenuItem("Rename", "F2", false, editable))
    {
        m_renameID = object.GetID();
        m_renameFocus = true;
        std::snprintf(m_renameBuffer, sizeof(m_renameBuffer), "%s", object.GetName().c_str());
    }
    if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, editable))
    {
        m_document->DuplicateSelection();
    }
    if (ImGui::MenuItem("Delete", "Del", false, editable))
    {
        m_document->DeleteSelection();
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Create Empty Child", nullptr, false, editable || m_document->IsCellRoot(object.GetID())))
    {
        m_expanded.insert(object.GetID());
        m_document->CreateEmpty("GameObject", &object);
    }
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
    if (ImGui::MenuItem("Focus", "F"))
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

void EditorApp::DrawPropertyLabel(const char *label)
{
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-FLT_MIN);
}

// Three fields with coloured X / Y / Z tags, as in Unity and Godot. Clicking a tag resets
// that component. Returns true when the value changed; `released` is set when an edit ended.
bool EditorApp::DrawVec3Row(const char *label, glm::vec3 &value, float speed, float resetValue, bool &released)
{
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::TableNextColumn();
    ImGui::PushID(label);
    bool changed = false;
    const float total = ImGui::GetContentRegionAvail().x;
    const float tag = 16.0f;
    const float field = std::max(20.0f, (total - 3.0f * tag - 8.0f) / 3.0f);
    static const ImU32 colors[3] = {Theme::AxisX, Theme::AxisY, Theme::AxisZ};
    static const char *names[3] = {"X", "Y", "Z"};
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
    for (int i = 0; i < 3; ++i)
    {
        ImGui::PushID(i);
        if (i > 0)
        {
            ImGui::SameLine(0.0f, 4.0f);
        }
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4((colors[i] & 0x00ffffffu) | 0xc0000000u));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::ColorConvertU32ToFloat4(colors[i]));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::ColorConvertU32ToFloat4(colors[i]));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, ImGui::GetStyle().FramePadding.y));
        if (ImGui::Button(names[i], ImVec2(tag, 0.0f)) && value[i] != resetValue)
        {
            value[i] = resetValue;
            changed = true;
            released = true;
        }
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(3);
        ImGui::SetItemTooltip("Reset %s", names[i]);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(field);
        if (ImGui::DragFloat("##v", &value[i], speed, 0.0f, 0.0f, "%.4g"))
        {
            changed = true;
        }
        released = released || ImGui::IsItemDeactivatedAfterEdit();
        ImGui::PopID();
    }
    ImGui::PopStyleVar();
    ImGui::PopID();
    return changed;
}

void EditorApp::DrawInspector()
{
    if (!m_showInspector)
    {
        return;
    }
    if (!ImGui::Begin("Inspector", &m_showInspector))
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
        const bool world = m_document->IsWorldDocument();
        const Icon icon = world ? Icon::World : m_document->IsPrefabDocument() ? Icon::Prefab : Icon::Scene;
        const std::string path =
            m_document->GetPath().empty() ? std::string("untitled") : m_project.ToAssetPath(m_document->GetPath());
        ImGui::Dummy(ImVec2(0.0f, 2.0f));
        IconText(icon);
        ImGui::TextUnformatted(std::filesystem::path(path).filename().string().c_str());
        ImGui::TextDisabled("%s", path.c_str());
        ImGui::Spacing();
        if (BeginPropertyTable("##sceneInfo"))
        {
            auto row = [](const char *label, const std::string &value) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", label);
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(value.c_str());
            };
            row("Type", world ? "World" : m_document->IsPrefabDocument() ? "Prefab" : "Scene");
            if (world)
            {
                const auto &cells = m_document->GetRegionCells();
                row("Region", cells.empty() ? std::string("none loaded")
                                            : std::to_string(cells.front().x) + "," + std::to_string(cells.front().z) + " to " +
                                                  std::to_string(cells.back().x) + "," + std::to_string(cells.back().z));
            }
            row("Root objects", std::to_string(m_scene->GetObjects().size()));
            row("Meshes", std::to_string(m_scene->CountComponents<MeshComponent>() + m_scene->CountComponents<SkinnedMeshComponent>()));
            row("Lights", std::to_string(m_scene->CountComponents<LightComponent>()));
            GameObject *camera = m_scene->GetMainCamera();
            row("Main camera", camera ? camera->GetName() : "none");
            ImGui::EndTable();
        }
        ImGui::Spacing();
        ImGui::TextDisabled("Select an object to edit it.");
        ImGui::End();
        return;
    }

    const size_t count = m_document->GetSelection().size();
    const bool editable = m_document->CanEdit(object);
    const NodeState before = NodeState::Capture(*object);

    // Header: icon, active toggle, name.
    ImGui::BeginDisabled(!editable);
    {
        const Icon icon = ObjectIcon(*object, m_document->IsCellRoot(object->GetID()));
        const float side = ImGui::GetFrameHeight();
        const ImVec2 at = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(side, side));
        DrawIcon(*ImGui::GetWindowDrawList(), icon, ImVec2(at.x + side * 0.5f, at.y + side * 0.5f), side * 0.8f, IconColor(icon));
        ImGui::SameLine();
        bool active = object->IsActive();
        if (ImGui::Checkbox("##active", &active))
        {
            NodeState state = before;
            state.active = active;
            m_document->SetNodeState(*object, state, active ? "Activate" : "Deactivate");
        }
        ImGui::SetItemTooltip("Active");
        ImGui::SameLine();
        char name[256];
        std::snprintf(name, sizeof(name), "%s", object->GetName().c_str());
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::InputText("##name", name, sizeof(name)))
        {
            BeginNodeEdit(*object, before);
            object->SetName(name);
        }
        if (ImGui::IsItemDeactivatedAfterEdit())
        {
            EndNodeEdit();
        }
    }
    ImGui::EndDisabled();
    if (count > 1)
    {
        ImGui::TextDisabled("%zu objects selected; editing the last one", count);
    }
    if (m_document->IsPlaying())
    {
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(Theme::Warning), "Playing: changes are not kept");
    }

    // Prefab bar.
    if (object->IsPrefabInstance() || object->IsFromPrefab())
    {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::ColorConvertU32ToFloat4((Theme::Prefab & 0x00ffffffu) | 0x22000000u));
        ImGui::BeginChild("##prefab", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders);
        IconText(Icon::Prefab);
        if (object->IsPrefabInstance())
        {
            ImGui::TextUnformatted(AssetLabel(object->GetPrefab()).c_str());
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
                const std::string source = entry->source;
                ImGui::EndChild();
                ImGui::PopStyleColor();
                ImGui::End();
                RequestAction(PendingAction::OpenScene, source);
                return;
            }
        }
        else
        {
            ImGui::TextWrapped("Part of a prefab instance. Open the prefab to change it, or unpack the instance.");
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }
    ImGui::Spacing();

    if (SectionHeader("Transform", Icon::Move))
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

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::BeginDisabled(!componentsEditable);
    const float buttonWidth = std::min(240.0f, ImGui::GetContentRegionAvail().x);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (ImGui::GetContentRegionAvail().x - buttonWidth) * 0.5f);
    if (ImGui::Button("Add Component", ImVec2(buttonWidth, 0.0f)))
    {
        m_componentFilter[0] = '\0';
        ImGui::OpenPopup("##addComponent");
    }
    ImGui::EndDisabled();
    ImGui::SetNextWindowSize(ImVec2(buttonWidth, 0.0f));
    if (ImGui::BeginPopup("##addComponent"))
    {
        if (ImGui::IsWindowAppearing())
        {
            ImGui::SetKeyboardFocusHere();
        }
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##componentFilter", "Search", m_componentFilter, sizeof(m_componentFilter));
        ImGui::Separator();
        for (const std::string &type : TypeRegistry::Get().GetComponentTypes())
        {
            const std::string pretty = PrettyName(type);
            if (!ContainsIgnoreCase(pretty, m_componentFilter) && !ContainsIgnoreCase(type, m_componentFilter))
            {
                continue;
            }
            IconText(ComponentIcon(type));
            if (ImGui::Selectable(pretty.c_str()))
            {
                m_document->AddComponent(*object, type);
                ImGui::CloseCurrentPopup();
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
    if (!BeginPropertyTable("##transform"))
    {
        ImGui::EndDisabled();
        return;
    }

    bool released = false;
    glm::vec3 position = before.position;
    if (DrawVec3Row("Position", position, 0.05f, 0.0f, released))
    {
        BeginNodeEdit(object, before);
        object.SetPosition(position);
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
    if (DrawVec3Row("Rotation", m_eulerDegrees, 0.5f, 0.0f, released))
    {
        BeginNodeEdit(object, before);
        m_eulerRotation = glm::normalize(glm::quat(glm::radians(m_eulerDegrees)));
        object.SetRotation(m_eulerRotation);
    }

    glm::vec3 scale = before.scale;
    if (DrawVec3Row("Scale", scale, 0.02f, 1.0f, released))
    {
        BeginNodeEdit(object, before);
        object.SetScale(scale);
    }
    ImGui::EndTable();
    if (released)
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

    const std::string pretty = PrettyName(component.GetTypeName());
    const bool open = SectionHeader(pretty.c_str(), ComponentIcon(component.GetTypeName()));
    // The component menu at the right end of the header.
    {
        const ImVec2 high = ImGui::GetItemRectMax();
        const ImVec2 low = ImGui::GetItemRectMin();
        const float side = high.y - low.y;
        const ImVec2 back = ImGui::GetCursorScreenPos();
        ImGui::SetCursorScreenPos(ImVec2(high.x - side - 2.0f, low.y));
        if (IconButton("##componentMenu", Icon::Dots, false, nullptr, side))
        {
            ImGui::OpenPopup("##componentActions");
        }
        ImGui::SetCursorScreenPos(back);
    }
    bool remove = false;
    if (ImGui::BeginPopup("##componentActions"))
    {
        if (ImGui::MenuItem("Remove Component", nullptr, false, editable))
        {
            remove = true;
        }
        ImGui::EndPopup();
    }
    if (remove)
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
        else if (BeginPropertyTable("##properties"))
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
            ImGui::EndTable();
        }
    }
    ImGui::PopID();
}

// One reflected property as a table row. A widget that is dragged or typed into previews
// the value on the live component and records one command when it is released; a
// checkbox or a combo records its command at once.
bool EditorApp::DrawProperty(GameObject &object, int index, Component &component, const PropertyDesc &property)
{
    const nlohmann::json before = property.get(component);
    nlohmann::json value = before;
    bool changed = false;
    bool immediate = false;
    const std::string pretty = PrettyName(property.name);
    const std::string id = "##" + property.name;
    const char *label = id.c_str();
    DrawPropertyLabel(pretty.c_str());

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
            if (ImGui::BeginCombo(label, PrettyName(current).c_str()))
            {
                for (const std::string &name : property.enumNames)
                {
                    if (ImGui::Selectable(PrettyName(name).c_str(), name == current) && name != current)
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
            std::string shown = AssetLabel(current);
            const size_t slash = shown.find_last_of('/');
            if (slash != std::string::npos)
            {
                shown = shown.substr(slash + 1);
            }
            if (ImGui::BeginCombo(label, shown.c_str(), ImGuiComboFlags_HeightLarge))
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
            ImGui::SetItemTooltip("%s\nDrop an asset here", AssetLabel(current).c_str());
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
        ImGui::TextDisabled("(unreadable value)");
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

// --- Console -----------------------------------------------------------------------------------

void EditorApp::DrawConsole()
{
    if (!m_showConsole)
    {
        return;
    }
    if (!ImGui::Begin("Console", &m_showConsole))
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
        messages = Log::GetRecent(512);
        seenCount = count;
    }
    // Only what arrived since the last Clear.
    const size_t fresh = std::min(messages.size(), count - std::min(count, m_consoleClearedAt));
    const size_t first = messages.size() - fresh;

    int infos = 0, warnings = 0, errors = 0;
    for (size_t i = first; i < messages.size(); ++i)
    {
        const LogLevel level = messages[i].level;
        (level >= LogLevel::Error ? errors : level == LogLevel::Warn ? warnings : infos)++;
    }

    if (ImGui::Button("Clear"))
    {
        m_consoleClearedAt = count;
    }
    ImGui::SameLine();
    const float toggles = 3.0f * 64.0f;
    ImGui::SetNextItemWidth(std::max(80.0f, ImGui::GetContentRegionAvail().x - toggles - 8.0f));
    ImGui::InputTextWithHint("##consoleFilter", "Search", m_consoleFilter, sizeof(m_consoleFilter));
    auto toggle = [](const char *id, Icon icon, int number, bool &on) {
        ImGui::SameLine(0.0f, 4.0f);
        char text[32];
        std::snprintf(text, sizeof(text), "      %d", number);
        ImGui::PushID(id);
        if (!on)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        }
        if (ImGui::Button(text, ImVec2(60.0f, 0.0f)))
        {
            on = !on;
        }
        if (!on)
        {
            ImGui::PopStyleColor();
        }
        const ImVec2 low = ImGui::GetItemRectMin();
        const float side = ImGui::GetItemRectSize().y;
        DrawIcon(*ImGui::GetWindowDrawList(), icon, ImVec2(low.x + 14.0f, low.y + side * 0.5f), side * 0.58f, on ? IconColor(icon) : Theme::TextDim);
        ImGui::PopID();
    };
    toggle("info", Icon::Info, infos, m_consoleInfo);
    toggle("warn", Icon::Warning, warnings, m_consoleWarnings);
    toggle("error", Icon::Error, errors, m_consoleErrors);

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::ColorConvertU32ToFloat4(Theme::PanelDark));
    ImGui::BeginChild("##log", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
    ImGui::PopStyleColor();
    const bool atBottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.0f;
    for (size_t i = first; i < messages.size(); ++i)
    {
        const LogMessage &message = messages[i];
        const bool error = message.level >= LogLevel::Error;
        const bool warning = message.level == LogLevel::Warn;
        if ((error && !m_consoleErrors) || (warning && !m_consoleWarnings) || (!error && !warning && !m_consoleInfo))
        {
            continue;
        }
        if (!ContainsIgnoreCase(message.text, m_consoleFilter))
        {
            continue;
        }
        const Icon icon = error ? Icon::Error : warning ? Icon::Warning : Icon::Info;
        ImGui::Dummy(ImVec2(2.0f, 0.0f));
        ImGui::SameLine();
        IconText(icon);
        ImGui::TextDisabled("%7.2f", message.time);
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(error ? Theme::Error : warning ? Theme::Warning : Theme::Text));
        ImGui::TextUnformatted(message.text.c_str());
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered() && message.file && message.file[0])
        {
            ImGui::SetTooltip("%s:%d", message.file, message.line);
        }
    }
    if (changed && atBottom)
    {
        ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();
    ImGui::End();
}

// --- World ---------------------------------------------------------------------------------------

// The open world's cell grid (docs/design/0007). Click a cell to start a region, Shift+click
// to extend it, then load it. Cells with a file are filled, loaded cells are outlined.
void EditorApp::DrawWorldPanel()
{
    if (!m_document->IsWorldDocument() || !m_showWorld)
    {
        return;
    }
    if (!ImGui::Begin("World", &m_showWorld))
    {
        ImGui::End();
        return;
    }
    const WorldSource &world = m_document->GetWorld();
    const int x0 = std::min(m_regionPick[0], m_regionPick[2]);
    const int x1 = std::max(m_regionPick[0], m_regionPick[2]);
    const int z0 = std::min(m_regionPick[1], m_regionPick[3]);
    const int z1 = std::max(m_regionPick[1], m_regionPick[3]);
    IconText(Icon::World);
    ImGui::Text("%s", m_document->GetPath().filename().string().c_str());
    ImGui::TextDisabled("%d x %d cells of %.0f m", world.cellsX, world.cellsZ, world.cellSize);
    ImGui::Text("Selected: %d,%d to %d,%d (%d cells)", x0, z0, x1, z1, (x1 - x0 + 1) * (z1 - z0 + 1));

    const bool playing = m_document->IsPlaying();
    ImGui::BeginDisabled(playing);
    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(Theme::AccentDim));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::ColorConvertU32ToFloat4(Theme::Accent));
    if (ImGui::Button(m_document->IsDirty() ? "Save and Load Region" : "Load Region", ImVec2(-1.0f, 0.0f)))
    {
        std::string error;
        if (!m_document->IsDirty() || m_document->Save(&error))
        {
            LoadRegion(x0, z0, x1, z1);
        }
        else
        {
            m_document->SetStatus("Could not save: " + error);
        }
    }
    ImGui::PopStyleColor(2);
    ImGui::EndDisabled();
    ImGui::TextDisabled("Click a cell, Shift+click to extend, double-click to load.");

    const float available = ImGui::GetContentRegionAvail().x;
    const float cell = std::clamp(std::floor(available / static_cast<float>(std::max(world.cellsX, 1))), 8.0f, 28.0f);
    ImGui::BeginChild("##cells", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
    ImDrawList *draw = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##grid", ImVec2(cell * world.cellsX, cell * world.cellsZ));
    const bool hovered = ImGui::IsItemHovered();
    // +Z (south) is drawn downwards, matching the editor camera's default view.
    for (int z = 0; z < world.cellsZ; ++z)
    {
        for (int x = 0; x < world.cellsX; ++x)
        {
            const ImVec2 lo(origin.x + x * cell, origin.y + z * cell);
            const ImVec2 hi(lo.x + cell - 2.0f, lo.y + cell - 2.0f);
            const bool picked = x >= x0 && x <= x1 && z >= z0 && z <= z1;
            const bool file = m_document->CellFileExists(x, z);
            const ImU32 fill = picked ? (file ? IM_COL32(220, 170, 70, 255) : IM_COL32(130, 105, 60, 255))
                               : file ? IM_COL32(88, 96, 110, 255)
                                      : IM_COL32(44, 47, 54, 255);
            draw->AddRectFilled(lo, hi, fill, 2.0f);
        }
    }
    for (const auto &loaded : m_document->GetRegionCells())
    {
        const ImVec2 lo(origin.x + loaded.x * cell, origin.y + loaded.z * cell);
        draw->AddRect(ImVec2(lo.x - 1.0f, lo.y - 1.0f), ImVec2(lo.x + cell - 1.0f, lo.y + cell - 1.0f), Theme::Accent, 2.0f, 0, 2.0f);
    }
    if (hovered)
    {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        const int x = static_cast<int>((mouse.x - origin.x) / cell);
        const int z = static_cast<int>((mouse.y - origin.y) / cell);
        if (x >= 0 && z >= 0 && x < world.cellsX && z < world.cellsZ)
        {
            ImGui::SetTooltip("Cell %d,%d%s", x, z, m_document->CellFileExists(x, z) ? "" : " (empty)");
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                if (!ImGui::GetIO().KeyShift)
                {
                    m_regionPick[0] = x;
                    m_regionPick[1] = z;
                }
                m_regionPick[2] = x;
                m_regionPick[3] = z;
            }
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !playing)
            {
                LoadRegion(x0, z0, x1, z1);
            }
        }
    }
    ImGui::Dummy(ImVec2(0.0f, 6.0f));
    auto legend = [&](ImU32 color, const char *text, bool outline) {
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const float s = ImGui::GetTextLineHeight() - 4.0f;
        if (outline)
        {
            draw->AddRect(ImVec2(at.x, at.y + 2.0f), ImVec2(at.x + s, at.y + 2.0f + s), color, 2.0f, 0, 2.0f);
        }
        else
        {
            draw->AddRectFilled(ImVec2(at.x, at.y + 2.0f), ImVec2(at.x + s, at.y + 2.0f + s), color, 2.0f);
        }
        ImGui::Dummy(ImVec2(s + 4.0f, s));
        ImGui::SameLine();
        ImGui::TextDisabled("%s", text);
        ImGui::SameLine(0.0f, 12.0f);
    };
    legend(IM_COL32(88, 96, 110, 255), "has content", false);
    legend(IM_COL32(220, 170, 70, 255), "selected", false);
    legend(Theme::Accent, "loaded", true);
    ImGui::NewLine();
    ImGui::EndChild();
    ImGui::End();
}

} // namespace Geni::Editor
