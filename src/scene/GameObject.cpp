#include "scene/GameObject.h"
#include "scene/Scene.h"
#include "glm/ext/quaternion_float.hpp"
#include "glm/ext/vector_float3.hpp"
#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>

namespace Geni
{

GameObject::~GameObject()
{
    if (m_scene)
    {
        for (auto &component : m_components)
        {
            m_scene->UnregisterComponent(component.get());
        }
        m_scene->UnregisterObject(this);
    }
}

void GameObject::UpdateEditMode()
{
    if (!m_active)
    {
        return;
    }

    for (auto &component : m_components)
    {
        component->EditUpdate();
    }

    for (auto it = m_children.begin(); it != m_children.end();)
    {
        if ((*it)->IsAlive())
        {
            (*it)->UpdateEditMode();
            ++it;
        }
        else
        {
            it = m_children.erase(it);
        }
    }
}

bool GameObject::RemoveComponent(Component *component)
{
    for (auto it = m_components.begin(); it != m_components.end(); ++it)
    {
        if (it->get() == component)
        {
            if (m_scene)
            {
                m_scene->UnregisterComponent(component);
            }
            m_components.erase(it);
            return true;
        }
    }
    return false;
}

int GameObject::GetSiblingIndex() const
{
    const std::vector<std::unique_ptr<GameObject>> *siblings = nullptr;
    if (m_parent)
    {
        siblings = &m_parent->m_children;
    }
    else if (m_scene)
    {
        siblings = &m_scene->GetObjects();
    }
    if (siblings)
    {
        for (size_t i = 0; i < siblings->size(); ++i)
        {
            if ((*siblings)[i].get() == this)
            {
                return static_cast<int>(i);
            }
        }
    }
    return -1;
}

void GameObject::Update(float deltaTime)
{
    if (!m_active)
    {
        return;
    }

    for (auto &component : m_components)
    {
        component->Update(deltaTime);
    }

    for (auto it = m_children.begin(); it != m_children.end();)
    {
        if ((*it)->IsAlive())
        {
            (*it)->Update(deltaTime);
            ++it;
        }
        else
        {
            it = m_children.erase(it);
        }
    }
}

const std::string &GameObject::GetName() const
{
    return m_name;
}

void GameObject::SetName(const std::string &name)
{
    m_name = name;
}

GameObject *GameObject::GetParent()
{
    return m_parent;
}

bool GameObject::SetParent(GameObject *parent)
{
    if (!m_scene)
    {
        return false;
    }

    return m_scene->SetParent(this, parent);
}

Scene *GameObject::GetScene()
{
    return m_scene;
}

void GameObject::SetActive(bool active)
{
    m_active = active;
}

bool GameObject::IsActive() const
{
    return m_active;
}

bool GameObject::IsAlive() const
{
    return m_isAlive;
}

void GameObject::MarkForDestroy()
{
    m_isAlive = false;
}

GameObject *GameObject::FindChildByName(const std::string &name)
{
    if (m_name == name)
    {
        return this;
    }

    for (auto &child : m_children)
    {
        if (auto res = child->FindChildByName(name))
        {
            return res;
        }
    }

    return nullptr;
}

const std::vector<std::unique_ptr<GameObject>> &GameObject::GetChildren() const
{
    return m_children;
}

void GameObject::AddComponent(Component *component, int index)
{
    if (!component)
    {
        return;
    }

    if (index < 0 || static_cast<size_t>(index) >= m_components.size())
    {
        m_components.emplace_back(component);
    }
    else
    {
        m_components.emplace(m_components.begin() + index, component);
    }
    component->m_owner = this;
    if (m_scene)
    {
        m_scene->RegisterComponent(component);
    }
    component->Init();
}

glm::vec3 GameObject::GetPosition() const
{
    return m_position;
}

void GameObject::SetPosition(const glm::vec3 &pos)
{
    m_position = pos;
}

glm::quat GameObject::GetRotation() const
{
    return m_rotation;
}

void GameObject::SetRotation(const glm::quat &rot)
{
    m_rotation = rot;
}

glm::vec3 GameObject::GetScale() const
{
    return m_scale;
}

void GameObject::SetScale(const glm::vec3 &scale)
{
    m_scale = scale;
}

glm::mat4 GameObject::GetLocalTransform() const
{
    glm::mat4 mat(1.0f);

    // translation
    mat = glm::translate(mat, m_position);

    // rotation
    mat = mat * glm::mat4_cast(m_rotation);

    // scale
    mat = glm::scale(mat, m_scale);

    return mat;
}

glm::mat4 GameObject::GetWorldTransform() const
{
    if (m_parent)
    {
        return m_parent->GetWorldTransform() * GetLocalTransform();
    }

    return GetLocalTransform();
}

glm::vec3 GameObject::GetWorldPosition() const
{
    glm::vec4 hom = GetWorldTransform() * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    return glm::vec3(hom) / hom.w;
}

glm::quat GameObject::GetWorldRotation() const
{
    const glm::mat4 world = GetWorldTransform();
    glm::mat3 basis(world);
    // Remove scale column by column.
    for (int column = 0; column < 3; ++column)
    {
        const float length = glm::length(basis[column]);
        if (length > 1e-6f)
        {
            basis[column] /= length;
        }
    }
    return glm::normalize(glm::quat_cast(basis));
}

} // namespace Geni
