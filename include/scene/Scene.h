#pragma once
#include "scene/GameObject.h"
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

namespace Orbis
{

class GameObject;

class Scene
{
  public:
    void Update(float deltaTime);
    void Clear();
    bool SetParent(GameObject *obj, GameObject *parent);

    GameObject *CreateObject(const std::string &name, GameObject *parent = nullptr);

    template <typename T, typename = typename std::enable_if_t<std::is_base_of_v<GameObject, T>>>
    T *CreateObject(const std::string &name, GameObject *parent = nullptr)
    {
        auto obj = new GameObject();
        obj->SetName(name);
        SetParent(obj, parent);

        return obj;
    }

  private:
    std::vector<std::unique_ptr<GameObject>> m_objects;
};

} // namespace Orbis