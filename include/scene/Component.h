#pragma once

#include <cstddef>
namespace Geni
{

class GameObject;

class Component
{
  public:
    virtual ~Component() = default;
    virtual void Init();
    virtual void Update(float deltaTime) = 0;
    virtual size_t GetTypeID() const = 0;

    GameObject *GetOwner();

    template <typename T> static size_t StaticTypeID()
    {
        static size_t typeID = nextID++;
        return typeID;
    }

  protected:
    GameObject *m_owner;

    friend class GameObject;

  private:
    static size_t nextID;
};

#define COMPONENT(ComponentClass)                                                                                      \
  public:                                                                                                              \
    static size_t TypeID()                                                                                             \
    {                                                                                                                  \
        return Component::StaticTypeID<ComponentClass>();                                                              \
    }                                                                                                                  \
                                                                                                                       \
    size_t GetTypeID() const override                                                                                  \
    {                                                                                                                  \
        return TypeID();                                                                                               \
    }

} // namespace Geni