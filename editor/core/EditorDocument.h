#pragma once

#include "CommandStack.h"
#include "EditorCommands.h"

#include "assets/AssetID.h"
#include "core/Common.h"

#include <cstdint>
#include <filesystem>
#include <glm/vec3.hpp>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace Geni
{
class AssetManager;
class GameObject;
class Scene;
} // namespace Geni

namespace Geni::Editor
{

// The scene being edited: its file, selection, undo history and play state. Every edit
// the user can make is a method here that records a command, so the panels, the scripted
// session and the tests all go through the same code (docs/design/0006).
//
// The document does not own the scene; it edits the one it is given.
class EditorDocument
{
  public:
    EditorDocument(Scene &scene, AssetManager *assets);

    Scene &GetScene()
    {
        return m_scene;
    }
    EditContext &GetContext()
    {
        return m_context;
    }

    // --- File -------------------------------------------------------------------------
    void NewScene();
    // Opens a .gscene, or a .gprefab to edit the prefab itself.
    bool Open(const std::filesystem::path &path, std::string *error = nullptr);
    bool Save(std::string *error = nullptr);
    bool SaveAs(const std::filesystem::path &path, std::string *error = nullptr);
    const std::filesystem::path &GetPath() const
    {
        return m_path;
    }
    bool IsPrefabDocument() const
    {
        return m_isPrefab;
    }
    bool IsDirty() const
    {
        return m_commands.IsDirty();
    }

    // --- Selection (object IDs; the last one is the primary) ---------------------------
    const std::vector<uint64_t> &GetSelection() const
    {
        return m_selection;
    }
    void Select(uint64_t id, bool additive = false);
    void ToggleSelected(uint64_t id);
    void ClearSelection();
    bool IsSelected(uint64_t id) const;
    GameObject *GetPrimary();
    // Live selected objects, in selection order.
    std::vector<GameObject *> GetSelectedObjects();
    // The same without objects whose ancestor is also selected: what a move, a delete
    // or a duplicate acts on.
    std::vector<GameObject *> GetSelectedRoots();
    // Drops IDs of objects that no longer exist. Called once per frame.
    void PruneSelection();

    // --- Edits (each records one undo step; none is allowed while playing) -------------
    GameObject *CreateEmpty(const std::string &name = "GameObject", GameObject *parent = nullptr);
    GameObject *CreateLight(LightType type);
    GameObject *CreateCamera();
    GameObject *CreateMeshObject(AssetID mesh, AssetID material, const std::string &name);
    GameObject *InstantiatePrefab(AssetID prefab, GameObject *parent = nullptr);
    // Adds the nodes of another scene document (a cooked model) to this scene.
    bool ImportScene(const nlohmann::json &document, std::string *error = nullptr);

    bool DeleteSelection();
    // Duplicates the selected roots next to the originals and selects the copies.
    std::vector<GameObject *> DuplicateSelection();
    // Copies of the selected roots on a countX by countZ grid (the originals are cell 0,0),
    // `spacing` apart along the world X and Z axes. One undo step.
    std::vector<GameObject *> DuplicateGrid(int countX, int countZ, const glm::vec3 &spacing);

    // Moves objects under `newParent` (nullptr: the root) at `index` (-1: last). With
    // `keepWorldTransform` their local transform is adjusted so they stay where they are.
    bool Reparent(const std::vector<GameObject *> &objects, GameObject *newParent, int index = -1,
                  bool keepWorldTransform = true);

    // Records the change from `before` to each object's current state: the end of a drag
    // that already moved the objects. Entries that did not change are dropped.
    bool CommitNodeStates(const char *name, const std::vector<std::pair<uint64_t, NodeState>> &before);
    // Applies and records a new state for one object.
    bool SetNodeState(GameObject &object, const NodeState &state, const char *name = "Edit object");

    bool SetProperty(GameObject &object, int component, const std::string &property, const nlohmann::json &value);
    // As CommitNodeStates, for a property that was previewed with PropertyCommand::Set.
    bool CommitProperty(uint64_t object, int component, const std::string &property, const nlohmann::json &before);
    bool AddComponent(GameObject &object, const std::string &type);
    bool RemoveComponent(GameObject &object, int index);
    bool SetMainCamera(GameObject *camera);

    // Saves `object` and its subtree as a prefab at `path` and turns the object into an
    // instance of it. `prefab` is the asset ID the file has (AssetManager::RegisterSourceAsset).
    bool CreatePrefab(GameObject &object, const std::filesystem::path &path, AssetID prefab,
                      std::string *error = nullptr);
    bool UnpackPrefab(GameObject &instance);
    // Rebuilds every instance of `prefab` in the scene from the (reloaded) prefab asset.
    // Not an undo step: the prefab file changed, the scene did not.
    int RefreshPrefabInstances(AssetID prefab);

    // True when the object can be edited: it exists, play mode is off and it is not part
    // of a prefab instance's contents.
    bool CanEdit(const GameObject *object) const;

    // --- Undo --------------------------------------------------------------------------
    bool Undo();
    bool Redo();
    CommandStack &GetCommands()
    {
        return m_commands;
    }

    // --- Play in editor ----------------------------------------------------------------
    // Play snapshots the scene and restarts it from the snapshot, so every component
    // starts from the edited state; Stop puts the snapshot back.
    bool Play();
    bool Stop();
    bool IsPlaying() const
    {
        return m_playing;
    }

    // The most recent thing that went wrong or was done, for the status bar.
    const std::string &GetStatus() const
    {
        return m_status;
    }
    void SetStatus(std::string status)
    {
        m_status = std::move(status);
    }

  private:
    void RecordCreated(const char *name, const std::vector<GameObject *> &roots, bool select);
    bool RestoreSnapshot();
    void ResetDocument();

    Scene &m_scene;
    EditContext m_context;
    CommandStack m_commands;
    std::vector<uint64_t> m_selection;
    std::filesystem::path m_path;
    bool m_isPrefab = false;

    bool m_playing = false;
    nlohmann::json m_playSnapshot;
    uint64_t m_playMainCamera = 0;
    std::string m_status;
};

} // namespace Geni::Editor
