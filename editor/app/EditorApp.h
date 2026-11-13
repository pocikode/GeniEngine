#pragma once

#include "scene/Timeline.h"

#include "../core/EditorCamera.h"
#include "../core/EditorDocument.h"
#include "../core/Gizmo.h"
#include "../core/Project.h"

#include <geni.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

struct ImDrawList;

namespace Geni::Editor
{

struct EditorOptions
{
    // Empty assets folder: start on the project manager.
    ProjectConfig project;
    // Roboto-Medium.ttf and friends (ImGui's misc/fonts).
    std::filesystem::path fontFolder;
    // Asset path of the scene (or prefab) to open at startup. Empty: what was open when
    // the project was last closed, else the project's world, else its first scene that is
    // not a world cell, else a new scene.
    std::string scene;
    // A scripted session: one command per frame (see RunScriptCommand). The editor
    // quits after the last one. Used by CI and to do an edit from the command line.
    std::vector<std::string> script;
    bool hotReload = true;
};

// The editor application (docs/design/0006): dock space with hierarchy, inspector,
// assets and console panels around the scene viewport.
class EditorApp : public Application
{
  public:
    explicit EditorApp(EditorOptions options);

    bool Init() override;
    void Update(float deltaTime) override;
    void Destroy() override;

    // Non-zero when a scripted session failed.
    int GetExitCode() const
    {
        return m_exitCode;
    }

  private:
    // What an "are you sure" answer continues with.
    enum class PendingAction
    {
        None,
        NewScene,
        OpenScene,
        OpenWorld,
        OpenProject,
        Quit
    };

    // Startup: pick a project, cook it in the background, then open its content.
    enum class Stage
    {
        ProjectManager,
        Preparing,
        Ready
    };

    struct RecentProject
    {
        std::string path;
        double lastOpened = 0.0; // seconds since the epoch
    };

    // One folder of the assets browser, built from the files on disk.
    struct AssetFile
    {
        std::string path; // asset path, "imported/village/Wall.gltf"
        std::string name;
        AssetID primary;  // the file's own asset (model scene, texture, prefab...), if cooked
        std::vector<AssetID> parts; // sub-assets: meshes, materials, animations
    };
    struct AssetFolder
    {
        std::string path; // "" for the root
        std::string name;
        std::vector<std::unique_ptr<AssetFolder>> folders;
        std::vector<AssetFile> files;
    };

    // --- Project ---
    void OpenProject(const std::filesystem::path &folder);
    void StartProject();
    void FinishStartup();
    bool OpenDefaultContent();
    bool PickDefaultRegion(int region[4]) const;
    void LoadRecentProjects();
    void SaveRecentProjects() const;
    void SaveProjectState() const;
    nlohmann::json LoadProjectState() const;
    void DrawProjectManager();
    void DrawPrepareProgress();
    void DrawFolderBrowser();

    // --- Frame ---
    void DrawDockSpace();
    void DrawMenuBar();
    void DrawToolbar();
    void DrawStatusBar();
    void DrawDialogs();
    void HandleShortcuts();
    void UpdateHotReload();
    void UpdateWindowTitle();

    // --- Panels (EditorPanels.cpp) ---
    void DrawHierarchy();
    void DrawHierarchyRow(GameObject &object, int depth);
    void DrawHierarchyContextMenu(GameObject &object);
    void DrawInspector();
    void DrawTransform(GameObject &object, bool editable);
    void DrawComponent(GameObject &object, int index, bool editable);
    bool DrawProperty(GameObject &object, int index, Component &component, const PropertyDesc &property);
    void DrawAssets();
    void DrawConsole();
    // A world's cell grid: pick a region and load it (docs/design/0007).
    void DrawWorldPanel();
    // The Timeline panel (docs/design/0026); EditorTimeline.cpp.
    void DrawTimelinePanel();
    // The timeline of the selection or its nearest ancestor that has one.
    GameObject *FindTimelineOwner(int &componentIndex);
    // Writes `data` to the timeline's `tracks` through the document (undoable).
    bool CommitTimeline(GameObject &owner, int componentIndex, const TimelineData &data);
    void ActivateAsset(AssetID id, const glm::vec3 *position = nullptr);
    void DrawPropertyLabel(const char *label);
    bool DrawVec3Row(const char *label, glm::vec3 &value, float speed, float resetValue, bool &released);

    // --- Assets browser (EditorAssets.cpp) ---
    void ScanAssets();
    void DrawAssetFolderTree(AssetFolder &folder);
    void DrawAssetTile(const AssetFile &file, float tile);
    void DrawAssetPartTile(AssetID id, float tile);
    AssetFolder *FindAssetFolder(const std::string &path);
    void OpenAssetFile(const AssetFile &file);

    // --- Viewport (EditorViewport.cpp) ---
    void UpdateViewport(float deltaTime);
    void DrawOverlays();
    void DrawGizmo(ImDrawList &draw, const CameraData &camera);
    void DrawViewportToolbar();
    void DrawOrientationGizmo(ImDrawList &draw);
    void FrameScene();
    bool GetGizmoFrame(GizmoFrame &frame);
    void FocusSelection();
    Ray MouseRay(const CameraData &camera) const;

    // --- Actions ---
    void RequestAction(PendingAction action, const std::string &path = std::string());
    void PerformAction(PendingAction action, const std::string &path);
    bool OpenAssetPath(const std::string &assetPath);
    bool OpenWorldAssetPath(const std::string &assetPath);
    bool LoadRegion(int x0, int z0, int x1, int z1);
    bool SaveToAssetPath(const std::string &assetPath);
    void SaveOrAsk();
    void TogglePlay();
    void ApplyPlayState();
    void BeginNodeEdit(const GameObject &object, const NodeState &before);
    void EndNodeEdit();
    void EndPropertyEdit();
    std::string AssetLabel(AssetID id);

    // --- Scripted session ---
    void RunScript();
    bool RunScriptCommand(const std::string &line, std::string &error);

    EditorOptions m_options;
    Stage m_stage = Stage::ProjectManager;
    double m_prepareStarted = 0.0;
    bool m_reimporting = false; // a Reimport All: the open scene stays open
    std::vector<RecentProject> m_recentProjects;
    char m_projectPath[512] = "";
    bool m_openProjectDialog = false;
    std::string m_browsePath;
    Scene *m_scene = nullptr; // owned by the engine
    std::unique_ptr<EditorDocument> m_document;
    Project m_project;
    EditorCamera m_camera;
    Gizmo m_gizmo;
    GizmoTargets m_gizmoTargets;
    GizmoHandle m_hoveredHandle = GizmoHandle::None;
    glm::vec3 m_gizmoPivot{0.0f};

    // Viewport rectangle in ImGui (logical) coordinates.
    glm::vec2 m_viewportPos{0.0f};
    glm::vec2 m_viewportSize{1.0f};
    bool m_viewportHovered = false;
    bool m_orientationHovered = false; // last frame: the mouse was over the axis gizmo
    bool m_cameraDragging = false;
    bool m_showGrid = true;
    bool m_showMarkers = true;
    bool m_previewSun = true;
    bool m_layoutBuilt = false;
    bool m_resetLayout = false;
    // Panels (Window menu).
    bool m_showHierarchy = true;
    bool m_showInspector = true;
    bool m_showAssets = true;
    bool m_showConsole = true;
    bool m_showWorld = true;
    bool m_showTimeline = true;
    // Timeline panel: preview playback and the key being dragged.
    bool m_timelinePreview = false;
    int m_timelineDragTrack = -1;
    int m_timelineDragKey = -1;
    int m_focusHierarchy = 0; // frames left to bring the Hierarchy tab to the front
    int m_selectAssetsTab = 0; // frames left to bring the Assets tab to the front
    // Play: paused, and one frame to step while paused.
    bool m_paused = false;
    bool m_stepFrame = false;

    // Hierarchy.
    std::unordered_set<uint64_t> m_expanded;
    char m_hierarchyFilter[128] = "";
    bool m_scrollToSelection = false;
    uint64_t m_lastPrimary = 0;
    // Renaming a row in place (F2 or double-click on a selected row).
    uint64_t m_renameID = 0;
    bool m_renameFocus = false;
    char m_renameBuffer[256] = "";

    // Inspector: an edit in progress becomes one command when the widget is released.
    bool m_nodeEditActive = false;
    std::pair<uint64_t, NodeState> m_nodeEditBefore;
    bool m_propertyEditActive = false;
    uint64_t m_propertyObject = 0;
    int m_propertyComponent = 0;
    std::string m_propertyName;
    nlohmann::json m_propertyBefore;
    // Euler angles shown for the primary object; kept so they do not jump while typing.
    uint64_t m_eulerObject = 0;
    glm::quat m_eulerRotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 m_eulerDegrees{0.0f};

    // Assets panel.
    char m_assetFilter[128] = "";
    std::unique_ptr<AssetFolder> m_assetRoot;
    std::string m_assetFolder;   // the folder shown on the right
    std::string m_assetSelected; // a file's asset path
    std::unordered_set<std::string> m_assetExpanded; // models showing their sub-assets
    float m_assetTile = 76.0f;
    size_t m_assetManifestSize = static_cast<size_t>(-1);
    double m_assetScanTime = -100.0;

    // Console.
    bool m_consoleInfo = true;
    bool m_consoleWarnings = true;
    bool m_consoleErrors = true;
    size_t m_consoleClearedAt = 0;
    char m_consoleFilter[128] = "";

    // Inspector.
    char m_componentFilter[64] = "";

    // Dialogs.
    PendingAction m_pendingAction = PendingAction::None;
    std::string m_pendingPath;
    bool m_openConfirm = false;
    bool m_openSaveAs = false;
    bool m_openOpen = false;
    bool m_openWorld = false;
    // World panel: the region being picked (cell coordinates, inclusive).
    int m_regionPick[4] = {0, 0, 1, 1};
    bool m_openPrefab = false;
    bool m_openGrid = false;
    char m_pathBuffer[512] = "";
    uint64_t m_prefabObject = 0;
    int m_gridCountX = 5;
    int m_gridCountZ = 5;
    glm::vec3 m_gridSpacing{3.0f, 0.0f, 3.0f};

    // Script.
    size_t m_scriptCursor = 0;
    int m_scriptWait = 0;
    bool m_scriptQuit = false;
    int m_exitCode = 0;
    double m_time = 0.0;
    std::string m_title;
};

} // namespace Geni::Editor
