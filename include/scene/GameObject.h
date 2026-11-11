#pragma once
#include "glm/ext/quaternion_float.hpp"
#include "glm/ext/vector_float3.hpp"
#include "assets/AssetID.h"
#include "scene/Component.h"
#include <cstdint>
#include <cstddef>
#include <glm/gtc/quaternion.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <memory>
#include <string>
#include <vector>

namespace Geni
{

class Scene;

class GameObject
{
  public:
    virtual ~GameObject();
    virtual void Update(float deltaTime);
    // Edit-mode traversal: Component::EditUpdate instead of Update (Scene::Update with
    // SceneUpdateMode::Edit).
    void UpdateEditMode();
    // Unique within the scene for the life of the object; 0 before it joins a scene.
    // Not written to scene files: selection and undo use it to name an object.
    uint64_t GetID() const
    {
        return m_id;
    }
    const std::string &GetName() const;
    void SetName(const std::string &name);
    GameObject *GetParent();
    bool SetParent(GameObject *parent);
    Scene *GetScene();
    void SetActive(bool active);
    bool IsActive() const;
    bool IsAlive() const;
    void MarkForDestroy();

    // Takes ownership and runs Init. `index` places the component in the list (undo puts
    // a removed component back where it was); negative or past the end appends.
    void AddComponent(Component *component, int index = -1);
    // Destroys the component now. False when it does not belong to this object.
    bool RemoveComponent(Component *component);

    // Prefab instances (docs/design/0006). An instance root names the prefab asset it was
    // made from; the components and children the prefab gave it are flagged FromPrefab
    // and are not written when the scene is saved.
    AssetID GetPrefab() const
    {
        return m_prefab;
    }
    void SetPrefab(AssetID prefab)
    {
        m_prefab = prefab;
    }
    bool IsPrefabInstance() const
    {
        return m_prefab.IsValid();
    }
    bool IsFromPrefab() const
    {
        return m_fromPrefab;
    }
    void SetFromPrefab(bool fromPrefab)
    {
        m_fromPrefab = fromPrefab;
    }
    // Position among its siblings (or among the scene's roots).
    int GetSiblingIndex() const;
    template <typename T, typename = typename std::enable_if_t<std::is_base_of_v<Component, T>>> T *GetComponent()
    {
        size_t typeID = Component::StaticTypeID<T>();

        for (auto &component : m_components)
        {
            if (component->GetTypeID() == typeID)
            {
                return static_cast<T *>(component.get());
            }
        }

        return nullptr;
    }

    GameObject *FindChildByName(const std::string &name);
    const std::vector<std::unique_ptr<GameObject>> &GetChildren() const;
    const std::vector<std::unique_ptr<Component>> &GetComponents() const
    {
        return m_components;
    }

    glm::vec3 GetPosition() const;
    void SetPosition(const glm::vec3 &pos);

    glm::quat GetRotation() const;
    void SetRotation(const glm::quat &rot);

    glm::vec3 GetScale() const;
    void SetScale(const glm::vec3 &scale);

    glm::mat4 GetLocalTransform() const;
    glm::mat4 GetWorldTransform() const;
    glm::vec3 GetWorldPosition() const;
    // Rotation part of the world transform. A transform with scale gives an
    // approximate rotation; lights and cameras are normally unscaled.
    glm::quat GetWorldRotation() const;

  protected:
    GameObject() = default;

  private:
    std::string m_name;
    GameObject *m_parent = nullptr;
    Scene *m_scene = nullptr;
    std::vector<std::unique_ptr<GameObject>> m_children;
    std::vector<std::unique_ptr<Component>> m_components;
    bool m_isAlive = true;
    uint64_t m_id = 0;
    AssetID m_prefab;
    bool m_fromPrefab = false;

    glm::vec3 m_position = glm::vec3(0.0f);
    glm::quat m_rotation = glm::quat(1.0, 0.0, 0.0f, 0.0f);
    glm::vec3 m_scale = glm::vec3(1.0f);
    bool m_active = true;

    friend class Scene;
};

} // namespace Geni