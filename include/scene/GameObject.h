#pragma once
#include "glm/ext/vector_float3.hpp"
#include "scene/Component.h"
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <memory>
#include <string>
#include <vector>

namespace Orbis
{

class GameObject
{
  public:
    virtual ~GameObject() = default;
    virtual void Update(float deltaTime);
    const std::string &GetName() const;
    void SetName(const std::string &name);
    GameObject *GetParent();
    bool IsAlive() const;
    void MarkForDestroy();

    void AddComponent(Component *component);

    glm::vec3 GetPosition() const;
    void SetPosition(glm::vec3 &pos);

    glm::vec3 GetRotation() const;
    void SetRotation(glm::vec3 &rot);

    glm::vec3 GetScale() const;
    void SetScale(glm::vec3 &scale);

    glm::mat4 GetLocalTransform() const;
    glm::mat4 GetWorldTransform() const;

  protected:
    GameObject() = default;

  private:
    std::string m_name;
    GameObject *m_parent = nullptr;
    std::vector<std::unique_ptr<GameObject>> m_children;
    std::vector<std::unique_ptr<Component>> m_components;
    bool m_isAlive = true;

    glm::vec3 m_position = glm::vec3(0.0f);
    glm::vec3 m_rotation = glm::vec3(0.0f);
    glm::vec3 m_scale = glm::vec3(1.0f);

    friend class Scene;
};

} // namespace Orbis