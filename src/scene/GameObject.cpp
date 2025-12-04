#include "scene/GameObject.h"
#include "glm/ext/matrix_float4x4.hpp"
#include "glm/ext/matrix_transform.hpp"
#include "glm/ext/vector_float3.hpp"

namespace Orbis
{

void GameObject::Update(float deltaTime)
{
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

bool GameObject::IsAlive() const
{
    return m_isAlive;
}

void GameObject::MarkForDestroy()
{
    m_isAlive = false;
}

void GameObject::AddComponent(Component *component)
{
    m_components.emplace_back(component);
    component->m_owner = this;
}

glm::vec3 GameObject::GetPosition() const
{
    return m_position;
}

void GameObject::SetPosition(glm::vec3 &pos)
{
    m_position = pos;
}

glm::vec3 GameObject::GetRotation() const
{
    return m_rotation;
}

void GameObject::SetRotation(glm::vec3 &rot)
{
    m_rotation = rot;
}

glm::vec3 GameObject::GetScale() const
{
    return m_scale;
}

void GameObject::SetScale(glm::vec3 &scale)
{
    m_scale = scale;
}

glm::mat4 GameObject::GetLocalTransform() const
{
    glm::mat4 mat(1.0f);

    // translation
    mat = glm::translate(mat, m_position);

    // rotation
    mat = glm::rotate(mat, m_rotation.x, glm::vec3(1.0f, 0.0f, 0.0f)); // x-axis
    mat = glm::rotate(mat, m_rotation.y, glm::vec3(0.0f, 1.0f, 0.0f)); // y-axis
    mat = glm::rotate(mat, m_rotation.z, glm::vec3(0.0f, 0.0f, 1.0f)); // z-axis

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

} // namespace Orbis