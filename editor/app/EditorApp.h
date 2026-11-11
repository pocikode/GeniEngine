#pragma once

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
    ProjectConfig project;
    // Asset path of the scene (or prefab) to open at startup. Empty: the first .gscene
    // in the project, or a new scene.
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
        Quit
    };

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
    void ActivateAsset(AssetID id, const glm::vec3 *position = nullptr);

    // --- Viewport (EditorViewport.cpp) ---
    void UpdateViewport(float deltaTime);
    void DrawOverlays();
    void DrawGizmo(ImDrawList &draw, const CameraData &camera);
    bool GetGizmoFrame(GizmoFrame &frame);
    void FocusSelection();
    Ray MouseRay(const CameraData &camera) const;

    // --- Actions ---
    void RequestAction(PendingAction action, const std::string &path = std::string());
    void PerformAction(PendingAction action, const std::string &path);
    bool OpenAssetPath(const std::string &assetPath);
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
    bool m_cameraDragging = false;
    bool m_showGrid = true;
    bool m_showMarkers = true;
    bool m_layoutBuilt = false;

    // Hierarchy.
    std::unordered_set<uint64_t> m_expanded;
    char m_hierarchyFilter[128] = "";
    bool m_scrollToSelection = false;
    uint64_t m_lastPrimary = 0;

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
    int m_assetTypeFilter = 0;

    // Dialogs.
    PendingAction m_pendingAction = PendingAction::None;
    std::string m_pendingPath;
    bool m_openConfirm = false;
    bool m_openSaveAs = false;
    bool m_openOpen = false;
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
