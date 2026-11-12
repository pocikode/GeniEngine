// Projects: the project manager shown when the editor starts without one, the recent
// projects list, the first cook running in the background, and what opens once the
// project is ready (what was open last time, else the project's world or first scene).

#include "EditorApp.h"
#include "EditorTheme.h"

#include <GLFW/glfw3.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <imgui.h>
#include <imgui_internal.h>

namespace Geni::Editor
{

namespace
{

std::filesystem::path UserFolder()
{
#if defined(_WIN32)
    const char *home = std::getenv("USERPROFILE");
#else
    const char *home = std::getenv("HOME");
#endif
    if (!home || home[0] == '\0')
    {
        return {};
    }
    return std::filesystem::path(home) / ".geni";
}

double EpochSeconds()
{
    return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string AgoText(double seconds)
{
    const double ago = EpochSeconds() - seconds;
    if (seconds <= 0.0)
    {
        return "never opened";
    }
    if (ago < 120.0)
    {
        return "just now";
    }
    if (ago < 7200.0)
    {
        return std::to_string(static_cast<int>(ago / 60.0)) + " minutes ago";
    }
    if (ago < 172800.0)
    {
        return std::to_string(static_cast<int>(ago / 3600.0)) + " hours ago";
    }
    return std::to_string(static_cast<int>(ago / 86400.0)) + " days ago";
}

// The project folder for a path the user gave: the folder itself when it has assets/,
// its parent when it is the assets folder.
bool ResolveProjectFolder(const std::filesystem::path &input, std::filesystem::path &root)
{
    std::error_code ec;
    const std::filesystem::path folder = std::filesystem::absolute(input, ec).lexically_normal();
    if (std::filesystem::is_directory(folder / "assets", ec))
    {
        root = folder;
        return true;
    }
    if (folder.filename() == "assets" && std::filesystem::is_directory(folder, ec))
    {
        root = folder.parent_path();
        return true;
    }
    return false;
}

} // namespace

// --- Recent projects -------------------------------------------------------------------

void EditorApp::LoadRecentProjects()
{
    m_recentProjects.clear();
    const std::filesystem::path file = UserFolder() / "editor_projects.json";
    std::ifstream stream(file);
    if (!stream)
    {
        return;
    }
    try
    {
        nlohmann::json document;
        stream >> document;
        for (const auto &entry : document.value("projects", nlohmann::json::array()))
        {
            RecentProject project;
            project.path = entry.value("path", std::string());
            project.lastOpened = entry.value("lastOpened", 0.0);
            if (!project.path.empty())
            {
                m_recentProjects.push_back(project);
            }
        }
    }
    catch (const std::exception &e)
    {
        GENI_LOG_WARN("editor: cannot read %s: %s", file.string().c_str(), e.what());
    }
}

void EditorApp::SaveRecentProjects() const
{
    const std::filesystem::path folder = UserFolder();
    if (folder.empty())
    {
        return;
    }
    std::error_code ec;
    std::filesystem::create_directories(folder, ec);
    nlohmann::json projects = nlohmann::json::array();
    for (const auto &project : m_recentProjects)
    {
        projects.push_back({{"path", project.path}, {"lastOpened", project.lastOpened}});
    }
    std::ofstream stream(folder / "editor_projects.json");
    stream << nlohmann::json{{"projects", projects}}.dump(2) << "\n";
}

// --- Per-project editor state ----------------------------------------------------------

nlohmann::json EditorApp::LoadProjectState() const
{
    std::ifstream stream(m_options.project.cooked.parent_path() / "editor_state.json");
    if (!stream)
    {
        return nlohmann::json::object();
    }
    try
    {
        nlohmann::json document;
        stream >> document;
        return document.is_object() ? document : nlohmann::json::object();
    }
    catch (const std::exception &)
    {
        return nlohmann::json::object();
    }
}

void EditorApp::SaveProjectState() const
{
    // A scripted session must not change what the next interactive one opens.
    if (m_stage != Stage::Ready || !m_options.script.empty() || m_options.project.cooked.empty())
    {
        return;
    }
    nlohmann::json state = nlohmann::json::object();
    const std::string path = m_document->GetPath().empty() ? std::string() : m_project.ToAssetPath(m_document->GetPath());
    if (m_document->IsWorldDocument())
    {
        state["world"] = path;
        const auto &cells = m_document->GetRegionCells();
        if (!cells.empty())
        {
            state["region"] = {cells.front().x, cells.front().z, cells.back().x, cells.back().z};
        }
    }
    else if (!path.empty())
    {
        state["scene"] = path;
    }
    const glm::vec3 position = m_camera.GetPosition();
    state["camera"] = {position.x, position.y, position.z, m_camera.GetYaw(), m_camera.GetPitch()};
    std::error_code ec;
    std::filesystem::create_directories(m_options.project.cooked.parent_path(), ec);
    std::ofstream stream(m_options.project.cooked.parent_path() / "editor_state.json");
    stream << state.dump(2) << "\n";
}

// --- Opening a project -----------------------------------------------------------------

void EditorApp::OpenProject(const std::filesystem::path &folder)
{
    std::filesystem::path root;
    if (!ResolveProjectFolder(folder, root))
    {
        m_document->SetStatus("Not a project: " + folder.string() + " has no assets folder");
        GENI_LOG_ERROR("editor: %s has no assets folder; create one or pick the project's folder", folder.string().c_str());
        return;
    }
    if (m_project.IsPreparing())
    {
        m_document->SetStatus("Wait for the current project to finish importing");
        return;
    }
    SaveProjectState();

    m_options.project.assets = root / "assets";
    m_options.project.cooked = root / ".geni" / "cooked";
    std::error_code ec;
    std::filesystem::create_directories(m_options.project.cooked, ec);

    m_document->NewScene();
    m_expanded.clear();
    m_assetRoot.reset();
    m_assetFolder.clear();
    m_assetSelected.clear();
    StartProject();

    const std::string key = root.string();
    m_recentProjects.erase(
        std::remove_if(m_recentProjects.begin(), m_recentProjects.end(), [&](const RecentProject &p) { return p.path == key; }),
        m_recentProjects.end()
    );
    m_recentProjects.insert(m_recentProjects.begin(), RecentProject{key, EpochSeconds()});
    if (m_recentProjects.size() > 20)
    {
        m_recentProjects.resize(20);
    }
    if (m_options.script.empty())
    {
        SaveRecentProjects();
    }
    GENI_LOG_INFO("editor: opening project %s", key.c_str());
}

void EditorApp::FinishStartup()
{
    bool ok = true;
    std::string output;
    if (!m_project.FinishPrepare(ok, &output))
    {
        return;
    }
    if (!ok)
    {
        GENI_LOG_WARN("editor: preparing the project reported problems:\n%s", output.c_str());
    }
    m_stage = Stage::Ready;
    m_assetRoot.reset();
    m_assetScanTime = -100.0;
    if (m_reimporting)
    {
        m_reimporting = false;
        Engine::GetInstance().GetAssetManager().ReloadChanged();
        Shader::ReloadChanged();
        m_document->SetStatus(ok ? "Reimported all assets" : "Reimport reported problems; see the console");
        return;
    }
    m_assetManifestSize = static_cast<size_t>(-1);
    GENI_LOG_INFO(
        "Editor ready: assets %s, cooked %s (%.1f s to import)", m_options.project.assets.string().c_str(),
        m_options.project.cooked.string().c_str(), m_time - m_prepareStarted
    );

    if (!m_options.script.empty())
    {
        // A scripted session opens only what it was told to, and must not continue on the
        // wrong scene.
        if (!m_options.scene.empty() && !OpenAssetPath(m_options.scene))
        {
            m_exitCode = 1;
            SetNeedsToBeClosed(true);
        }
        return;
    }
    OpenDefaultContent();
}

bool EditorApp::OpenDefaultContent()
{
    if (!m_options.scene.empty())
    {
        const std::string scene = m_options.scene;
        m_options.scene.clear();
        return OpenAssetPath(scene);
    }

    std::error_code ec;
    auto exists = [&](const std::string &path) { return !path.empty() && std::filesystem::exists(m_project.ToAbsolute(path), ec); };
    const nlohmann::json state = LoadProjectState();
    auto restoreCamera = [&] {
        const nlohmann::json camera = state.value("camera", nlohmann::json());
        if (camera.is_array() && camera.size() == 5)
        {
            m_camera.SetPosition(glm::vec3(camera[0].get<float>(), camera[1].get<float>(), camera[2].get<float>()));
            m_camera.SetAngles(camera[3].get<float>(), camera[4].get<float>());
        }
    };

    // What was open last time.
    const std::string lastWorld = state.value("world", std::string());
    const std::string lastScene = state.value("scene", std::string());
    if (exists(lastWorld) && OpenWorldAssetPath(lastWorld))
    {
        const nlohmann::json region = state.value("region", nlohmann::json());
        if (region.is_array() && region.size() == 4)
        {
            LoadRegion(region[0].get<int>(), region[1].get<int>(), region[2].get<int>(), region[3].get<int>());
        }
        else
        {
            int pick[4];
            if (PickDefaultRegion(pick))
            {
                LoadRegion(pick[0], pick[1], pick[2], pick[3]);
            }
        }
        restoreCamera();
        return true;
    }
    if (exists(lastScene) && OpenAssetPath(lastScene))
    {
        restoreCamera();
        return true;
    }

    // First time: the project's world, with its busiest few cells loaded.
    const auto worlds = m_project.ListAssets(".gworld");
    if (!worlds.empty() && OpenWorldAssetPath(worlds.front()))
    {
        int pick[4];
        if (PickDefaultRegion(pick))
        {
            LoadRegion(pick[0], pick[1], pick[2], pick[3]);
        }
        return true;
    }

    // Else a scene that stands on its own (a world's cells only make sense in the world).
    for (const std::string &scene : m_project.ListAssets(".gscene"))
    {
        if (scene.find("/cells/cell_") == std::string::npos && scene.rfind("cells/cell_", 0) != 0)
        {
            return OpenAssetPath(scene);
        }
    }
    m_document->NewScene();
    m_camera.SetPosition(glm::vec3(8.0f, 6.0f, 12.0f));
    m_camera.SetAngles(35.0f, -22.0f);
    return true;
}

// The window of up to 4 x 4 cells holding the most content (by file size), so a world
// opens on its town rather than on empty fields.
bool EditorApp::PickDefaultRegion(int region[4]) const
{
    if (!m_document->IsWorldDocument())
    {
        return false;
    }
    const WorldSource &world = m_document->GetWorld();
    const int width = std::min(4, world.cellsX);
    const int depth = std::min(4, world.cellsZ);
    std::vector<uintmax_t> sizes(static_cast<size_t>(world.cellsX * world.cellsZ), 0);
    std::error_code ec;
    bool any = false;
    for (int z = 0; z < world.cellsZ; ++z)
    {
        for (int x = 0; x < world.cellsX; ++x)
        {
            if (m_document->CellFileExists(x, z))
            {
                const uintmax_t size = std::filesystem::file_size(m_document->CellPath(x, z), ec);
                sizes[static_cast<size_t>(z * world.cellsX + x)] = ec ? 1 : size + 1;
                any = true;
            }
        }
    }
    if (!any)
    {
        region[0] = region[2] = world.cellsX / 2;
        region[1] = region[3] = world.cellsZ / 2;
        return true;
    }
    uintmax_t best = 0;
    for (int z0 = 0; z0 + depth <= world.cellsZ; ++z0)
    {
        for (int x0 = 0; x0 + width <= world.cellsX; ++x0)
        {
            uintmax_t total = 0;
            for (int z = z0; z < z0 + depth; ++z)
            {
                for (int x = x0; x < x0 + width; ++x)
                {
                    total += sizes[static_cast<size_t>(z * world.cellsX + x)];
                }
            }
            if (total > best)
            {
                best = total;
                region[0] = x0;
                region[1] = z0;
                region[2] = x0 + width - 1;
                region[3] = z0 + depth - 1;
            }
        }
    }
    return best > 0;
}

// --- Project manager -------------------------------------------------------------------

void EditorApp::DrawProjectManager()
{
    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoDocking;
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::GetStyleColorVec4(ImGuiCol_MenuBarBg));
    ImGui::Begin("##ProjectManager", nullptr, flags);
    ImGui::PopStyleColor();

    const float width = std::min(760.0f, viewport->WorkSize.x - 40.0f);
    ImGui::SetCursorPosX((viewport->WorkSize.x - width) * 0.5f);
    ImGui::SetCursorPosY(std::max(30.0f, viewport->WorkSize.y * 0.08f));
    ImGui::BeginGroup();
    {
        ImDrawList &draw = *ImGui::GetWindowDrawList();
        const ImVec2 at = ImGui::GetCursorScreenPos();
        DrawIcon(draw, Icon::Model, ImVec2(at.x + 22.0f, at.y + 22.0f), 40.0f, Theme::Accent);
        ImGui::SetCursorScreenPos(ImVec2(at.x + 54.0f, at.y + 2.0f));
        ImGui::SetWindowFontScale(1.6f);
        ImGui::TextUnformatted("GeniEngine");
        ImGui::SetWindowFontScale(1.0f);
        ImGui::SetCursorScreenPos(ImVec2(at.x + 54.0f, ImGui::GetCursorScreenPos().y - 2.0f));
        ImGui::TextDisabled("Project Manager");
        ImGui::Dummy(ImVec2(0.0f, 14.0f));
    }

    ImGui::SetNextItemWidth(width - 230.0f);
    const bool enter = ImGui::InputTextWithHint(
        "##projectPath", "Project folder (the one that holds assets/)", m_projectPath, sizeof(m_projectPath),
        ImGuiInputTextFlags_EnterReturnsTrue
    );
    ImGui::SameLine();
    if (ImGui::Button("Browse...", ImVec2(100.0f, 0.0f)))
    {
        m_browsePath = m_projectPath[0] ? std::string(m_projectPath) : UserFolder().parent_path().string();
        m_openProjectDialog = true;
    }
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(Theme::AccentDim));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::ColorConvertU32ToFloat4(Theme::Accent));
    if ((ImGui::Button("Open", ImVec2(112.0f, 0.0f)) || enter) && m_projectPath[0] != '\0')
    {
        OpenProject(m_projectPath);
    }
    ImGui::PopStyleColor(2);
    if (ImGui::Button("New Project in This Folder") && m_projectPath[0] != '\0')
    {
        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::path(m_projectPath) / "assets", ec);
        if (ec)
        {
            m_document->SetStatus("Cannot create " + std::string(m_projectPath) + "/assets: " + ec.message());
        }
        else
        {
            OpenProject(m_projectPath);
        }
    }
    if (!m_document->GetStatus().empty())
    {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", m_document->GetStatus().c_str());
    }

    ImGui::Dummy(ImVec2(0.0f, 10.0f));
    ImGui::TextDisabled("RECENT PROJECTS");
    const float listHeight = std::max(160.0f, viewport->WorkSize.y - ImGui::GetCursorPosY() - 40.0f);
    ImGui::BeginChild("##recent", ImVec2(width, listHeight), ImGuiChildFlags_Borders);
    if (m_recentProjects.empty())
    {
        ImGui::Dummy(ImVec2(0.0f, 20.0f));
        ImGui::TextDisabled("  No recent projects. Type or browse to a folder that has an assets/ folder.");
    }
    int remove = -1;
    for (size_t i = 0; i < m_recentProjects.size(); ++i)
    {
        const RecentProject &project = m_recentProjects[i];
        ImGui::PushID(static_cast<int>(i));
        const std::filesystem::path path(project.path);
        std::error_code ec;
        const bool missing = !std::filesystem::is_directory(path / "assets", ec);
        const float rowHeight = 50.0f;
        const ImVec2 at = ImGui::GetCursorScreenPos();
        if (ImGui::Selectable("##row", false, ImGuiSelectableFlags_AllowDoubleClick, ImVec2(0.0f, rowHeight)) && !missing)
        {
            OpenProject(path);
        }
        if (ImGui::BeginPopupContextItem("##menu"))
        {
            if (ImGui::MenuItem("Remove from List"))
            {
                remove = static_cast<int>(i);
            }
            ImGui::EndPopup();
        }
        ImDrawList &draw = *ImGui::GetWindowDrawList();
        DrawIcon(draw, Icon::Folder, ImVec2(at.x + 24.0f, at.y + rowHeight * 0.5f), 26.0f, missing ? Theme::TextDim : IconColor(Icon::Folder));
        const std::string name = path.filename().string();
        draw.AddText(ImVec2(at.x + 50.0f, at.y + 7.0f), missing ? Theme::TextDim : Theme::Text, name.c_str());
        const std::string detail = project.path + (missing ? "   (missing)" : "");
        draw.AddText(ImVec2(at.x + 50.0f, at.y + 27.0f), Theme::TextDim, detail.c_str());
        const std::string when = AgoText(project.lastOpened);
        const float whenWidth = ImGui::CalcTextSize(when.c_str()).x;
        draw.AddText(ImVec2(at.x + ImGui::GetContentRegionAvail().x - whenWidth - 8.0f, at.y + 7.0f), Theme::TextDim, when.c_str());
        ImGui::PopID();
    }
    if (remove >= 0)
    {
        m_recentProjects.erase(m_recentProjects.begin() + remove);
        SaveRecentProjects();
    }
    ImGui::EndChild();
    ImGui::EndGroup();
    DrawFolderBrowser();
    ImGui::End();
}

// A small folder picker (no native dialog is vendored): walk folders, pick one.
void EditorApp::DrawFolderBrowser()
{
    if (m_openProjectDialog)
    {
        m_openProjectDialog = false;
        ImGui::OpenPopup("Choose Project Folder");
    }
    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(600.0f, 440.0f), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("Choose Project Folder"))
    {
        return;
    }
    std::error_code ec;
    std::filesystem::path current(m_browsePath);
    if (current.empty() || !std::filesystem::is_directory(current, ec))
    {
        current = std::filesystem::current_path(ec);
    }
    if (IconButton("##up", Icon::FolderOpen, false, "Up one folder"))
    {
        current = current.parent_path();
    }
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(current.string().c_str());
    const bool isProject = std::filesystem::is_directory(current / "assets", ec);

    ImGui::BeginChild("##folders", ImVec2(0.0f, -ImGui::GetFrameHeightWithSpacing() * 1.4f), ImGuiChildFlags_Borders);
    std::vector<std::filesystem::path> folders;
    for (auto it = std::filesystem::directory_iterator(current, ec); !ec && it != std::filesystem::directory_iterator(); it.increment(ec))
    {
        std::error_code entryError;
        const std::string name = it->path().filename().string();
        if (it->is_directory(entryError) && !name.empty() && name[0] != '.')
        {
            folders.push_back(it->path());
        }
    }
    std::sort(folders.begin(), folders.end());
    std::filesystem::path next = current;
    for (const auto &folder : folders)
    {
        const bool project = std::filesystem::is_directory(folder / "assets", ec);
        ImGui::PushID(folder.string().c_str());
        IconText(Icon::Folder, project ? Theme::Accent : 0);
        if (ImGui::Selectable(folder.filename().string().c_str(), false, ImGuiSelectableFlags_AllowDoubleClick))
        {
            next = folder;
        }
        if (project)
        {
            ImGui::SameLine();
            ImGui::TextDisabled("project");
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
    m_browsePath = next.string();

    ImGui::BeginDisabled(!isProject);
    if (ImGui::Button("Open This Project"))
    {
        std::snprintf(m_projectPath, sizeof(m_projectPath), "%s", current.string().c_str());
        ImGui::CloseCurrentPopup();
        OpenProject(current);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Use This Folder"))
    {
        std::snprintf(m_projectPath, sizeof(m_projectPath), "%s", current.string().c_str());
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
    {
        ImGui::CloseCurrentPopup();
    }
    if (!isProject)
    {
        ImGui::SameLine();
        ImGui::TextDisabled("No assets/ folder here");
    }
    ImGui::EndPopup();
}

// The import dialog shown while the first cook runs. The rest of the editor stays drawn
// (and usable for looking around) behind it.
void EditorApp::DrawPrepareProgress()
{
    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(520.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18.0f, 16.0f));
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_AlwaysAutoResize;
    ImGui::Begin("##Importing", nullptr, flags);
    ImGui::PopStyleVar();
    const CookProgress progress = m_project.GetCookProgress();
    {
        const ImVec2 at = ImGui::GetCursorScreenPos();
        Spinner(*ImGui::GetWindowDrawList(), ImVec2(at.x + 9.0f, at.y + 9.0f), 8.0f, Theme::Accent, m_time);
        ImGui::Dummy(ImVec2(20.0f, 18.0f));
        ImGui::SameLine();
        ImGui::TextUnformatted(progress.active ? "Importing assets" : "Preparing project");
    }
    ImGui::TextDisabled("%s", m_options.project.assets.parent_path().string().c_str());
    ImGui::Dummy(ImVec2(0.0f, 4.0f));
    char overlay[64];
    float fraction = 0.0f;
    if (progress.active && progress.total > 0)
    {
        fraction = static_cast<float>(progress.done) / static_cast<float>(progress.total);
        std::snprintf(overlay, sizeof(overlay), "%d / %d", progress.done, progress.total);
    }
    else
    {
        // Indeterminate: checking what changed, or compiling shaders.
        fraction = static_cast<float>(std::fmod(m_time * 0.5, 1.0));
        std::snprintf(overlay, sizeof(overlay), "checking");
    }
    ImGui::ProgressBar(fraction, ImVec2(-1.0f, 0.0f), overlay);
    std::string current = progress.current;
    if (current.size() > 64)
    {
        current = "..." + current.substr(current.size() - 61);
    }
    ImGui::TextDisabled("%s", current.empty() ? " " : current.c_str());
    ImGui::TextDisabled("%.0f s. The first import of a large project takes a while; later ones only redo what changed.", m_time - m_prepareStarted);
    ImGui::End();
}

} // namespace Geni::Editor
