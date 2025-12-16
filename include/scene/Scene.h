#pragma once

#include "core/Common.h"
#include "scene/GameObject.h"
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

namespace Geni
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
        auto obj = new T();
        obj->SetName(name);
        SetParent(obj, parent);

        return obj;
    }

    void SetMainCamera(GameObject *camera);
    GameObject *GetMainCamera();

    std::vector<LightData> CollectLights();

  private:
    void CollectLightsRecursive(GameObject *obj, std::vector<LightData> &out);

  private:
    std::vector<std::unique_ptr<GameObject>> m_objects;
    GameObject *m_mainCamera = nullptr;
};

} // namespace Geni