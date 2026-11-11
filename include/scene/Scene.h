#pragma once

#include "core/Common.h"
#include "scene/ComponentPool.h"
#include "scene/GameObject.h"
#include <memory>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Geni
{

class GameObject;

// Play runs every component. Edit is what an editor shows: objects are drawn
// (Component::EditUpdate) but nothing simulates, animates or reads input.
enum class SceneUpdateMode
{
    Play,
    Edit
};

class Scene
{
  public:
    ~Scene();
    void Update(float deltaTime, SceneUpdateMode mode = SceneUpdateMode::Play);
    void Clear();
    bool SetParent(GameObject *obj, GameObject *parent);
    // SetParent, then moves the object to `index` among its new siblings (clamped; a
    // negative index means last). False when the move would create a cycle.
    bool MoveObject(GameObject *obj, GameObject *parent, int index);
    // Destroys the object and its subtree now. Unlike MarkForDestroy this must not be
    // called from inside Update (from a component); tools call it between frames.
    void DestroyObject(GameObject *obj);

    // nullptr when no live object has the ID.
    GameObject *FindObjectByID(uint64_t id) const;
    // Gives an object a specific ID: used when undo restores a deleted object. False
    // when another object already has it.
    bool SetObjectID(GameObject *obj, uint64_t id);

    GameObject *CreateObject(const std::string &name, GameObject *parent = nullptr);
    // Finds an object by name anywhere in the scene (roots and their subtrees).
    GameObject *FindObject(const std::string &name);
    const std::vector<std::unique_ptr<GameObject>> &GetObjects() const
    {
        return m_objects;
    }

    template <typename T, typename = typename std::enable_if_t<std::is_base_of_v<GameObject, T>>>
    T *CreateObject(const std::string &name, GameObject *parent = nullptr)
    {
        auto obj = new T();
        obj->SetName(name);
        AdoptObject(obj);
        SetParent(obj, parent);

        return obj;
    }

    void SetMainCamera(GameObject *camera);
    GameObject *GetMainCamera();
    const GameObject *GetMainCamera() const
    {
        return m_mainCamera;
    }

    std::vector<LightData> CollectLights();

    // Scene-wide component iteration. This is the system-facing API: it visits a dense
    // per-type array instead of walking the tree.
    template <typename T, typename Fn> void ForEachComponent(Fn &&fn)
    {
        auto it = m_componentPools.find(Component::StaticTypeID<T>());
        if (it != m_componentPools.end())
        {
            it->second->template ForEach<T>(std::forward<Fn>(fn));
        }
    }

    template <typename T> size_t CountComponents()
    {
        auto it = m_componentPools.find(Component::StaticTypeID<T>());
        return it == m_componentPools.end() ? 0 : it->second->Count();
    }

  private:
    // Advances every animation clock, samples the poses in parallel on the job system and
    // applies them, before objects update (so skinned meshes see this frame's pose).
    void UpdateAnimation(float deltaTime);

    // Called by GameObject when a component is added or the object is destroyed.
    void RegisterComponent(Component *component);
    void UnregisterComponent(Component *component);
    // Points a new object at this scene and gives it an ID.
    void AdoptObject(GameObject *obj);
    void UnregisterObject(GameObject *obj);

  private:
    std::vector<std::unique_ptr<GameObject>> m_objects;
    std::unordered_map<size_t, std::unique_ptr<ComponentPool>> m_componentPools;
    std::unordered_map<uint64_t, GameObject *> m_objectsByID;
    uint64_t m_nextObjectID = 1;
    GameObject *m_mainCamera = nullptr;

    friend class GameObject;
};

} // namespace Geni