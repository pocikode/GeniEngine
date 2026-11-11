#pragma once

#include "scene/Reflection.h"

#include <cstddef>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace Geni
{

class GameObject;
class Scene;
class SceneAsset;

// Instantiates a scene document a few nodes at a time, so a large scene can be built
// over several frames instead of stalling one. Objects are created first (pass 1), then
// components (pass 2), so components can refer to any object in the scene.
//
// `document` must outlive the instantiation (keep the SceneAsset handle). A malformed
// document fails cleanly: the objects created so far are removed and GetError says why.
class SceneInstantiation
{
  public:
    enum class Status
    {
        InProgress,
        Done,
        Failed
    };

    // `clear` false appends the scene's nodes to whatever the target already contains;
    // used when a cooked model is loaded into a running scene.
    SceneInstantiation(
        Scene &scene, const nlohmann::json &document, const SerializationContext &context, bool clear = true
    );

    // Creates up to `maxNodes` objects or component lists, then returns.
    Status Step(size_t maxNodes);
    Status GetStatus() const
    {
        return m_status;
    }
    const std::string &GetError() const
    {
        return m_error;
    }
    size_t GetNodeCount() const
    {
        return m_objects.size();
    }
    // 0 to 1: half for creating objects, half for their components.
    float GetProgress() const;

  private:
    void Begin();
    void CreateObjects(size_t &budget);
    void CreateComponents(size_t &budget);
    void Finish();
    void Abort(const std::string &message);

    enum class Phase
    {
        Validate,
        Objects,
        Components,
        Finished
    };

    Scene &m_scene;
    const nlohmann::json &m_document;
    SerializationContext m_context;
    bool m_clear = true;
    Phase m_phase = Phase::Validate;
    Status m_status = Status::InProgress;
    std::string m_error;
    const nlohmann::json *m_nodes = nullptr;
    std::vector<GameObject *> m_objects;
    size_t m_cursor = 0;
    bool m_preserveIds = false;
    // Parent for the document's root nodes (InstantiateSubtree); nullptr: scene roots.
    GameObject *m_attachParent = nullptr;

    friend class SceneSerializer;
};

// Text scene format (docs/design/0005): a flat node array with parent indices and
// reflected component properties. The cooker writes the same schema, so a cooked scene
// and a scene saved from code are the same kind of file.
class SceneSerializer
{
  public:
    static bool Save(const Scene &scene, const std::filesystem::path &path, std::string *error = nullptr);
    static nlohmann::ordered_json SaveToJson(const Scene &scene);
    // The file text: one node per line, so adding or editing an object is a one-line diff.
    static std::string SaveToText(const Scene &scene);

    // Instantiates the whole document now. For large scenes, use SceneInstantiation and
    // step it once per frame. `clear` as for SceneInstantiation.
    static bool Load(
        Scene &scene, const nlohmann::json &document, SerializationContext &context, std::string *error = nullptr,
        bool clear = true
    );
    static bool LoadFromFile(
        Scene &scene, const std::filesystem::path &path, SerializationContext &context, std::string *error = nullptr
    );
    static bool Instantiate(
        Scene &scene, const SceneAsset &asset, SerializationContext &context, std::string *error = nullptr
    );

    // Part of a scene in the scene format: each root and everything under it. With
    // `includeIds` every node carries its object ID, which InstantiateSubtree can restore
    // (undo of a delete, play/stop). `format` is "geni.scene" or "geni.prefab".
    static nlohmann::ordered_json SaveSubtree(
        const std::vector<GameObject *> &roots, bool includeIds = false, const char *format = "geni.scene"
    );
    // Adds a document's nodes under `parent` (nullptr: as scene roots) without touching
    // the rest of the scene. `roots` receives the objects created for the document's
    // root nodes. On failure nothing is left behind.
    static bool InstantiateSubtree(
        Scene &scene, const nlohmann::json &document, SerializationContext &context, GameObject *parent,
        std::vector<GameObject *> *roots = nullptr, std::string *error = nullptr, bool preserveIds = false
    );

    // Prefabs (docs/design/0006). SavePrefab writes `root` and its subtree as a .gprefab.
    static bool SavePrefab(GameObject &root, const std::filesystem::path &path, std::string *error = nullptr);
    // Creates an object named after the prefab's root under `parent` and fills it from
    // the prefab asset. nullptr when the prefab cannot be loaded.
    static GameObject *InstantiatePrefab(
        Scene &scene, AssetID prefab, SerializationContext &context, GameObject *parent = nullptr
    );
    // Rebuilds what an instance got from its prefab (after the prefab file changed).
    static bool RefreshPrefabInstance(GameObject &instance, SerializationContext &context);
    // Turns an instance into plain objects: its contents stay, the link to the prefab goes.
    static void UnpackPrefabInstance(GameObject &instance);
};

} // namespace Geni
