#include "EditorApp.h"

#include <GLFW/glfw3.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <imgui.h>
#include <imgui_internal.h>
#include <sstream>

namespace Geni::Editor
{

namespace
{

double NowSeconds()
{
    using Clock = std::chrono::steady_clock;
    static const Clock::time_point start = Clock::now();
    return std::chrono::duration<double>(Clock::now() - start).count();
}

void CopyToBuffer(char *buffer, size_t size, const std::string &text)
{
    std::snprintf(buffer, size, "%s", text.c_str());
}

bool EndsWith(const std::string &text, const std::string &suffix)
{
    return text.size() >= suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

} // namespace

EditorApp::EditorApp(EditorOptions options) : m_options(std::move(options))
{
}

bool EditorApp::Init()
{
    auto &engine = Engine::GetInstance();

    m_scene = new Scene();
    engine.SetScene(m_scene);
    m_document = std::make_unique<EditorDocument>(*m_scene, &engine.GetAssetManager());

    m_project.Init(m_options.project, engine.GetFileSystem(), &engine.GetAssetManager(), engine.GetJobSystem());
    std::string output;
    if (!m_project.Prepare(&output))
    {
        GENI_LOG_WARN("editor: preparing the project reported problems:\n%s", output.c_str());
    }

    std::string scene = m_options.scene;
    if (scene.empty() && m_options.script.empty())
    {
        const auto scenes = m_project.ListAssets(".gscene");
        if (!scenes.empty())
        {
            scene = scenes.front();
        }
    }
    if (!scene.empty() && !OpenAssetPath(scene))
    {
        // A scripted session must not continue on the wrong scene.
        if (!m_options.script.empty())
        {
            return false;
        }
    }
    ApplyPlayState();
    GENI_LOG_INFO(
        "Editor ready: assets %s, cooked %s", m_options.project.assets.string().c_str(),
        m_options.project.cooked.string().c_str()
    );
    return true;
}

void EditorApp::Destroy()
{
    m_document.reset();
}

void EditorApp::Update(float deltaTime)
{
    m_time = NowSeconds();
    m_document->PruneSelection();
    UpdateHotReload();

    // Closing the window with unsaved changes asks first.
    GLFWwindow *window = Engine::GetInstance().GetWindow();
    if (glfwWindowShouldClose(window) && m_document->IsDirty() && m_options.script.empty() &&
        m_pendingAction != PendingAction::Quit)
    {
        glfwSetWindowShouldClose(window, GLFW_FALSE);
        RequestAction(PendingAction::Quit);
    }

    // The bars come first: they take their space out of the area the dock space fills.
    DrawMenuBar();
    DrawToolbar();
    DrawStatusBar();
    DrawDockSpace();
    DrawHierarchy();
    DrawInspector();
    // Console before Assets: of two panels docked together, the one submitted last
    // starts in front.
    DrawConsole();
    DrawAssets();
    DrawWorldPanel();
    HandleShortcuts();
    UpdateViewport(deltaTime);
    DrawOverlays();
    DrawDialogs();
    RunScript();

    ApplyPlayState();
    m_scene->Update(deltaTime, m_document->IsPlaying() ? SceneUpdateMode::Play : SceneUpdateMode::Edit);
    UpdateWindowTitle();
}

void EditorApp::ApplyPlayState()
{
    auto &engine = Engine::GetInstance();
    const bool playing = m_document->IsPlaying();
    engine.SetPhysicsPaused(!playing);

    GameObject *mainCamera = m_scene->GetMainCamera();
    const bool gameCamera = playing && mainCamera && mainCamera->GetComponent<CameraComponent>();
    if (gameCamera)
    {
        engine.ClearViewOverride();
    }
    else
    {
        engine.SetViewOverride(m_camera.GetViewOverride());
    }

    // The scene is drawn where the panels leave room.
    const ImVec2 scale = ImGui::GetIO().DisplayFramebufferScale;
    SceneViewport viewport;
    viewport.x = static_cast<uint32_t>(std::max(0.0f, m_viewportPos.x * scale.x));
    viewport.y = static_cast<uint32_t>(std::max(0.0f, m_viewportPos.y * scale.y));
    viewport.width = static_cast<uint32_t>(std::max(1.0f, m_viewportSize.x * scale.x));
    viewport.height = static_cast<uint32_t>(std::max(1.0f, m_viewportSize.y * scale.y));
    engine.GetRenderer().SetSceneViewport(viewport);
}

void EditorApp::UpdateHotReload()
{
    if (!m_options.hotReload || m_document->IsPlaying())
    {
        return;
    }
    ReloadReport report;
    if (!m_project.Update(m_time, report))
    {
        return;
    }
    for (AssetID prefab : report.prefabs)
    {
        m_document->RefreshPrefabInstances(prefab);
    }
    if (report.failed)
    {
        m_document->SetStatus("Hot reload failed; see the console");
    }
    else if (report.shaders > 0 || report.assets > 0)
    {
        m_document->SetStatus(
            "Reloaded " + std::to_string(report.shaders) + " shaders, " + std::to_string(report.assets) + " assets"
        );
    }
}

void EditorApp::UpdateWindowTitle()
{
    std::string title = "GeniEngine Editor - ";
    title += m_document->GetPath().empty() ? "untitled" : m_project.ToAssetPath(m_document->GetPath());
    if (m_document->IsDirty())
    {
        title += " *";
    }
    if (m_document->IsPlaying())
    {
        title += " (playing)";
    }
    if (title != m_title)
    {
        m_title = title;
        glfwSetWindowTitle(Engine::GetInstance().GetWindow(), title.c_str());
    }
}

// --- Layout ----------------------------------------------------------------------------

void EditorApp::DrawDockSpace()
{
    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    const ImGuiID dockSpace = ImGui::GetID("GeniEditorDockSpace");

    // First run (no saved layout): hierarchy left, inspector right, assets and console
    // below, the scene in the middle.
    if (!m_layoutBuilt)
    {
        m_layoutBuilt = true;
        if (ImGui::DockBuilderGetNode(dockSpace) == nullptr)
        {
            ImGui::DockBuilderAddNode(dockSpace, ImGuiDockNodeFlags_DockSpace);
            ImGui::DockBuilderSetNodeSize(dockSpace, viewport->WorkSize);
            ImGuiID center = dockSpace;
            const ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.20f, nullptr, &center);
            const ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.30f, nullptr, &center);
            const ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.28f, nullptr, &center);
            ImGui::DockBuilderDockWindow("Hierarchy", left);
            ImGui::DockBuilderDockWindow("Inspector", right);
            ImGui::DockBuilderDockWindow("Assets", bottom);
            ImGui::DockBuilderDockWindow("Console", bottom);
            ImGui::DockBuilderFinish(dockSpace);
        }
    }

    ImGui::DockSpaceOverViewport(dockSpace, viewport, ImGuiDockNodeFlags_PassthruCentralNode);

    // The empty central node is the scene viewport.
    if (const ImGuiDockNode *central = ImGui::DockBuilderGetCentralNode(dockSpace))
    {
        m_viewportPos = glm::vec2(central->Pos.x - viewport->Pos.x, central->Pos.y - viewport->Pos.y);
        m_viewportSize = glm::vec2(std::max(central->Size.x, 1.0f), std::max(central->Size.y, 1.0f));
    }
}

void EditorApp::DrawMenuBar()
{
    if (!ImGui::BeginMainMenuBar())
    {
        return;
    }
    const bool playing = m_document->IsPlaying();
    if (ImGui::BeginMenu("File"))
    {
        if (ImGui::MenuItem("New Scene", "Ctrl+N"))
        {
            RequestAction(PendingAction::NewScene);
        }
        if (ImGui::MenuItem("Open...", "Ctrl+O"))
        {
            m_openOpen = true;
        }
        if (ImGui::MenuItem("Open World..."))
        {
            m_openWorld = true;
        }
        if (ImGui::MenuItem("Save", "Ctrl+S", false, !playing))
        {
            SaveOrAsk();
        }
        if (ImGui::MenuItem("Save As...", "Ctrl+Shift+S", false, !playing))
        {
            m_openSaveAs = true;
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Cook Assets Now", nullptr, false, !m_options.project.cooker.empty()))
        {
            std::string output;
            const bool ok = m_project.Cook(&output);
            Engine::GetInstance().GetAssetManager().ReloadChanged();
            m_document->SetStatus(ok ? "Cooked assets" : "Cooking failed; see the console");
            if (!ok)
            {
                GENI_LOG_ERROR("cook: %s", output.c_str());
            }
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Quit"))
        {
            RequestAction(PendingAction::Quit);
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit"))
    {
        auto &commands = m_document->GetCommands();
        const std::string undo = std::string("Undo ") + commands.GetUndoName();
        const std::string redo = std::string("Redo ") + commands.GetRedoName();
        if (ImGui::MenuItem(undo.c_str(), "Ctrl+Z", false, !playing && commands.CanUndo()))
        {
            m_document->Undo();
        }
        if (ImGui::MenuItem(redo.c_str(), "Ctrl+Shift+Z", false, !playing && commands.CanRedo()))
        {
            m_document->Redo();
        }
        ImGui::Separator();
        const bool hasSelection = !m_document->GetSelection().empty() && !playing;
        if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, hasSelection))
        {
            m_document->DuplicateSelection();
        }
        if (ImGui::MenuItem("Duplicate as Grid...", nullptr, false, hasSelection))
        {
            m_openGrid = true;
        }
        if (ImGui::MenuItem("Delete", "Del", false, hasSelection))
        {
            m_document->DeleteSelection();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Focus Selection", "F", false, !m_document->GetSelection().empty()))
        {
            FocusSelection();
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("GameObject", !playing))
    {
        if (ImGui::MenuItem("Create Empty"))
        {
            m_document->CreateEmpty();
        }
        if (ImGui::MenuItem("Create Empty Child", nullptr, false, m_document->CanEdit(m_document->GetPrimary())))
        {
            GameObject *parent = m_document->GetPrimary();
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
        ImGui::Separator();
        GameObject *primary = m_document->GetPrimary();
        if (ImGui::MenuItem("Create Prefab...", nullptr, false, m_document->CanEdit(primary) && !primary->IsPrefabInstance()))
        {
            m_prefabObject = primary->GetID();
            CopyToBuffer(m_pathBuffer, sizeof(m_pathBuffer), "prefabs/" + primary->GetName() + ".gprefab");
            m_openPrefab = true;
        }
        if (ImGui::MenuItem("Unpack Prefab", nullptr, false, m_document->CanEdit(primary) && primary->IsPrefabInstance()))
        {
            m_document->UnpackPrefab(*primary);
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View"))
    {
        ImGui::MenuItem("Grid", nullptr, &m_showGrid);
        ImGui::MenuItem("Light and Camera Markers", nullptr, &m_showMarkers);
        bool stats = Engine::GetInstance().GetDebugOverlay().IsVisible();
        if (ImGui::MenuItem("Engine Stats", "F1", &stats))
        {
            Engine::GetInstance().GetDebugOverlay().SetVisible(stats);
        }
        ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
}

void EditorApp::DrawToolbar()
{
    ImGuiViewport *viewport = ImGui::GetMainViewport();
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar;
    if (ImGui::BeginViewportSideBar("##GeniToolbar", viewport, ImGuiDir_Up, ImGui::GetFrameHeight(), flags))
    {
        if (ImGui::BeginMenuBar())
        {
            const bool playing = m_document->IsPlaying();
            auto modeButton = [&](const char *label, GizmoMode mode) {
                const bool active = m_gizmo.mode == mode;
                if (active)
                {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
                }
                if (ImGui::Button(label))
                {
                    m_gizmo.mode = mode;
                }
                if (active)
                {
                    ImGui::PopStyleColor();
                }
            };
            modeButton("Move (W)", GizmoMode::Translate);
            modeButton("Rotate (E)", GizmoMode::Rotate);
            modeButton("Scale (R)", GizmoMode::Scale);
            ImGui::Separator();
            if (ImGui::Button(m_gizmo.space == GizmoSpace::World ? "World" : "Local"))
            {
                m_gizmo.space = m_gizmo.space == GizmoSpace::World ? GizmoSpace::Local : GizmoSpace::World;
            }
            ImGui::Checkbox("Snap", &m_gizmo.snap.enabled);
            if (m_gizmo.snap.enabled)
            {
                ImGui::SetNextItemWidth(60.0f);
                ImGui::DragFloat("##snapMove", &m_gizmo.snap.translate, 0.05f, 0.01f, 100.0f, "%.2f m");
                ImGui::SetNextItemWidth(60.0f);
                ImGui::DragFloat("##snapRotate", &m_gizmo.snap.rotateDegrees, 1.0f, 1.0f, 180.0f, "%.0f deg");
            }
            ImGui::Separator();
            if (ImGui::Button(playing ? "Stop (Ctrl+P)" : "Play (Ctrl+P)"))
            {
                TogglePlay();
            }
            ImGui::EndMenuBar();
        }
    }
    ImGui::End();
}

void EditorApp::DrawStatusBar()
{
    ImGuiViewport *viewport = ImGui::GetMainViewport();
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar;
    if (ImGui::BeginViewportSideBar("##GeniStatusBar", viewport, ImGuiDir_Down, ImGui::GetFrameHeight(), flags))
    {
        if (ImGui::BeginMenuBar())
        {
            ImGui::TextUnformatted(m_document->GetStatus().c_str());
            const char *mode = m_document->IsPlaying() ? "PLAYING" : m_document->IsPrefabDocument() ? "Prefab" : "Scene";
            const std::string right =
                std::string(mode) + "   " + std::to_string(m_document->GetSelection().size()) + " selected" +
                (m_project.IsBusy() ? "   cooking..." : "");
            ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::CalcTextSize(right.c_str()).x - 16.0f);
            ImGui::TextUnformatted(right.c_str());
            ImGui::EndMenuBar();
        }
    }
    ImGui::End();
}

void EditorApp::HandleShortcuts()
{
    ImGuiIO &io = ImGui::GetIO();
    if (io.WantTextInput)
    {
        return;
    }
    const bool playing = m_document->IsPlaying();
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_P))
    {
        TogglePlay();
    }
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_N))
    {
        RequestAction(PendingAction::NewScene);
    }
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_O))
    {
        m_openOpen = true;
    }
    if (playing)
    {
        return;
    }
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z) ||
        ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Y))
    {
        m_document->Redo();
    }
    else if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Z))
    {
        m_document->Undo();
    }
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_S))
    {
        m_openSaveAs = true;
    }
    else if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S))
    {
        SaveOrAsk();
    }
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_D))
    {
        m_document->DuplicateSelection();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) || ImGui::IsKeyPressed(ImGuiKey_Backspace, false))
    {
        m_document->DeleteSelection();
    }
    // Tool keys, unless the right button is flying the camera (W is "forward" then).
    if (!m_cameraDragging && !io.KeyCtrl && !io.KeySuper)
    {
        if (ImGui::IsKeyPressed(ImGuiKey_W, false))
        {
            m_gizmo.mode = GizmoMode::Translate;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_E, false))
        {
            m_gizmo.mode = GizmoMode::Rotate;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_R, false))
        {
            m_gizmo.mode = GizmoMode::Scale;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_F, false))
        {
            FocusSelection();
        }
    }
}

// --- Actions ---------------------------------------------------------------------------

void EditorApp::TogglePlay()
{
    EndNodeEdit();
    EndPropertyEdit();
    if (m_document->IsPlaying())
    {
        m_document->Stop();
    }
    else
    {
        m_document->Play();
    }
}

void EditorApp::RequestAction(PendingAction action, const std::string &path)
{
    if (m_document->IsPlaying())
    {
        m_document->Stop();
    }
    if (m_document->IsDirty())
    {
        m_pendingAction = action;
        m_pendingPath = path;
        m_openConfirm = true;
        return;
    }
    PerformAction(action, path);
}

void EditorApp::PerformAction(PendingAction action, const std::string &path)
{
    m_pendingAction = PendingAction::None;
    switch (action)
    {
    case PendingAction::NewScene:
        m_document->NewScene();
        m_expanded.clear();
        break;
    case PendingAction::OpenScene:
        OpenAssetPath(path);
        break;
    case PendingAction::OpenWorld:
        OpenWorldAssetPath(path);
        break;
    case PendingAction::Quit:
        SetNeedsToBeClosed(true);
        break;
    case PendingAction::None:
        break;
    }
}

bool EditorApp::OpenAssetPath(const std::string &assetPath)
{
    std::string error;
    if (!m_document->Open(m_project.ToAbsolute(assetPath), &error))
    {
        GENI_LOG_ERROR("editor: %s", error.c_str());
        m_document->SetStatus("Could not open " + assetPath);
        return false;
    }
    m_expanded.clear();

    // Start with the whole scene in view: frame every object's position, not only the
    // roots (a scene is often one root with everything under it).
    glm::vec3 low(0.0f);
    glm::vec3 high(0.0f);
    bool any = false;
    std::vector<GameObject *> stack;
    for (const auto &root : m_scene->GetObjects())
    {
        stack.push_back(root.get());
    }
    while (!stack.empty())
    {
        GameObject *object = stack.back();
        stack.pop_back();
        const glm::vec3 position = object->GetWorldPosition();
        low = any ? glm::min(low, position) : position;
        high = any ? glm::max(high, position) : position;
        any = true;
        for (const auto &child : object->GetChildren())
        {
            stack.push_back(child.get());
        }
    }
    m_camera.SetAngles(-35.0f, -25.0f);
    m_camera.Focus((low + high) * 0.5f, std::max(glm::length(high - low) * 0.25f, 5.0f));
    return true;
}

bool EditorApp::OpenWorldAssetPath(const std::string &assetPath)
{
    std::string error;
    if (!m_document->OpenWorld(m_project.ToAbsolute(assetPath), &error))
    {
        GENI_LOG_ERROR("editor: %s", error.c_str());
        m_document->SetStatus("Could not open " + assetPath + ": " + error);
        return false;
    }
    m_expanded.clear();
    // Start the region pick in the middle of the world.
    const WorldSource &world = m_document->GetWorld();
    m_regionPick[0] = m_regionPick[2] = world.cellsX / 2;
    m_regionPick[1] = m_regionPick[3] = world.cellsZ / 2;
    return true;
}

bool EditorApp::LoadRegion(int x0, int z0, int x1, int z1)
{
    std::string error;
    if (!m_document->LoadRegion(x0, z0, x1, z1, &error))
    {
        GENI_LOG_ERROR("editor: %s", error.c_str());
        m_document->SetStatus("Could not load the region: " + error);
        return false;
    }
    m_expanded.clear();
    // Frame the region: the origin is its first corner.
    const auto &cells = m_document->GetRegionCells();
    if (!cells.empty())
    {
        m_regionPick[0] = cells.front().x;
        m_regionPick[1] = cells.front().z;
        m_regionPick[2] = cells.back().x;
        m_regionPick[3] = cells.back().z;
    }
    const float size = m_document->GetWorld().cellSize;
    const int columns = cells.empty() ? 1 : cells.back().x - cells.front().x + 1;
    const int rows = cells.empty() ? 1 : cells.back().z - cells.front().z + 1;
    const glm::vec3 centre(0.5f * size * columns, 0.0f, 0.5f * size * rows);
    m_camera.SetAngles(-35.0f, -35.0f);
    m_camera.Focus(centre, 0.45f * size * static_cast<float>(std::max(columns, rows)));
    return true;
}

bool EditorApp::SaveToAssetPath(const std::string &assetPath)
{
    std::string path = assetPath;
    if (!EndsWith(path, ".gscene") && !EndsWith(path, ".gprefab"))
    {
        path += m_document->IsPrefabDocument() ? ".gprefab" : ".gscene";
    }
    std::string error;
    if (!m_document->SaveAs(m_project.ToAbsolute(path), &error))
    {
        GENI_LOG_ERROR("editor: %s", error.c_str());
        m_document->SetStatus("Could not save: " + error);
        return false;
    }
    // Usable as an asset right away, before the next cook lists it.
    Engine::GetInstance().GetAssetManager().RegisterSourceAsset(
        EndsWith(path, ".gprefab") ? AssetType::Prefab : AssetType::Scene, path
    );
    return true;
}

void EditorApp::SaveOrAsk()
{
    if (m_document->GetPath().empty())
    {
        m_openSaveAs = true;
        return;
    }
    std::string error;
    if (!m_document->Save(&error))
    {
        GENI_LOG_ERROR("editor: %s", error.c_str());
        m_document->SetStatus("Could not save: " + error);
    }
}

std::string EditorApp::AssetLabel(AssetID id)
{
    if (!id.IsValid())
    {
        return "(none)";
    }
    const AssetManifestEntry *entry = Engine::GetInstance().GetAssetManager().GetManifest().Find(id);
    return entry ? entry->source : id.ToHex() + " (missing)";
}

// --- Dialogs ---------------------------------------------------------------------------

void EditorApp::DrawDialogs()
{
    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    auto openModal = [&](bool &flag, const char *name) {
        if (flag)
        {
            flag = false;
            ImGui::OpenPopup(name);
        }
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        return ImGui::BeginPopupModal(name, nullptr, ImGuiWindowFlags_AlwaysAutoResize);
    };

    if (openModal(m_openConfirm, "Unsaved Changes"))
    {
        ImGui::TextUnformatted("The scene has changes that are not saved.");
        if (ImGui::Button("Discard"))
        {
            const PendingAction action = m_pendingAction;
            // Dropping the history is what makes the document clean.
            m_document->GetCommands().Clear();
            PerformAction(action, m_pendingPath);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
        {
            m_pendingAction = PendingAction::None;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (m_openSaveAs)
    {
        const std::string current =
            m_document->GetPath().empty() ? std::string("scenes/untitled.gscene") : m_project.ToAssetPath(m_document->GetPath());
        CopyToBuffer(m_pathBuffer, sizeof(m_pathBuffer), current);
    }
    if (openModal(m_openSaveAs, "Save Scene As"))
    {
        ImGui::TextUnformatted("Path in the assets folder:");
        ImGui::SetNextItemWidth(420.0f);
        const bool enter = ImGui::InputText("##path", m_pathBuffer, sizeof(m_pathBuffer), ImGuiInputTextFlags_EnterReturnsTrue);
        if ((ImGui::Button("Save") || enter) && m_pathBuffer[0] != '\0')
        {
            SaveToAssetPath(m_pathBuffer);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
        {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (openModal(m_openOpen, "Open Scene"))
    {
        ImGui::TextUnformatted("Scenes and prefabs in the assets folder:");
        ImGui::BeginChild("##files", ImVec2(440.0f, 260.0f), ImGuiChildFlags_Borders);
        for (const char *extension : {".gscene", ".gprefab"})
        {
            for (const std::string &path : m_project.ListAssets(extension))
            {
                if (ImGui::Selectable(path.c_str()))
                {
                    RequestAction(PendingAction::OpenScene, path);
                    ImGui::CloseCurrentPopup();
                }
            }
        }
        ImGui::EndChild();
        if (ImGui::Button("Cancel"))
        {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (openModal(m_openWorld, "Open World"))
    {
        ImGui::TextUnformatted("Worlds in the assets folder:");
        ImGui::BeginChild("##worlds", ImVec2(440.0f, 200.0f), ImGuiChildFlags_Borders);
        for (const std::string &path : m_project.ListAssets(".gworld"))
        {
            if (ImGui::Selectable(path.c_str()))
            {
                RequestAction(PendingAction::OpenWorld, path);
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndChild();
        if (ImGui::Button("Cancel"))
        {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (openModal(m_openPrefab, "Create Prefab"))
    {
        ImGui::TextUnformatted("The object becomes an instance of the new prefab.\nPath in the assets folder:");
        ImGui::SetNextItemWidth(420.0f);
        ImGui::InputText("##prefabPath", m_pathBuffer, sizeof(m_pathBuffer));
        if (ImGui::Button("Create") && m_pathBuffer[0] != '\0')
        {
            std::string path = m_pathBuffer;
            if (!EndsWith(path, ".gprefab"))
            {
                path += ".gprefab";
            }
            GameObject *object = m_scene->FindObjectByID(m_prefabObject);
            std::string error;
            const AssetID id = Engine::GetInstance().GetAssetManager().RegisterSourceAsset(AssetType::Prefab, path);
            if (!object || !m_document->CreatePrefab(*object, m_project.ToAbsolute(path), id, &error))
            {
                GENI_LOG_ERROR("editor: could not create the prefab: %s", error.c_str());
                m_document->SetStatus("Could not create the prefab: " + error);
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
        {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (openModal(m_openGrid, "Duplicate as Grid"))
    {
        ImGui::TextUnformatted("Copies of the selection on a grid; the selection is the first cell.");
        ImGui::InputInt("Count X", &m_gridCountX);
        ImGui::InputInt("Count Z", &m_gridCountZ);
        ImGui::DragFloat("Spacing X", &m_gridSpacing.x, 0.1f);
        ImGui::DragFloat("Spacing Z", &m_gridSpacing.z, 0.1f);
        m_gridCountX = std::clamp(m_gridCountX, 1, 500);
        m_gridCountZ = std::clamp(m_gridCountZ, 1, 500);
        if (ImGui::Button("Duplicate"))
        {
            m_document->DuplicateGrid(m_gridCountX, m_gridCountZ, m_gridSpacing);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
        {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

// --- Scripted session --------------------------------------------------------------------

void EditorApp::RunScript()
{
    if (m_options.script.empty() || m_scriptQuit)
    {
        return;
    }
    // Let asset loads and a requested screenshot settle before the next command.
    if (m_scriptWait > 0)
    {
        --m_scriptWait;
        return;
    }
    if (Engine::GetInstance().GetAssetManager().IsLoading() || m_project.IsBusy())
    {
        return;
    }
    if (m_scriptCursor >= m_options.script.size())
    {
        m_scriptQuit = true;
        SetNeedsToBeClosed(true);
        return;
    }
    const std::string line = m_options.script[m_scriptCursor++];
    std::string error;
    if (!RunScriptCommand(line, error))
    {
        GENI_LOG_ERROR("editor script: '%s' failed: %s", line.c_str(), error.c_str());
        m_exitCode = 5;
        m_scriptQuit = true;
        SetNeedsToBeClosed(true);
    }
}

// One command of a scripted session. Commands go through EditorDocument exactly as the
// menus do. Returns false with a reason when the command fails or an `expect` is not met.
bool EditorApp::RunScriptCommand(const std::string &line, std::string &error)
{
    std::istringstream stream(line);
    std::string command;
    stream >> command;
    auto &assets = Engine::GetInstance().GetAssetManager();
    auto rest = [&stream] {
        std::string text;
        std::getline(stream >> std::ws, text);
        return text;
    };
    auto fail = [&error](const std::string &message) {
        error = message;
        return false;
    };
    GameObject *primary = m_document->GetPrimary();

    if (command.empty() || command[0] == '#')
    {
        return true;
    }
    if (command == "new")
    {
        m_document->NewScene();
        return true;
    }
    if (command == "open")
    {
        return OpenAssetPath(rest()) || fail("could not open the scene");
    }
    if (command == "world")
    {
        return OpenWorldAssetPath(rest()) || fail("could not open the world");
    }
    if (command == "region")
    {
        int x0 = 0, z0 = 0, x1 = 0, z1 = 0;
        stream >> x0 >> z0 >> x1 >> z1;
        if (!stream)
        {
            return fail("usage: region X0 Z0 X1 Z1");
        }
        return LoadRegion(x0, z0, x1, z1) || fail("could not load the region");
    }
    if (command == "import")
    {
        // The scene the cooker made from a model, added to the current scene.
        const std::string source = rest();
        const auto scene = assets.LoadScene(assets.FindBySource(AssetType::Scene, source), false);
        if (!scene.IsReady())
        {
            return fail("no cooked scene for " + source);
        }
        return m_document->ImportScene(scene->document, &error);
    }
    if (command == "save")
    {
        return m_document->Save(&error);
    }
    if (command == "saveas")
    {
        return SaveToAssetPath(rest()) || fail("could not save");
    }
    if (command == "select")
    {
        GameObject *object = m_scene->FindObject(rest());
        if (!object)
        {
            return fail("no such object");
        }
        m_document->Select(object->GetID());
        return true;
    }
    if (command == "selectroots")
    {
        m_document->ClearSelection();
        for (const auto &root : m_scene->GetObjects())
        {
            m_document->Select(root->GetID(), true);
        }
        return true;
    }
    if (command == "move" || command == "scale")
    {
        glm::vec3 value(0.0f);
        stream >> value.x >> value.y >> value.z;
        if (!primary)
        {
            return fail("nothing selected");
        }
        NodeState state = NodeState::Capture(*primary);
        if (command == "move")
        {
            state.position += value;
        }
        else
        {
            state.scale *= value;
        }
        return m_document->SetNodeState(*primary, state) || fail("the object did not change");
    }
    if (command == "rename")
    {
        if (!primary)
        {
            return fail("nothing selected");
        }
        NodeState state = NodeState::Capture(*primary);
        state.name = rest();
        return m_document->SetNodeState(*primary, state, "Rename") || fail("the name did not change");
    }
    if (command == "create")
    {
        const std::string kind = rest();
        GameObject *created = kind == "camera"        ? m_document->CreateCamera()
                              : kind == "sun"         ? m_document->CreateLight(LightType::Directional)
                              : kind == "pointlight"  ? m_document->CreateLight(LightType::Point)
                              : kind == "spotlight"   ? m_document->CreateLight(LightType::Spot)
                                                      : m_document->CreateEmpty(kind.empty() ? "GameObject" : kind);
        return created != nullptr || fail("could not create the object");
    }
    if (command == "parent")
    {
        // Moves the selection under the named object ("-" for the scene root).
        const std::string name = rest();
        GameObject *parent = name == "-" ? nullptr : m_scene->FindObject(name);
        if (name != "-" && !parent)
        {
            return fail("no such object");
        }
        return m_document->Reparent(m_document->GetSelectedRoots(), parent) || fail("nothing was moved");
    }
    if (command == "set")
    {
        // set <component index> <property> <json value>
        int component = 0;
        std::string property;
        stream >> component >> property;
        const nlohmann::json value = nlohmann::json::parse(rest(), nullptr, false);
        if (!primary || value.is_discarded())
        {
            return fail("needs a selection and a JSON value");
        }
        return m_document->SetProperty(*primary, component, property, value) || fail("the property did not change");
    }
    if (command == "duplicate")
    {
        return !m_document->DuplicateSelection().empty() || fail("nothing to duplicate");
    }
    if (command == "grid")
    {
        int countX = 1, countZ = 1;
        float spacingX = 1.0f, spacingZ = 1.0f;
        stream >> countX >> countZ >> spacingX >> spacingZ;
        return !m_document->DuplicateGrid(countX, countZ, glm::vec3(spacingX, 0.0f, spacingZ)).empty() ||
               fail("nothing to duplicate");
    }
    if (command == "delete")
    {
        return m_document->DeleteSelection() || fail("nothing to delete");
    }
    if (command == "undo")
    {
        return m_document->Undo() || fail("nothing to undo");
    }
    if (command == "redo")
    {
        return m_document->Redo() || fail("nothing to redo");
    }
    if (command == "prefab")
    {
        const std::string path = rest();
        if (!primary)
        {
            return fail("nothing selected");
        }
        const AssetID id = assets.RegisterSourceAsset(AssetType::Prefab, path);
        return m_document->CreatePrefab(*primary, m_project.ToAbsolute(path), id, &error);
    }
    if (command == "instantiate")
    {
        const AssetID id = assets.RegisterSourceAsset(AssetType::Prefab, rest());
        return m_document->InstantiatePrefab(id) != nullptr || fail("could not instantiate the prefab");
    }
    if (command == "unpack")
    {
        return (primary && m_document->UnpackPrefab(*primary)) || fail("the selection is not a prefab instance");
    }
    if (command == "play")
    {
        return m_document->Play() || fail("already playing");
    }
    if (command == "stop")
    {
        return m_document->Stop() || fail("not playing");
    }
    if (command == "mode")
    {
        const std::string mode = rest();
        m_gizmo.mode = mode == "rotate" ? GizmoMode::Rotate : mode == "scale" ? GizmoMode::Scale : GizmoMode::Translate;
        return true;
    }
    if (command == "focus")
    {
        FocusSelection();
        return true;
    }
    if (command == "camera")
    {
        glm::vec3 position(0.0f);
        float yaw = 0.0f, pitch = 0.0f;
        stream >> position.x >> position.y >> position.z >> yaw >> pitch;
        m_camera.SetPosition(position);
        m_camera.SetAngles(yaw, pitch);
        return true;
    }
    if (command == "reload")
    {
        // Picks up files changed on disk now instead of at the next poll.
        ReloadReport report;
        if (m_project.UpdateNow(report))
        {
            for (AssetID prefab : report.prefabs)
            {
                m_document->RefreshPrefabInstances(prefab);
            }
        }
        return !report.failed || fail("the reload reported errors");
    }
    if (command == "wait")
    {
        stream >> m_scriptWait;
        return true;
    }
    if (command == "screenshot")
    {
        Engine::GetInstance().RequestScreenshot(rest());
        m_scriptWait = 2;
        return true;
    }
    if (command == "expect")
    {
        std::string what;
        stream >> what;
        if (what == "objects" || what == "selected" || what == "dirty" || what == "children" || what == "components")
        {
            long expected = 0;
            stream >> expected;
            long actual = 0;
            if (what == "objects")
            {
                // Everything in the scene, prefab contents included.
                std::vector<GameObject *> stack;
                for (const auto &root : m_scene->GetObjects())
                {
                    stack.push_back(root.get());
                }
                while (!stack.empty())
                {
                    GameObject *object = stack.back();
                    stack.pop_back();
                    ++actual;
                    for (const auto &child : object->GetChildren())
                    {
                        stack.push_back(child.get());
                    }
                }
            }
            else if (what == "selected")
            {
                actual = static_cast<long>(m_document->GetSelection().size());
            }
            else if (what == "dirty")
            {
                actual = m_document->IsDirty() ? 1 : 0;
            }
            else if (!primary)
            {
                return fail("nothing selected");
            }
            else if (what == "children")
            {
                actual = static_cast<long>(primary->GetChildren().size());
            }
            else
            {
                actual = static_cast<long>(primary->GetComponents().size());
            }
            return actual == expected ||
                   fail("expected " + std::to_string(expected) + " " + what + ", found " + std::to_string(actual));
        }
        if (what == "position")
        {
            glm::vec3 expected(0.0f);
            stream >> expected.x >> expected.y >> expected.z;
            if (!primary)
            {
                return fail("nothing selected");
            }
            const glm::vec3 actual = primary->GetPosition();
            return glm::length(actual - expected) < 1e-3f ||
                   fail("position is " + std::to_string(actual.x) + " " + std::to_string(actual.y) + " " + std::to_string(actual.z));
        }
        if (what == "cells")
        {
            long expected = 0;
            stream >> expected;
            const long actual = static_cast<long>(m_document->GetRegionCells().size());
            return actual == expected ||
                   fail("expected " + std::to_string(expected) + " cells, found " + std::to_string(actual));
        }
        if (what == "cell")
        {
            // expect cell X Z N: the cell's root has N objects directly under it.
            int x = 0, z = 0;
            long expected = 0;
            stream >> x >> z >> expected;
            for (const auto &cell : m_document->GetRegionCells())
            {
                if (cell.x == x && cell.z == z)
                {
                    GameObject *root = m_scene->FindObjectByID(cell.root);
                    const long actual = root ? static_cast<long>(root->GetChildren().size()) : -1;
                    return actual == expected ||
                           fail(
                               "cell " + std::to_string(x) + "," + std::to_string(z) + " has " +
                               std::to_string(actual) + " objects, expected " + std::to_string(expected)
                           );
                }
            }
            return fail("cell " + std::to_string(x) + "," + std::to_string(z) + " is not loaded");
        }
        if (what == "playing")
        {
            int expected = 0;
            stream >> expected;
            return (m_document->IsPlaying() ? 1 : 0) == expected || fail("play state differs");
        }
        return fail("unknown expectation '" + what + "'");
    }
    if (command == "quit")
    {
        m_scriptQuit = true;
        SetNeedsToBeClosed(true);
        return true;
    }
    return fail("unknown command");
}

} // namespace Geni::Editor
