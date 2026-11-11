#include "scene/Scene.h"
#include "core/Common.h"
#include "scene/GameObject.h"
#include "core/Engine.h"
#include "core/JobSystem.h"
#include "scene/components/AnimationComponent.h"
#include "scene/components/AnimatorComponent.h"
#include "scene/components/LightComponent.h"
#include <algorithm>
#include <cstddef>
#include <memory>
#include <vector>

namespace Geni
{

Scene::~Scene()
{
    // GameObjects unregister their components in their destructor, so objects must go
    // before the component pools; member destruction would do the opposite.
    Clear();
}

namespace
{
// True when the object and all its ancestors are active and not marked for destruction,
// which is when GameObject::Update would have reached its components.
bool IsUpdatedThisFrame(GameObject *object)
{
    for (GameObject *node = object; node; node = node->GetParent())
    {
        if (!node->IsActive() || !node->IsAlive())
        {
            return false;
        }
    }
    return object != nullptr;
}
} // namespace

void Scene::UpdateAnimation(float deltaTime)
{
    // AnimationComponent (one clip) and AnimatorComponent (a graph, docs/design/0011) run
    // the same three steps: advance clocks on the main thread, sample every pose in
    // parallel on the job system, then apply the poses on the main thread.
    std::vector<AnimationComponent *> clips;
    std::vector<AnimatorComponent *> animators;
    auto clipPool = m_componentPools.find(Component::StaticTypeID<AnimationComponent>());
    if (clipPool != m_componentPools.end())
    {
        clipPool->second->ForEach<AnimationComponent>([&](AnimationComponent &animation) {
            if (IsUpdatedThisFrame(animation.GetOwner()) && animation.AdvanceClock(deltaTime))
            {
                clips.push_back(&animation);
            }
        });
    }
    auto animatorPool = m_componentPools.find(Component::StaticTypeID<AnimatorComponent>());
    if (animatorPool != m_componentPools.end())
    {
        animatorPool->second->ForEach<AnimatorComponent>([&](AnimatorComponent &animator) {
            if (IsUpdatedThisFrame(animator.GetOwner()) && animator.Advance(deltaTime))
            {
                animators.push_back(&animator);
            }
        });
    }
    const size_t total = clips.size() + animators.size();
    if (total == 0)
    {
        return;
    }

    auto sample = [&](size_t i) {
        if (i < clips.size())
        {
            clips[i]->SamplePose();
        }
        else
        {
            animators[i - clips.size()]->SamplePose();
        }
    };
    JobSystem *jobs = Engine::GetInstance().GetJobSystem();
    if (jobs && total > 1)
    {
        jobs->ParallelFor(total, 1, [&sample](size_t begin, size_t end) {
            for (size_t i = begin; i < end; ++i)
            {
                sample(i);
            }
        });
    }
    else
    {
        for (size_t i = 0; i < total; ++i)
        {
            sample(i);
        }
    }

    for (AnimationComponent *animation : clips)
    {
        animation->ApplyPose();
    }
    for (AnimatorComponent *animator : animators)
    {
        animator->ApplyPose();
    }
}

void Scene::Update(float deltaTime, SceneUpdateMode mode)
{
    const bool edit = mode == SceneUpdateMode::Edit;
    if (!edit)
    {
        UpdateAnimation(deltaTime);
    }

    for (auto it = m_objects.begin(); it != m_objects.end();)
    {
        if ((*it)->IsAlive())
        {
            if (edit)
            {
                (*it)->UpdateEditMode();
            }
            else
            {
                (*it)->Update(deltaTime);
            }
            ++it;
        }
        else
        {
            it = m_objects.erase(it);
        }
    }

    if (!edit)
    {
        std::vector<Component *> late;
        for (auto &[typeID, pool] : m_componentPools)
        {
            (void)typeID;
            const auto &list = pool->GetComponents();
            if (list.empty() || !list.front()->HasLateUpdate())
            {
                continue;
            }
            for (Component *component : list)
            {
                if (IsUpdatedThisFrame(component->GetOwner()))
                {
                    late.push_back(component);
                }
            }
        }
        for (Component *component : late)
        {
            component->LateUpdate(deltaTime);
        }
    }
}

void Scene::FixedUpdate(float fixedDeltaTime)
{
    // Collect first: a script may add or destroy objects while this runs.
    std::vector<Component *> components;
    for (auto &[typeID, pool] : m_componentPools)
    {
        (void)typeID;
        const auto &list = pool->GetComponents();
        if (list.empty() || !list.front()->HasFixedUpdate())
        {
            continue;
        }
        for (Component *component : list)
        {
            if (IsUpdatedThisFrame(component->GetOwner()))
            {
                components.push_back(component);
            }
        }
    }
    // Pools are in a hash map; keep the order stable from step to step and run to run.
    std::sort(components.begin(), components.end(), [](Component *a, Component *b) {
        const uint64_t ida = a->GetOwner()->GetID();
        const uint64_t idb = b->GetOwner()->GetID();
        return ida != idb ? ida < idb : a->GetTypeID() < b->GetTypeID();
    });
    for (Component *component : components)
    {
        component->FixedUpdate(fixedDeltaTime);
    }
}

void Scene::ShiftOrigin(const glm::dvec3 &newOrigin)
{
    const glm::vec3 offset(newOrigin - m_origin);
    m_origin = newOrigin;
    ++m_originShifts;
    if (offset == glm::vec3(0.0f))
    {
        return;
    }
    for (auto &object : m_objects)
    {
        object->SetPosition(object->GetPosition() - offset);
    }
    for (auto &[typeID, pool] : m_componentPools)
    {
        pool->ForEach<Component>([&](Component &component) { component.OnOriginShift(offset); });
    }
}

void Scene::Clear()
{
    m_objects.clear();
    for (auto &[typeID, pool] : m_componentPools)
    {
        pool->Clear();
    }
    m_componentPools.clear();
    m_objectsByID.clear();
    m_mainCamera = nullptr;
    m_origin = glm::dvec3(0.0);
}

void Scene::AdoptObject(GameObject *obj)
{
    obj->m_scene = this;
    obj->m_id = m_nextObjectID++;
    m_objectsByID[obj->m_id] = obj;
}

void Scene::UnregisterObject(GameObject *obj)
{
    auto it = m_objectsByID.find(obj->m_id);
    if (it != m_objectsByID.end() && it->second == obj)
    {
        m_objectsByID.erase(it);
    }
    if (m_mainCamera == obj)
    {
        m_mainCamera = nullptr;
    }
}

GameObject *Scene::FindObjectByID(uint64_t id) const
{
    auto it = m_objectsByID.find(id);
    return it == m_objectsByID.end() ? nullptr : it->second;
}

bool Scene::SetObjectID(GameObject *obj, uint64_t id)
{
    if (!obj || obj->m_scene != this || id == 0)
    {
        return false;
    }
    if (obj->m_id == id)
    {
        return true;
    }
    if (m_objectsByID.find(id) != m_objectsByID.end())
    {
        return false;
    }
    m_objectsByID.erase(obj->m_id);
    obj->m_id = id;
    m_objectsByID[id] = obj;
    m_nextObjectID = std::max(m_nextObjectID, id + 1);
    return true;
}

void Scene::DestroyObject(GameObject *obj)
{
    if (!obj || obj->m_scene != this)
    {
        return;
    }
    auto &siblings = obj->m_parent ? obj->m_parent->m_children : m_objects;
    auto it = std::find_if(siblings.begin(), siblings.end(), [obj](const std::unique_ptr<GameObject> &el) {
        return el.get() == obj;
    });
    if (it != siblings.end())
    {
        siblings.erase(it);
    }
}

bool Scene::MoveObject(GameObject *obj, GameObject *parent, int index)
{
    if (!obj || obj->m_scene != this || (parent && parent->m_scene != this))
    {
        return false;
    }
    if (obj->m_parent != parent && !SetParent(obj, parent))
    {
        return false;
    }
    auto &siblings = parent ? parent->m_children : m_objects;
    auto it = std::find_if(siblings.begin(), siblings.end(), [obj](const std::unique_ptr<GameObject> &el) {
        return el.get() == obj;
    });
    if (it == siblings.end())
    {
        return false;
    }
    std::unique_ptr<GameObject> holder = std::move(*it);
    siblings.erase(it);
    const size_t target =
        index < 0 ? siblings.size() : std::min(static_cast<size_t>(index), siblings.size());
    siblings.insert(siblings.begin() + static_cast<std::ptrdiff_t>(target), std::move(holder));
    return true;
}

void Scene::RegisterComponent(Component *component)
{
    if (!component)
    {
        return;
    }
    auto &pool = m_componentPools[component->GetTypeID()];
    if (!pool)
    {
        pool = std::make_unique<ComponentPool>();
    }
    pool->Add(component);
}

void Scene::UnregisterComponent(Component *component)
{
    if (!component)
    {
        return;
    }
    auto it = m_componentPools.find(component->GetTypeID());
    if (it != m_componentPools.end())
    {
        it->second->Remove(component);
    }
}

GameObject *Scene::CreateObject(const std::string &name, GameObject *parent)
{
    auto obj = new GameObject();
    obj->SetName(name);
    AdoptObject(obj);
    SetParent(obj, parent);

    return obj;
}

GameObject *Scene::FindObject(const std::string &name)
{
    for (auto &object : m_objects)
    {
        if (GameObject *found = object->FindChildByName(name))
        {
            return found;
        }
    }
    return nullptr;
}

bool Scene::SetParent(GameObject *obj, GameObject *parent)
{
    bool result = false;
    auto currentParent = obj->GetParent();

    // Already where it should be. Also keeps the move below from touching the vector
    // it is iterating.
    if (parent != nullptr && parent == currentParent)
    {
        return true;
    }

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
                    result = true;
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
    ForEachComponent<LightComponent>([&lights](LightComponent &light) {
        GameObject *owner = light.GetOwner();
        if (!owner)
        {
            return;
        }
        for (GameObject *node = owner; node; node = node->GetParent())
        {
            if (!node->IsActive())
            {
                return;
            }
        }
        LightData data;
        data.type = light.GetType();
        data.color = light.GetColor() * light.GetIntensity();
        data.position = owner->GetWorldPosition();
        data.direction = light.GetDirection();
        data.range = light.GetRange();
        data.innerConeAngle = light.GetInnerConeAngle();
        data.outerConeAngle = light.GetOuterConeAngle();
        lights.push_back(data);
    });

    return lights;
}


} // namespace Geni