// The assets browser (docs/design/0006), laid out like Unity's Project window and Godot's
// FileSystem dock: a folder tree on the left and the selected folder's files as tiles on
// the right. Files come from the source assets folder on disk; a model is one tile whose
// meshes, materials and animations open below it.

#include "EditorApp.h"
#include "EditorTheme.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <imgui.h>
#include <map>

namespace Geni::Editor
{

namespace
{

constexpr const char *kAssetPayload = "GENI_ASSET";

std::string Lower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

std::string Extension(const std::string &path)
{
    return Lower(std::filesystem::path(path).extension().string());
}

Icon FileIcon(const std::string &path)
{
    const std::string extension = Extension(path);
    if (extension == ".gltf" || extension == ".glb")
    {
        return Icon::Model;
    }
    if (extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".ktx2")
    {
        return Icon::Texture;
    }
    if (extension == ".mat")
    {
        return Icon::Material;
    }
    if (extension == ".gscene")
    {
        return Icon::Scene;
    }
    if (extension == ".gprefab")
    {
        return Icon::Prefab;
    }
    if (extension == ".gworld")
    {
        return Icon::World;
    }
    if (extension == ".slang")
    {
        return Icon::Shader;
    }
    return Icon::File;
}

Icon TypeIcon(AssetType type)
{
    switch (type)
    {
    case AssetType::Mesh:
        return Icon::Mesh;
    case AssetType::Texture:
        return Icon::Texture;
    case AssetType::Material:
        return Icon::Material;
    case AssetType::Animation:
        return Icon::Animation;
    case AssetType::Scene:
        return Icon::Scene;
    case AssetType::Prefab:
        return Icon::Prefab;
    default:
        return Icon::File;
    }
}

const char *KindName(const std::string &path)
{
    switch (FileIcon(path))
    {
    case Icon::Model:
        return "Model";
    case Icon::Texture:
        return "Texture";
    case Icon::Material:
        return "Material";
    case Icon::Scene:
        return "Scene";
    case Icon::Prefab:
        return "Prefab";
    case Icon::World:
        return "World";
    case Icon::Shader:
        return "Shader";
    default:
        return "File";
    }
}

bool ContainsNoCase(const std::string &text, const char *needle)
{
    if (!needle || needle[0] == '\0')
    {
        return true;
    }
    return Lower(text).find(Lower(needle)) != std::string::npos;
}

// The middle of a long name, so tiles keep their width: "Wall_Plaster_W...Round".
std::string FitText(const std::string &text, float width)
{
    if (ImGui::CalcTextSize(text.c_str()).x <= width)
    {
        return text;
    }
    std::string fitted = text;
    while (fitted.size() > 4 && ImGui::CalcTextSize((fitted + "...").c_str()).x > width)
    {
        fitted.pop_back();
    }
    return fitted + "...";
}

} // namespace

// --- Placing assets ----------------------------------------------------------------------

// What double-clicking or dropping an asset does: a scene file opens, a model's scene is
// added to the current scene, a prefab is instantiated, a mesh becomes an object.
void EditorApp::ActivateAsset(AssetID id, const glm::vec3 *position)
{
    auto &assets = Engine::GetInstance().GetAssetManager();
    const AssetManifestEntry *found = assets.GetManifest().Find(id);
    if (!found || m_document->IsPlaying())
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
        m_document->SetStatus("Drag a material or texture onto a component's field in the Inspector");
        return;
    }
    if (!created)
    {
        return;
    }
    if (position)
    {
        // Placed where it was dropped. This needs no command of its own: undoing the
        // creation records the object as it is then, so redo brings it back in place.
        created->SetPosition(*position);
    }
    else
    {
        // In front of the camera, on the ground when the camera looks down at it.
        const glm::vec3 pivot = m_camera.GetPivot();
        if (!created->GetParent())
        {
            created->SetPosition(glm::vec3(pivot.x, 0.0f, pivot.z));
        }
    }
}

void EditorApp::OpenAssetFile(const AssetFile &file)
{
    const Icon icon = FileIcon(file.path);
    if (std::filesystem::path(file.path).extension() == ".mat")
    {
        OpenMaterial(file.path);
    }
    else if (std::filesystem::path(file.path).extension() == ".animgraph")
    {
        OpenAnimGraph(file.path);
    }
    else if (icon == Icon::Scene || icon == Icon::Prefab)
    {
        RequestAction(PendingAction::OpenScene, file.path);
    }
    else if (icon == Icon::World)
    {
        RequestAction(PendingAction::OpenWorld, file.path);
    }
    else if (file.primary.IsValid())
    {
        ActivateAsset(file.primary);
    }
    else
    {
        m_document->SetStatus(file.name + " has no imported asset to place");
    }
}

// --- Scanning ----------------------------------------------------------------------------

void EditorApp::ScanAssets()
{
    auto &manifest = Engine::GetInstance().GetAssetManager().GetManifest();
    m_assetManifestSize = manifest.GetEntries().size();
    m_assetScanTime = m_time;

    // Manifest entries by source file: the file's own asset, and its parts ("#mesh0").
    std::map<std::string, AssetFile> byPath;
    for (const auto &entry : manifest.GetEntries())
    {
        const size_t hash = entry.source.find('#');
        const std::string file = entry.source.substr(0, hash);
        AssetFile &item = byPath[file];
        if (hash == std::string::npos)
        {
            item.primary = entry.id;
        }
        else if (entry.type != AssetType::Mesh || entry.source.find("#hlod") == std::string::npos)
        {
            item.parts.push_back(entry.id);
        }
    }
    for (auto &[path, item] : byPath)
    {
        std::sort(item.parts.begin(), item.parts.end(), [&](AssetID a, AssetID b) {
            const auto *ea = manifest.Find(a);
            const auto *eb = manifest.Find(b);
            return ea->type != eb->type ? ea->type < eb->type : ea->source < eb->source;
        });
    }

    auto root = std::make_unique<AssetFolder>();
    root->name = "Assets";
    std::map<std::string, AssetFolder *> folders{{"", root.get()}};
    auto folderFor = [&](const std::string &path) {
        // Creates the chain of folders down to `path`.
        std::string current;
        AssetFolder *node = root.get();
        size_t start = 0;
        while (start < path.size())
        {
            const size_t slash = path.find('/', start);
            const std::string name = path.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
            current = current.empty() ? name : current + "/" + name;
            auto found = folders.find(current);
            if (found == folders.end())
            {
                auto folder = std::make_unique<AssetFolder>();
                folder->name = name;
                folder->path = current;
                AssetFolder *raw = folder.get();
                node->folders.push_back(std::move(folder));
                found = folders.emplace(current, raw).first;
            }
            node = found->second;
            if (slash == std::string::npos)
            {
                break;
            }
            start = slash + 1;
        }
        return node;
    };

    std::error_code ec;
    const std::filesystem::path base = m_options.project.assets;
    for (auto it = std::filesystem::recursive_directory_iterator(base, ec); !ec && it != std::filesystem::recursive_directory_iterator();
         it.increment(ec))
    {
        std::error_code entryError;
        const std::string name = it->path().filename().string();
        if (!name.empty() && name[0] == '.')
        {
            if (it->is_directory(entryError))
            {
                it.disable_recursion_pending();
            }
            continue;
        }
        const std::string relative = std::filesystem::relative(it->path(), base, entryError).generic_string();
        if (it->is_directory(entryError))
        {
            if (name == "cooked")
            {
                it.disable_recursion_pending();
                continue;
            }
            folderFor(relative);
            continue;
        }
        const std::string extension = Extension(name);
        // Sidecars and buffers are part of the file next to them.
        if (extension == ".meta" || extension == ".bin" || extension == ".tmp" || extension == ".spv" || name == ".DS_Store")
        {
            continue;
        }
        const size_t slash = relative.find_last_of('/');
        AssetFolder *folder = folderFor(slash == std::string::npos ? std::string() : relative.substr(0, slash));
        AssetFile file;
        auto known = byPath.find(relative);
        if (known != byPath.end())
        {
            file = known->second;
        }
        file.path = relative;
        file.name = name;
        folder->files.push_back(std::move(file));
    }

    // Folders first and by name, then files by name, as file browsers do.
    std::vector<AssetFolder *> stack{root.get()};
    while (!stack.empty())
    {
        AssetFolder *folder = stack.back();
        stack.pop_back();
        std::sort(folder->folders.begin(), folder->folders.end(), [](const auto &a, const auto &b) { return Lower(a->name) < Lower(b->name); });
        std::sort(folder->files.begin(), folder->files.end(), [](const AssetFile &a, const AssetFile &b) { return Lower(a.name) < Lower(b.name); });
        for (const auto &child : folder->folders)
        {
            stack.push_back(child.get());
        }
    }
    m_assetRoot = std::move(root);
    if (!FindAssetFolder(m_assetFolder))
    {
        m_assetFolder.clear();
    }
}

EditorApp::AssetFolder *EditorApp::FindAssetFolder(const std::string &path)
{
    if (!m_assetRoot)
    {
        return nullptr;
    }
    AssetFolder *node = m_assetRoot.get();
    if (path.empty())
    {
        return node;
    }
    size_t start = 0;
    while (node && start <= path.size())
    {
        const size_t slash = path.find('/', start);
        const std::string name = path.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
        AssetFolder *next = nullptr;
        for (const auto &child : node->folders)
        {
            if (child->name == name)
            {
                next = child.get();
                break;
            }
        }
        node = next;
        if (slash == std::string::npos)
        {
            break;
        }
        start = slash + 1;
    }
    return node;
}

// --- Drawing -----------------------------------------------------------------------------

void EditorApp::DrawAssetFolderTree(AssetFolder &folder)
{
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth |
                               ImGuiTreeNodeFlags_FramePadding;
    if (folder.folders.empty())
    {
        flags |= ImGuiTreeNodeFlags_Leaf;
    }
    if (folder.path == m_assetFolder)
    {
        flags |= ImGuiTreeNodeFlags_Selected;
    }
    // Open the folders on the way to the one shown.
    if (!folder.path.empty() && m_assetFolder.rfind(folder.path + "/", 0) == 0)
    {
        ImGui::SetNextItemOpen(true, ImGuiCond_Always);
    }
    if (folder.path.empty())
    {
        ImGui::SetNextItemOpen(true, ImGuiCond_Once);
    }
    const std::string label = "      " + folder.name + "##" + folder.path;
    const bool open = ImGui::TreeNodeEx(label.c_str(), flags);
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
    {
        m_assetFolder = folder.path;
    }
    const ImVec2 low = ImGui::GetItemRectMin();
    const float height = ImGui::GetItemRectSize().y;
    DrawIcon(*ImGui::GetWindowDrawList(), open && !folder.folders.empty() ? Icon::FolderOpen : Icon::Folder,
             ImVec2(low.x + ImGui::GetTreeNodeToLabelSpacing() + 8.0f, low.y + height * 0.5f), height * 0.62f, IconColor(Icon::Folder));
    if (open)
    {
        for (const auto &child : folder.folders)
        {
            DrawAssetFolderTree(*child);
        }
        ImGui::TreePop();
    }
}

void EditorApp::DrawAssetTile(const AssetFile &file, float tile)
{
    ImGui::PushID(file.path.c_str());
    ImDrawList &draw = *ImGui::GetWindowDrawList();
    const bool list = tile < 40.0f;
    const float textHeight = ImGui::GetTextLineHeight();
    const ImVec2 size = list ? ImVec2(ImGui::GetContentRegionAvail().x, textHeight + 6.0f) : ImVec2(tile, tile + textHeight + 8.0f);
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const bool selected = m_assetSelected == file.path;
    const Icon icon = FileIcon(file.path);
    const bool imported = file.primary.IsValid() || !file.parts.empty();

    // One group: the expand button sits inside the tile and the tile lays out as one item.
    ImGui::BeginGroup();
    if (ImGui::Selectable("##tile", selected, ImGuiSelectableFlags_AllowDoubleClick | ImGuiSelectableFlags_AllowOverlap, size))
    {
        m_assetSelected = file.path;
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        {
            OpenAssetFile(file);
        }
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
    {
        ImGui::SetTooltip(
            "%s\n%s%s\n%s", file.path.c_str(), KindName(file.path),
            file.parts.empty() ? "" : (", " + std::to_string(file.parts.size()) + " sub-assets").c_str(),
            !imported                      ? "Not imported"
            : icon == Icon::Scene || icon == Icon::Prefab || icon == Icon::World ? "Double-click to open"
                                                                                  : "Double-click or drag into the scene"
        );
    }
    if (file.primary.IsValid() && ImGui::BeginDragDropSource())
    {
        ImGui::SetDragDropPayload(kAssetPayload, &file.primary.value, sizeof(file.primary.value));
        IconText(icon);
        ImGui::TextUnformatted(file.name.c_str());
        ImGui::EndDragDropSource();
    }
    if (ImGui::BeginPopupContextItem("##fileMenu"))
    {
        m_assetSelected = file.path;
        if (ImGui::MenuItem(icon == Icon::Scene || icon == Icon::Prefab || icon == Icon::World ? "Open" : "Add to Scene", nullptr, false, imported))
        {
            OpenAssetFile(file);
        }
        if (icon == Icon::Prefab && ImGui::MenuItem("Add to Scene", nullptr, false, file.primary.IsValid()))
        {
            ActivateAsset(file.primary, nullptr);
        }
        if (!file.parts.empty())
        {
            const bool expanded = m_assetExpanded.count(file.path) != 0;
            if (ImGui::MenuItem(expanded ? "Hide Sub-assets" : "Show Sub-assets"))
            {
                if (expanded)
                {
                    m_assetExpanded.erase(file.path);
                }
                else
                {
                    m_assetExpanded.insert(file.path);
                }
            }
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Copy Path"))
        {
            ImGui::SetClipboardText(file.path.c_str());
        }
        ImGui::EndPopup();
    }
    const ImU32 color = imported || icon == Icon::Shader || icon == Icon::File || icon == Icon::Material ? IconColor(icon) : (IconColor(icon) & 0x00ffffffu) | 0x70000000u;
    if (list)
    {
        DrawIcon(draw, icon, ImVec2(at.x + 10.0f, at.y + size.y * 0.5f), textHeight * 0.9f, color);
        draw.AddText(ImVec2(at.x + 24.0f, at.y + 3.0f), Theme::Text, file.name.c_str());
        const char *kind = KindName(file.path);
        draw.AddText(ImVec2(at.x + size.x - ImGui::CalcTextSize(kind).x - 8.0f, at.y + 3.0f), Theme::TextDim, kind);
    }
    else
    {
        draw.AddRectFilled(ImVec2(at.x + 4.0f, at.y + 4.0f), ImVec2(at.x + tile - 4.0f, at.y + tile - 4.0f), IM_COL32(255, 255, 255, 10), 4.0f);
        DrawIcon(draw, icon, ImVec2(at.x + tile * 0.5f, at.y + tile * 0.5f), tile * 0.48f, color);
        const std::string stem = std::filesystem::path(file.name).stem().string();
        const std::string shown = FitText(stem, tile - 4.0f);
        const float width = ImGui::CalcTextSize(shown.c_str()).x;
        draw.AddText(ImVec2(at.x + (tile - width) * 0.5f, at.y + tile + 2.0f), selected ? IM_COL32(255, 255, 255, 255) : Theme::Text, shown.c_str());
    }

    // A model's sub-assets open from the arrow on its tile.
    if (!file.parts.empty())
    {
        const bool expanded = m_assetExpanded.count(file.path) != 0;
        const float side = list ? size.y : 18.0f;
        const ImVec2 button = list ? ImVec2(at.x + size.x - 90.0f, at.y) : ImVec2(at.x + tile - side - 2.0f, at.y + tile * 0.5f - side * 0.5f);
        ImGui::SetCursorScreenPos(button);
        if (ImGui::InvisibleButton("##expand", ImVec2(side, side)))
        {
            if (expanded)
            {
                m_assetExpanded.erase(file.path);
            }
            else
            {
                m_assetExpanded.insert(file.path);
            }
        }
        ImGui::SetItemTooltip(expanded ? "Hide sub-assets" : "Show %zu sub-assets", file.parts.size());
        const bool hovered = ImGui::IsItemHovered();
        const ImVec2 c(button.x + side * 0.5f, button.y + side * 0.5f);
        draw.AddCircleFilled(c, side * 0.45f, hovered ? IM_COL32(90, 98, 112, 255) : IM_COL32(60, 66, 78, 230));
        if (expanded)
        {
            draw.AddTriangleFilled(ImVec2(c.x + 3.0f, c.y - 4.0f), ImVec2(c.x - 2.0f, c.y), ImVec2(c.x + 3.0f, c.y + 4.0f), Theme::Text);
        }
        else
        {
            draw.AddTriangleFilled(ImVec2(c.x - 2.0f, c.y - 4.0f), ImVec2(c.x + 3.0f, c.y), ImVec2(c.x - 2.0f, c.y + 4.0f), Theme::Text);
        }
    }
    ImGui::EndGroup();
    ImGui::PopID();
}

void EditorApp::DrawAssetPartTile(AssetID id, float tile)
{
    const AssetManifestEntry *entry = Engine::GetInstance().GetAssetManager().GetManifest().Find(id);
    if (!entry)
    {
        return;
    }
    ImGui::PushID(static_cast<int>(id.value & 0x7fffffff));
    ImDrawList &draw = *ImGui::GetWindowDrawList();
    const bool list = tile < 40.0f;
    const float textHeight = ImGui::GetTextLineHeight();
    const float side = list ? tile : tile * 0.8f;
    const ImVec2 size = list ? ImVec2(ImGui::GetContentRegionAvail().x, textHeight + 6.0f) : ImVec2(side, side + textHeight + 8.0f);
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const Icon icon = TypeIcon(entry->type);
    const size_t hash = entry->source.find('#');
    const std::string name = hash == std::string::npos ? entry->source : entry->source.substr(hash + 1);

    draw.AddRectFilled(at, ImVec2(at.x + size.x, at.y + size.y), IM_COL32(72, 140, 230, 22), 4.0f);
    if (ImGui::Selectable("##part", false, ImGuiSelectableFlags_AllowDoubleClick, size) && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
    {
        ActivateAsset(id);
    }
    ImGui::SetItemTooltip("%s\n%s", entry->source.c_str(), AssetTypeName(entry->type));
    if (ImGui::BeginDragDropSource())
    {
        ImGui::SetDragDropPayload(kAssetPayload, &id.value, sizeof(id.value));
        IconText(icon);
        ImGui::TextUnformatted(name.c_str());
        ImGui::EndDragDropSource();
    }
    if (list)
    {
        DrawIcon(draw, icon, ImVec2(at.x + 22.0f, at.y + size.y * 0.5f), textHeight * 0.8f, IconColor(icon));
        draw.AddText(ImVec2(at.x + 36.0f, at.y + 3.0f), Theme::TextDim, name.c_str());
    }
    else
    {
        DrawIcon(draw, icon, ImVec2(at.x + side * 0.5f, at.y + side * 0.5f), side * 0.45f, IconColor(icon));
        const std::string shown = FitText(name, side - 4.0f);
        const float width = ImGui::CalcTextSize(shown.c_str()).x;
        draw.AddText(ImVec2(at.x + (side - width) * 0.5f, at.y + side + 2.0f), Theme::TextDim, shown.c_str());
    }
    ImGui::PopID();
}

void EditorApp::DrawAssets()
{
    if (!m_showAssets)
    {
        return;
    }
    if (m_selectAssetsTab > 0)
    {
        // Over a few frames: on the first one the dock node has not placed its tabs yet.
        --m_selectAssetsTab;
        ImGui::SetNextWindowFocus();
    }
    if (!ImGui::Begin("Assets", &m_showAssets))
    {
        ImGui::End();
        return;
    }
    if (m_stage != Stage::Ready)
    {
        ImGui::TextDisabled("Importing...");
        ImGui::End();
        return;
    }
    const size_t manifestSize = Engine::GetInstance().GetAssetManager().GetManifest().GetEntries().size();
    if (!m_assetRoot || m_assetScanTime < 0.0 || manifestSize != m_assetManifestSize)
    {
        ScanAssets();
    }

    // Toolbar: breadcrumbs, then search, size and refresh on the right.
    {
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2.0f, 4.0f));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        if (ImGui::Button("Assets"))
        {
            m_assetFolder.clear();
        }
        std::string walked;
        size_t start = 0;
        while (!m_assetFolder.empty() && start <= m_assetFolder.size())
        {
            const size_t slash = m_assetFolder.find('/', start);
            const std::string name = m_assetFolder.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
            walked = walked.empty() ? name : walked + "/" + name;
            ImGui::SameLine();
            ImGui::TextDisabled(">");
            ImGui::SameLine();
            ImGui::PushID(walked.c_str());
            if (ImGui::Button(name.c_str()))
            {
                m_assetFolder = walked;
            }
            ImGui::PopID();
            if (slash == std::string::npos)
            {
                break;
            }
            start = slash + 1;
        }
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();

        const float right = 220.0f + 90.0f + ImGui::GetFrameHeight() + 16.0f;
        ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 10.0f, ImGui::GetWindowContentRegionMax().x - right));
        ImGui::SetNextItemWidth(220.0f);
        ImGui::InputTextWithHint("##assetFilter", "Search all assets", m_assetFilter, sizeof(m_assetFilter));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(90.0f);
        ImGui::SliderFloat("##tile", &m_assetTile, 24.0f, 140.0f, "");
        ImGui::SetItemTooltip("Tile size (smallest: list)");
        ImGui::SameLine();
        if (IconButton("##refresh", Icon::Refresh, false, "Rescan the assets folder"))
        {
            m_assetScanTime = -100.0;
        }
    }

    if (ImGui::BeginTable("##assetsLayout", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV))
    {
        ImGui::TableSetupColumn("folders", ImGuiTableColumnFlags_WidthFixed, 200.0f);
        ImGui::TableSetupColumn("files", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableNextRow();

        ImGui::TableNextColumn();
        ImGui::BeginChild("##folderTree", ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 2.0f));
        if (m_assetRoot)
        {
            DrawAssetFolderTree(*m_assetRoot);
        }
        ImGui::PopStyleVar();
        ImGui::EndChild();

        ImGui::TableNextColumn();
        ImGui::BeginChild("##files", ImVec2(0.0f, 0.0f));
        const bool searching = m_assetFilter[0] != '\0';
        std::vector<const AssetFolder *> folders;
        std::vector<const AssetFile *> files;
        if (searching && m_assetRoot)
        {
            std::vector<const AssetFolder *> stack{m_assetRoot.get()};
            while (!stack.empty())
            {
                const AssetFolder *folder = stack.back();
                stack.pop_back();
                for (const auto &file : folder->files)
                {
                    if (ContainsNoCase(file.name, m_assetFilter))
                    {
                        files.push_back(&file);
                    }
                }
                for (const auto &child : folder->folders)
                {
                    stack.push_back(child.get());
                }
            }
        }
        else if (const AssetFolder *folder = FindAssetFolder(m_assetFolder))
        {
            for (const auto &child : folder->folders)
            {
                folders.push_back(child.get());
            }
            for (const auto &file : folder->files)
            {
                files.push_back(&file);
            }
        }

        const bool list = m_assetTile < 40.0f;
        const float spacing = 8.0f;
        const float tile = m_assetTile;
        const float available = ImGui::GetContentRegionAvail().x;
        const int columns = list ? 1 : std::max(1, static_cast<int>((available + spacing) / (tile + spacing)));
        int column = 0;
        auto place = [&](float width) {
            if (!list && column > 0 && column < columns)
            {
                ImGui::SameLine(0.0f, spacing);
            }
            (void)width;
        };
        auto advance = [&] {
            if (!list && ++column >= columns)
            {
                column = 0;
            }
        };

        std::string openFolder;
        for (const AssetFolder *folder : folders)
        {
            place(tile);
            ImGui::PushID(folder->path.c_str());
            const ImVec2 at = ImGui::GetCursorScreenPos();
            const float textHeight = ImGui::GetTextLineHeight();
            const ImVec2 size = list ? ImVec2(ImGui::GetContentRegionAvail().x, textHeight + 6.0f) : ImVec2(tile, tile + textHeight + 8.0f);
            if (ImGui::Selectable("##folder", false, ImGuiSelectableFlags_AllowDoubleClick, size) && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            {
                openFolder = folder->path;
            }
            ImDrawList &draw = *ImGui::GetWindowDrawList();
            if (list)
            {
                DrawIcon(draw, Icon::Folder, ImVec2(at.x + 10.0f, at.y + size.y * 0.5f), textHeight * 0.9f, IconColor(Icon::Folder));
                draw.AddText(ImVec2(at.x + 24.0f, at.y + 3.0f), Theme::Text, folder->name.c_str());
            }
            else
            {
                DrawIcon(draw, Icon::Folder, ImVec2(at.x + tile * 0.5f, at.y + tile * 0.5f), tile * 0.6f, IconColor(Icon::Folder));
                const std::string shown = FitText(folder->name, tile - 4.0f);
                const float width = ImGui::CalcTextSize(shown.c_str()).x;
                draw.AddText(ImVec2(at.x + (tile - width) * 0.5f, at.y + tile + 2.0f), Theme::Text, shown.c_str());
            }
            ImGui::PopID();
            advance();
        }
        for (const AssetFile *file : files)
        {
            place(tile);
            DrawAssetTile(*file, tile);
            advance();
            if (m_assetExpanded.count(file->path) != 0)
            {
                for (AssetID part : file->parts)
                {
                    place(tile * 0.8f);
                    DrawAssetPartTile(part, tile);
                    advance();
                }
            }
        }
        if (folders.empty() && files.empty())
        {
            ImGui::Dummy(ImVec2(0.0f, 10.0f));
            ImGui::TextDisabled(searching ? "  No assets match" : "  Empty folder. Put glTF, images, scenes or prefabs here.");
        }
        if (!openFolder.empty())
        {
            m_assetFolder = openFolder;
        }
        ImGui::EndChild();
        ImGui::EndTable();
    }
    ImGui::End();
}

} // namespace Geni::Editor
