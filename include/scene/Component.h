#pragma once

#include <cstddef>
#include <glm/vec3.hpp>
namespace Geni
{

class GameObject;
struct SerializationContext;

class Component
{
  public:
    virtual ~Component() = default;
    virtual void Init();
    virtual void Update(float deltaTime) = 0;
    // Called in every fixed simulation step, before physics steps (Scene::FixedUpdate,
    // docs/design/0010), for types whose HasFixedUpdate returns true.
    virtual void FixedUpdate(float fixedDeltaTime)
    {
        (void)fixedDeltaTime;
    }
    virtual bool HasFixedUpdate() const
    {
        return false;
    }
    // Called after every object's Update in Scene::Update (play mode), for types whose
    // HasLateUpdate returns true: cameras that follow what moved this frame.
    virtual void LateUpdate(float deltaTime)
    {
        (void)deltaTime;
    }
    virtual bool HasLateUpdate() const
    {
        return false;
    }
    // Called before the component is destroyed while its object is still in the scene
    // (GameObject destructor, RemoveComponent).
    virtual void OnDestroy()
    {
    }
    // Called instead of Update while a scene is edited rather than played
    // (SceneUpdateMode::Edit). Only components that draw do anything here.
    virtual void EditUpdate()
    {
    }
    virtual size_t GetTypeID() const = 0;
    // Stable name used by the scene file and the type registry.
    virtual const char *GetTypeName() const = 0;

    // Called after SceneSerializer has set the reflected properties. Components that
    // reference assets turn their IDs into handles here; the loads may still be pending.
    virtual void ResolveAssets(SerializationContext &context)
    {
        (void)context;
    }

    // The scene's origin moved by `offset` (Scene::ShiftOrigin, docs/design/0007): every
    // root object has already been moved by -offset. A component that stores a position
    // in scene space outside its object's transform (a camera target) moves it too.
    virtual void OnOriginShift(const glm::vec3 &offset)
    {
        (void)offset;
    }

    GameObject *GetOwner();

    template <typename T> static size_t StaticTypeID()
    {
        static size_t typeID = nextID++;
        return typeID;
    }

  protected:
    GameObject *m_owner = nullptr;

    friend class GameObject;
    friend class ComponentPool;

  private:
    static constexpr size_t NotInPool = static_cast<size_t>(-1);
    // Position in the scene's ComponentPool for this type; maintained by the pool.
    size_t m_poolIndex = NotInPool;

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
    }                                                                                                                  \
                                                                                                                       \
    const char *GetTypeName() const override                                                                           \
    {                                                                                                                  \
        return #ComponentClass;                                                                                        \
    }

} // namespace Geni