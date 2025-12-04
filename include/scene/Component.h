#pragma once

namespace Orbis
{

class GameObject;

class Component
{
  public:
    virtual ~Component() = default;
    virtual void Update(float deltaTime) = 0;

    GameObject *GetOwner();

  protected:
    GameObject *m_owner;

    friend class GameObject;
};

} // namespace Orbis