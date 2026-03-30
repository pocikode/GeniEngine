#include "scene/Scene.h"
#include "core/Common.h"
#include "scene/GameObject.h"
#include "scene/components/LightComponent.h"
#include "scene/components/AudioComponent.h"
#include "scene/components/MeshComponent.h"
#include "scene/components/CameraComponent.h"
#include "scene/components/PhysicsComponent.h"
#include "scene/components/AnimationComponent.h"
#include "scene/components/PlayerControllerComponent.h"
#include "core/Engine.h"
#include "io/FileSystem.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstddef>
#include <memory>
#include <vector>

namespace Geni
{

void Scene::Update(float deltaTime)
{
    for (auto it = m_objects.begin(); it != m_objects.end();)
    {
        if ((*it)->IsAlive())
        {
            (*it)->Update(deltaTime);
            ++it;
        }
        else
        {
            it = m_objects.erase(it);
        }
    }
}

void Scene::Clear()
{
    m_objects.clear();
}

GameObject *Scene::CreateObject(const std::string &name, GameObject *parent)
{
    auto obj = new GameObject();
    obj->SetName(name);
    obj->m_scene = this;
    SetParent(obj, parent);

    return obj;
}

void Scene::AddObject(GameObject *obj, GameObject *parent)
{
    obj->m_scene = this;
    SetParent(obj, parent);
}

bool Scene::SetParent(GameObject *obj, GameObject *parent)
{
    bool result = false;
    auto currentParent = obj->GetParent();

    if (parent == nullptr)
    {
        if (currentParent != nullptr)
        {
            auto it = std::find_if(
                currentParent->m_children.begin(), currentParent->m_children.end(),
                [obj](const std::unique_ptr<GameObject> &el) { return el.get() == obj; }
            );

            if (it != currentParent->m_children.end())
            {
                m_objects.push_back(std::move(*it));
                obj->m_parent = nullptr;
                currentParent->m_children.erase(it);
                result = true;
            }
        }
        // No parent currently
        // 1. The object is in the scene root
        // 2. The object has been just created
        else
        {
            auto it = std::find_if(m_objects.begin(), m_objects.end(), [obj](const std::unique_ptr<GameObject> &el) {
                return el.get() == obj;
            });

            if (it == m_objects.end())
            {
                std::unique_ptr<GameObject> objHolder(obj);
                m_objects.push_back(std::move(objHolder));
                result = true;
            }
        }
    }
    // Add it as child of another object
    else
    {
        if (currentParent != nullptr)
        {
            auto it = std::find_if(
                currentParent->m_children.begin(), currentParent->m_children.end(),
                [obj](const std::unique_ptr<GameObject> &el) { return el.get() == obj; }
            );
            if (it != currentParent->m_children.end())
            {
                bool found = false;
                auto currentElement = parent;
                while (currentElement)
                {
                    if (currentElement == obj)
                    {
                        found = true;
                        break;
                    }
                    currentElement = currentElement->GetParent();
                }
                if (!found)
                {
                    parent->m_children.push_back(std::move(*it));
                    obj->m_parent = parent;
                    currentParent->m_children.erase(it);
                    result = false;
                }
            }
        }
        // No parent currently
        // 1. The object is in the scene root
        // 2. The object has been just created
        else
        {
            auto it = std::find_if(m_objects.begin(), m_objects.end(), [obj](const std::unique_ptr<GameObject> &el) {
                return el.get() == obj;
            });

            // The object has been just created
            if (it == m_objects.end())
            {
                std::unique_ptr<GameObject> objHolder(obj);
                parent->m_children.push_back(std::move(objHolder));
                obj->m_parent = parent;
                result = true;
            }
            else
            {
                bool found = false;
                auto currentElement = parent;
                while (currentElement)
                {
                    if (currentElement == obj)
                    {
                        found = true;
                        break;
                    }
                    currentElement = currentElement->GetParent();
                }

                if (!found)
                {
                    parent->m_children.push_back(std::move(*it));
                    obj->m_parent = parent;
                    m_objects.erase(it);
                    result = true;
                }
            }
        }
    }

    return result;
}

void Scene::SetMainCamera(GameObject *camera)
{
    m_mainCamera = camera;
}

GameObject *Scene::GetMainCamera()
{
    return m_mainCamera;
}

std::vector<LightData> Scene::CollectLights()
{
    std::vector<LightData> lights;
    for (auto &obj : m_objects)
    {
        CollectLightsRecursive(obj.get(), lights);
    }

    return lights;
}

void Scene::CollectLightsRecursive(GameObject *obj, std::vector<LightData> &out)
{
    if (auto light = obj->GetComponent<LightComponent>())
    {
        LightData data;
        data.color = light->GetColor();
        data.position = obj->GetWorldPosition();
        out.push_back(data);
    }

    for (auto &child : obj->m_children)
    {
        CollectLightsRecursive(child.get(), out);
    }
}

static void LoadGameObjectRecursive(Scene *scene, GameObject *parent, const nlohmann::json &j)
{
    std::string name = j.value("name", "GameObject");
    GameObject *obj = nullptr;

    // Check if it's a prefab/gltf
    if (j.contains("prefab"))
    {
        obj = GameObject::LoadGLTF(j["prefab"].get<std::string>());
        if (obj)
        {
            obj->SetName(name);
            scene->AddObject(obj, parent);
        }
    }
    else
    {
        obj = scene->CreateObject(name, parent);
    }

    if (!obj) return;

    if (j.contains("position"))
    {
        auto pos = j["position"];
        obj->SetPosition(glm::vec3(pos[0], pos[1], pos[2]));
    }
    if (j.contains("rotation"))
    {
        auto rot = j["rotation"];
        obj->SetRotation(glm::quat(rot[3], rot[0], rot[1], rot[2])); // w, x, y, z usually in glm, but from JSON might be x,y,z,w. Assuming x,y,z,w here. Let's use glm::quat(w,x,y,z)
    }
    if (j.contains("scale"))
    {
        auto scale = j["scale"];
        obj->SetScale(glm::vec3(scale[0], scale[1], scale[2]));
    }

    if (j.contains("active"))
    {
        obj->SetActive(j["active"].get<bool>());
    }

    if (j.contains("components"))
    {
        for (const auto &compJson : j["components"])
        {
            std::string type = compJson["type"].get<std::string>();
            if (type == "CameraComponent")
            {
                auto *cam = new CameraComponent();
                obj->AddComponent(cam);
                if (compJson.contains("fov")) cam->SetFov(compJson["fov"].get<float>());
                if (compJson.contains("near")) cam->SetNear(compJson["near"].get<float>());
                if (compJson.contains("far")) cam->SetFar(compJson["far"].get<float>());
                if (compJson.value("main", false))
                {
                    scene->SetMainCamera(obj);
                }
            }
            else if (type == "LightComponent")
            {
                auto *light = new LightComponent();
                obj->AddComponent(light);
                if (compJson.contains("color"))
                {
                    auto color = compJson["color"];
                    light->SetColor(glm::vec3(color[0], color[1], color[2]));
                }
            }
            else if (type == "AudioComponent")
            {
                auto *audio = new AudioComponent();
                obj->AddComponent(audio);
                if (compJson.contains("path"))
                {
                    audio->Load(compJson["path"].get<std::string>());
                }
                if (compJson.contains("looping"))
                {
                    audio->SetLooping(compJson["looping"].get<bool>());
                }
                if (compJson.value("playOnAwake", false))
                {
                    audio->Play();
                }
            }
        }
    }

    if (j.contains("children"))
    {
        for (const auto &childJson : j["children"])
        {
            LoadGameObjectRecursive(scene, obj, childJson);
        }
    }
}

Scene *Scene::Load(const std::string &path)
{
    auto &fs = Engine::GetInstance().GetFileSystem();
    std::string text = fs.LoadAssetFileText(path);
    if (text.empty())
    {
        return nullptr;
    }

    nlohmann::json j;
    try
    {
        j = nlohmann::json::parse(text);
    }
    catch (...)
    {
        return nullptr;
    }

    Scene *scene = new Scene();

    try
    {
        if (j.contains("objects"))
        {
            for (const auto &objJson : j["objects"])
            {
                LoadGameObjectRecursive(scene, nullptr, objJson);
            }
        }
    }
    catch (...)
    {
        delete scene;
        return nullptr;
    }

    return scene;
}

} // namespace Geni