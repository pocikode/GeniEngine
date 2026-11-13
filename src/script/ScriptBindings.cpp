#include "ScriptBindings.h"

#include "core/Application.h"
#include "core/Engine.h"
#include "core/Log.h"
#include "scene/GameObject.h"
#include "scene/Reflection.h"
#include "scene/Scene.h"
#include "scene/SceneSerializer.h"
#include "scene/components/AnimatorComponent.h"
#include "scene/components/AudioSourceComponent.h"
#include "scene/components/CharacterControllerComponent.h"
#include "scene/components/NavAgentComponent.h"
#include "scene/components/PhysicsComponent.h"
#include "scene/components/ScriptComponent.h"
#include "scene/components/SpriteComponent.h"
#include "scene/components/ThirdPersonCameraComponent.h"
#include "scene/components/TilemapComponent.h"
#include "scene/components/ParticleEmitterComponent.h"
#include "scene/components/WeatherComponent.h"
#include "scene/components/TimelineComponent.h"
#include "scene/components/VehicleComponent.h"
#include "script/ScriptSystem.h"
#include "ui/GameUI.h"

#include "lauxlib.h"
#include "lua.h"

#include <chrono>
#include <cmath>
#include <cstring>
#include <glm/gtc/quaternion.hpp>
#include <string>

namespace Geni
{
namespace ScriptBindings
{

namespace
{

constexpr const char *kObjectMeta = "Geni.Object";
constexpr const char *kComponentMeta = "Geni.Component";

struct ObjectHandle
{
    Scene *scene;
    uint64_t id;
};

struct ComponentHandle
{
    Scene *scene;
    uint64_t id;
    char type[64];
};

const auto s_start = std::chrono::steady_clock::now();

Engine &E()
{
    return Engine::GetInstance();
}

// ---- vectors ---------------------------------------------------------------------

void PushVec3(lua_State *L, double x, double y, double z)
{
    lua_createtable(L, 0, 3);
    lua_pushnumber(L, x);
    lua_setfield(L, -2, "x");
    lua_pushnumber(L, y);
    lua_setfield(L, -2, "y");
    lua_pushnumber(L, z);
    lua_setfield(L, -2, "z");
    lua_getfield(L, LUA_REGISTRYINDEX, "Geni.vec3");
    lua_setmetatable(L, -2);
}

void PushVec3(lua_State *L, const glm::vec3 &v)
{
    PushVec3(L, v.x, v.y, v.z);
}

void PushVec3(lua_State *L, const glm::dvec3 &v)
{
    PushVec3(L, v.x, v.y, v.z);
}

double Field(lua_State *L, int index, const char *name, int position)
{
    lua_getfield(L, index, name);
    if (lua_isnil(L, -1))
    {
        lua_pop(L, 1);
        lua_rawgeti(L, index, position);
    }
    const double value = lua_tonumber(L, -1);
    lua_pop(L, 1);
    return value;
}

glm::dvec3 CheckDVec3(lua_State *L, int index)
{
    index = lua_absindex(L, index);
    luaL_checktype(L, index, LUA_TTABLE);
    return glm::dvec3(Field(L, index, "x", 1), Field(L, index, "y", 2), Field(L, index, "z", 3));
}

glm::vec3 CheckVec3(lua_State *L, int index)
{
    return glm::vec3(CheckDVec3(L, index));
}

glm::vec3 OptVec3(lua_State *L, int index, const glm::vec3 &fallback)
{
    return lua_istable(L, index) ? CheckVec3(L, index) : fallback;
}

void PushQuat(lua_State *L, const glm::quat &q)
{
    lua_createtable(L, 0, 4);
    lua_pushnumber(L, q.x);
    lua_setfield(L, -2, "x");
    lua_pushnumber(L, q.y);
    lua_setfield(L, -2, "y");
    lua_pushnumber(L, q.z);
    lua_setfield(L, -2, "z");
    lua_pushnumber(L, q.w);
    lua_setfield(L, -2, "w");
}

glm::quat CheckQuat(lua_State *L, int index)
{
    index = lua_absindex(L, index);
    luaL_checktype(L, index, LUA_TTABLE);
    return glm::normalize(glm::quat(
        static_cast<float>(Field(L, index, "w", 4)), static_cast<float>(Field(L, index, "x", 1)),
        static_cast<float>(Field(L, index, "y", 2)), static_cast<float>(Field(L, index, "z", 3))
    ));
}

glm::vec4 OptColor(lua_State *L, int index, const glm::vec4 &fallback)
{
    if (!lua_istable(L, index))
    {
        return fallback;
    }
    index = lua_absindex(L, index);
    auto component = [&](const char *name, int position, float otherwise) {
        lua_getfield(L, index, name);
        if (lua_isnil(L, -1))
        {
            lua_pop(L, 1);
            lua_rawgeti(L, index, position);
        }
        const float value = lua_isnumber(L, -1) ? static_cast<float>(lua_tonumber(L, -1)) : otherwise;
        lua_pop(L, 1);
        return value;
    };
    return glm::vec4(component("r", 1, 1.0f), component("g", 2, 1.0f), component("b", 3, 1.0f), component("a", 4, 1.0f));
}

float Yaw(const glm::quat &rotation)
{
    const glm::vec3 forward = rotation * glm::vec3(0.0f, 0.0f, -1.0f);
    return std::atan2(-forward.x, -forward.z);
}

// ---- objects ---------------------------------------------------------------------

GameObject *Resolve(Scene *scene, uint64_t id)
{
    if (!scene || scene != E().GetScene())
    {
        return nullptr;
    }
    GameObject *object = scene->FindObjectByID(id);
    return object && object->IsAlive() ? object : nullptr;
}

GameObject *CheckObject(lua_State *L, int index)
{
    auto *handle = static_cast<ObjectHandle *>(luaL_checkudata(L, index, kObjectMeta));
    GameObject *object = Resolve(handle->scene, handle->id);
    if (!object)
    {
        luaL_error(L, "the object was destroyed");
    }
    return object;
}

Component *FindComponent(GameObject &object, const char *type)
{
    for (const auto &component : object.GetComponents())
    {
        if (std::strcmp(component->GetTypeName(), type) == 0)
        {
            return component.get();
        }
    }
    return nullptr;
}

void PushComponent(lua_State *L, GameObject *object, Component *component)
{
    if (!object || !component)
    {
        lua_pushnil(L);
        return;
    }
    auto *handle = static_cast<ComponentHandle *>(lua_newuserdatauv(L, sizeof(ComponentHandle), 0));
    handle->scene = object->GetScene();
    handle->id = object->GetID();
    std::strncpy(handle->type, component->GetTypeName(), sizeof(handle->type) - 1);
    handle->type[sizeof(handle->type) - 1] = '\0';
    luaL_setmetatable(L, kComponentMeta);
}

Component *CheckComponent(lua_State *L, int index, GameObject **owner = nullptr)
{
    auto *handle = static_cast<ComponentHandle *>(luaL_checkudata(L, index, kComponentMeta));
    GameObject *object = Resolve(handle->scene, handle->id);
    Component *component = object ? FindComponent(*object, handle->type) : nullptr;
    if (!component)
    {
        luaL_error(L, "the %s was destroyed", handle->type);
    }
    if (owner)
    {
        *owner = object;
    }
    return component;
}

template <typename T> T *CheckTyped(lua_State *L, int index)
{
    Component *component = CheckComponent(L, index);
    if (component->GetTypeID() != T::TypeID())
    {
        luaL_error(L, "expected a %s", T().GetTypeName());
    }
    return static_cast<T *>(component);
}

// Reflected properties <-> Lua, through the JSON the scene files use.
void PushProperty(lua_State *L, const PropertyDesc &property, const nlohmann::json &value)
{
    if (property.type == PropertyType::Vec3 && value.is_array() && value.size() == 3)
    {
        PushVec3(L, value[0].get<double>(), value[1].get<double>(), value[2].get<double>());
        return;
    }
    if (property.type == PropertyType::Quat && value.is_array() && value.size() == 4)
    {
        PushQuat(L, glm::quat(value[3].get<float>(), value[0].get<float>(), value[1].get<float>(), value[2].get<float>()));
        return;
    }
    PushJson(L, value);
}

nlohmann::json PropertyFromLua(lua_State *L, const PropertyDesc &property, int index)
{
    switch (property.type)
    {
    case PropertyType::Vec3: {
        const glm::vec3 v = CheckVec3(L, index);
        return nlohmann::json::array({v.x, v.y, v.z});
    }
    case PropertyType::Quat: {
        const glm::quat q = CheckQuat(L, index);
        return nlohmann::json::array({q.x, q.y, q.z, q.w});
    }
    case PropertyType::Bool:
        return nlohmann::json(static_cast<bool>(lua_toboolean(L, index)));
    case PropertyType::Int:
        return nlohmann::json(static_cast<int>(luaL_checkinteger(L, index)));
    case PropertyType::Float:
        return nlohmann::json(static_cast<float>(luaL_checknumber(L, index)));
    default:
        return ToJson(L, index);
    }
}

const PropertyDesc *FindProperty(const char *type, const char *name)
{
    const ComponentTypeDesc *desc = TypeRegistry::Get().Find(type);
    if (!desc)
    {
        return nullptr;
    }
    for (const PropertyDesc &property : desc->properties)
    {
        if (property.name == name)
        {
            return &property;
        }
    }
    return nullptr;
}

bool SetProperties(lua_State *L, Component &component, int tableIndex)
{
    tableIndex = lua_absindex(L, tableIndex);
    lua_pushnil(L);
    while (lua_next(L, tableIndex) != 0)
    {
        if (lua_type(L, -2) == LUA_TSTRING)
        {
            const char *key = lua_tostring(L, -2);
            if (const PropertyDesc *property = FindProperty(component.GetTypeName(), key))
            {
                SerializationContext context{E().GetScene(), &E().GetAssetManager()};
                property->set(component, PropertyFromLua(L, *property, -1), context);
            }
            else
            {
                GENI_LOG_WARN("Lua: %s has no property '%s'", component.GetTypeName(), key);
            }
        }
        lua_pop(L, 1);
    }
    return true;
}

int ObjectEq(lua_State *L)
{
    auto *a = static_cast<ObjectHandle *>(luaL_testudata(L, 1, kObjectMeta));
    auto *b = static_cast<ObjectHandle *>(luaL_testudata(L, 2, kObjectMeta));
    lua_pushboolean(L, a && b && a->id == b->id && a->scene == b->scene);
    return 1;
}

int ObjectToString(lua_State *L)
{
    auto *handle = static_cast<ObjectHandle *>(luaL_checkudata(L, 1, kObjectMeta));
    GameObject *object = Resolve(handle->scene, handle->id);
    lua_pushfstring(L, "GameObject(%s)", object ? object->GetName().c_str() : "destroyed");
    return 1;
}

int ObjectValid(lua_State *L)
{
    auto *handle = static_cast<ObjectHandle *>(luaL_checkudata(L, 1, kObjectMeta));
    lua_pushboolean(L, Resolve(handle->scene, handle->id) != nullptr);
    return 1;
}

int ObjectName(lua_State *L)
{
    lua_pushstring(L, CheckObject(L, 1)->GetName().c_str());
    return 1;
}

int ObjectSetName(lua_State *L)
{
    CheckObject(L, 1)->SetName(luaL_checkstring(L, 2));
    return 0;
}

int ObjectId(lua_State *L)
{
    lua_pushinteger(L, static_cast<lua_Integer>(CheckObject(L, 1)->GetID()));
    return 1;
}

int ObjectActive(lua_State *L)
{
    lua_pushboolean(L, CheckObject(L, 1)->IsActive());
    return 1;
}

int ObjectSetActive(lua_State *L)
{
    CheckObject(L, 1)->SetActive(lua_toboolean(L, 2));
    return 0;
}

int ObjectDestroy(lua_State *L)
{
    auto *handle = static_cast<ObjectHandle *>(luaL_checkudata(L, 1, kObjectMeta));
    if (GameObject *object = Resolve(handle->scene, handle->id))
    {
        object->MarkForDestroy();
    }
    return 0;
}

int ObjectParent(lua_State *L)
{
    PushObject(L, CheckObject(L, 1)->GetParent());
    return 1;
}

int ObjectSetParent(lua_State *L)
{
    GameObject *object = CheckObject(L, 1);
    GameObject *parent = lua_isnoneornil(L, 2) ? nullptr : CheckObject(L, 2);
    lua_pushboolean(L, object->SetParent(parent));
    return 1;
}

int ObjectChildren(lua_State *L)
{
    GameObject *object = CheckObject(L, 1);
    lua_newtable(L);
    int i = 1;
    for (const auto &child : object->GetChildren())
    {
        if (child->IsAlive())
        {
            PushObject(L, child.get());
            lua_rawseti(L, -2, i++);
        }
    }
    return 1;
}

int ObjectFind(lua_State *L)
{
    PushObject(L, CheckObject(L, 1)->FindChildByName(luaL_checkstring(L, 2)));
    return 1;
}

int ObjectPosition(lua_State *L)
{
    PushVec3(L, CheckObject(L, 1)->GetPosition());
    return 1;
}

int ObjectSetPosition(lua_State *L)
{
    CheckObject(L, 1)->SetPosition(CheckVec3(L, 2));
    return 0;
}

int ObjectWorldPosition(lua_State *L)
{
    PushVec3(L, CheckObject(L, 1)->GetWorldPosition());
    return 1;
}

int ObjectSetWorldPosition(lua_State *L)
{
    GameObject *object = CheckObject(L, 1);
    glm::vec3 position = CheckVec3(L, 2);
    if (GameObject *parent = object->GetParent())
    {
        position = glm::vec3(glm::inverse(parent->GetWorldTransform()) * glm::vec4(position, 1.0f));
    }
    object->SetPosition(position);
    return 0;
}

// Absolute world coordinates (scene position + origin), in double precision.
int ObjectWorldCoords(lua_State *L)
{
    GameObject *object = CheckObject(L, 1);
    PushVec3(L, object->GetScene()->ToWorld(object->GetWorldPosition()));
    return 1;
}

int ObjectRotation(lua_State *L)
{
    PushQuat(L, CheckObject(L, 1)->GetRotation());
    return 1;
}

int ObjectSetRotation(lua_State *L)
{
    CheckObject(L, 1)->SetRotation(CheckQuat(L, 2));
    return 0;
}

int ObjectYaw(lua_State *L)
{
    lua_pushnumber(L, glm::degrees(Yaw(CheckObject(L, 1)->GetWorldRotation())));
    return 1;
}

int ObjectSetYaw(lua_State *L)
{
    const float degrees = static_cast<float>(luaL_checknumber(L, 2));
    CheckObject(L, 1)->SetRotation(glm::angleAxis(glm::radians(degrees), glm::vec3(0.0f, 1.0f, 0.0f)));
    return 0;
}

int ObjectForward(lua_State *L)
{
    PushVec3(L, CheckObject(L, 1)->GetWorldRotation() * glm::vec3(0.0f, 0.0f, -1.0f));
    return 1;
}

int ObjectRight(lua_State *L)
{
    PushVec3(L, CheckObject(L, 1)->GetWorldRotation() * glm::vec3(1.0f, 0.0f, 0.0f));
    return 1;
}

int ObjectScale(lua_State *L)
{
    PushVec3(L, CheckObject(L, 1)->GetScale());
    return 1;
}

int ObjectSetScale(lua_State *L)
{
    GameObject *object = CheckObject(L, 1);
    if (lua_isnumber(L, 2))
    {
        object->SetScale(glm::vec3(static_cast<float>(lua_tonumber(L, 2))));
    }
    else
    {
        object->SetScale(CheckVec3(L, 2));
    }
    return 0;
}

int ObjectGet(lua_State *L)
{
    GameObject *object = CheckObject(L, 1);
    PushComponent(L, object, FindComponent(*object, luaL_checkstring(L, 2)));
    return 1;
}

int ObjectAdd(lua_State *L)
{
    GameObject *object = CheckObject(L, 1);
    const char *type = luaL_checkstring(L, 2);
    Component *component = TypeRegistry::Get().Create(type);
    if (!component)
    {
        return luaL_error(L, "unknown component type '%s'", type);
    }
    if (lua_istable(L, 3))
    {
        SetProperties(L, *component, 3);
    }
    object->AddComponent(component);
    SerializationContext context{object->GetScene(), &E().GetAssetManager()};
    component->ResolveAssets(context);
    PushComponent(L, object, component);
    return 1;
}

int ObjectRemove(lua_State *L)
{
    GameObject *object = CheckObject(L, 1);
    Component *component = FindComponent(*object, luaL_checkstring(L, 2));
    lua_pushboolean(L, component && object->RemoveComponent(component));
    return 1;
}

// ---- components ------------------------------------------------------------------

int ComponentIndex(lua_State *L)
{
    auto *handle = static_cast<ComponentHandle *>(luaL_checkudata(L, 1, kComponentMeta));
    const char *key = luaL_checkstring(L, 2);
    // Methods of this type, then common methods.
    lua_getfield(L, LUA_REGISTRYINDEX, "Geni.componentMethods");
    lua_getfield(L, -1, handle->type);
    if (lua_istable(L, -1))
    {
        lua_getfield(L, -1, key);
        if (!lua_isnil(L, -1))
        {
            return 1;
        }
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
    // Then reflected properties (a light's `type` is its property, not the method).
    if (const PropertyDesc *property = FindProperty(handle->type, key))
    {
        GameObject *object = nullptr;
        Component *component = CheckComponent(L, 1, &object);
        PushProperty(L, *property, property->get(*component));
        return 1;
    }
    lua_getfield(L, -1, "*");
    lua_getfield(L, -1, key);
    return 1;
}

int ComponentNewIndex(lua_State *L)
{
    auto *handle = static_cast<ComponentHandle *>(luaL_checkudata(L, 1, kComponentMeta));
    const char *key = luaL_checkstring(L, 2);
    Component *component = CheckComponent(L, 1);
    const PropertyDesc *property = FindProperty(handle->type, key);
    if (!property || !property->set)
    {
        return luaL_error(L, "%s has no property '%s'", handle->type, key);
    }
    SerializationContext context{E().GetScene(), &E().GetAssetManager()};
    if (!property->set(*component, PropertyFromLua(L, *property, 3), context))
    {
        return luaL_error(L, "bad value for %s.%s", handle->type, key);
    }
    return 0;
}

int ComponentToString(lua_State *L)
{
    auto *handle = static_cast<ComponentHandle *>(luaL_checkudata(L, 1, kComponentMeta));
    lua_pushfstring(L, "%s", handle->type);
    return 1;
}

int ComponentObject(lua_State *L)
{
    GameObject *object = nullptr;
    CheckComponent(L, 1, &object);
    PushObject(L, object);
    return 1;
}

int ComponentType(lua_State *L)
{
    auto *handle = static_cast<ComponentHandle *>(luaL_checkudata(L, 1, kComponentMeta));
    lua_pushstring(L, handle->type);
    return 1;
}

int ComponentValid(lua_State *L)
{
    auto *handle = static_cast<ComponentHandle *>(luaL_checkudata(L, 1, kComponentMeta));
    GameObject *object = Resolve(handle->scene, handle->id);
    lua_pushboolean(L, object && FindComponent(*object, handle->type));
    return 1;
}

// CharacterControllerComponent
int CharacterMove(lua_State *L)
{
    auto *character = CheckTyped<CharacterControllerComponent>(L, 1);
    character->SetMoveInput(CheckVec3(L, 2), lua_toboolean(L, 3));
    return 0;
}

int CharacterJump(lua_State *L)
{
    CheckTyped<CharacterControllerComponent>(L, 1)->Jump();
    return 0;
}

// 2D (docs/design/0017). Sprite and tilemap properties (frame, flipX, color, ...) are
// reflected and need no functions here.
int SpriteAnimatorPlay(lua_State *L)
{
    auto *animator = CheckTyped<SpriteAnimatorComponent>(L, 1);
    const int first = static_cast<int>(luaL_checkinteger(L, 2));
    const int count = static_cast<int>(luaL_checkinteger(L, 3));
    const float fps = static_cast<float>(luaL_optnumber(L, 4, animator->GetFps()));
    const bool loop = lua_isnoneornil(L, 5) ? true : lua_toboolean(L, 5) != 0;
    animator->Play(first, count, fps, loop);
    return 0;
}

int SpriteAnimatorStop(lua_State *L)
{
    CheckTyped<SpriteAnimatorComponent>(L, 1)->Stop();
    return 0;
}

int SpriteAnimatorFinished(lua_State *L)
{
    lua_pushboolean(L, CheckTyped<SpriteAnimatorComponent>(L, 1)->IsFinished());
    return 1;
}

int TilemapTile(lua_State *L)
{
    auto *map = CheckTyped<TilemapComponent>(L, 1);
    lua_pushinteger(
        L, map->GetTile(static_cast<int>(luaL_checkinteger(L, 2)), static_cast<int>(luaL_checkinteger(L, 3)))
    );
    return 1;
}

int TilemapSetTile(lua_State *L)
{
    auto *map = CheckTyped<TilemapComponent>(L, 1);
    map->SetTile(
        static_cast<int>(luaL_checkinteger(L, 2)), static_cast<int>(luaL_checkinteger(L, 3)),
        static_cast<int>(luaL_checkinteger(L, 4))
    );
    return 0;
}

int TilemapSolid(lua_State *L)
{
    auto *map = CheckTyped<TilemapComponent>(L, 1);
    lua_pushboolean(
        L, map->IsSolid(static_cast<int>(luaL_checkinteger(L, 2)), static_cast<int>(luaL_checkinteger(L, 3)))
    );
    return 1;
}

// map:cell_at(position) -> column, row (of a world position in scene space).
int TilemapCellAt(lua_State *L)
{
    auto *map = CheckTyped<TilemapComponent>(L, 1);
    const glm::ivec2 cell = map->WorldToCell(CheckVec3(L, 2));
    lua_pushinteger(L, cell.x);
    lua_pushinteger(L, cell.y);
    return 2;
}

// Particles (docs/design/0019). Every setting is a reflected property.
int EmitterEmit(lua_State *L)
{
    CheckTyped<ParticleEmitterComponent>(L, 1)->Emit(static_cast<int>(luaL_checkinteger(L, 2)));
    return 0;
}

int EmitterClear(lua_State *L)
{
    CheckTyped<ParticleEmitterComponent>(L, 1)->Clear();
    return 0;
}

int EmitterRestart(lua_State *L)
{
    CheckTyped<ParticleEmitterComponent>(L, 1)->Restart();
    return 0;
}

int EmitterCount(lua_State *L)
{
    lua_pushinteger(L, CheckTyped<ParticleEmitterComponent>(L, 1)->GetParticleCount());
    return 1;
}

// Timelines (docs/design/0026).
int TimelinePlay(lua_State *L)
{
    CheckTyped<TimelineComponent>(L, 1)->Play();
    return 0;
}

int TimelinePause(lua_State *L)
{
    CheckTyped<TimelineComponent>(L, 1)->Pause();
    return 0;
}

int TimelineStop(lua_State *L)
{
    CheckTyped<TimelineComponent>(L, 1)->Stop();
    return 0;
}

int TimelineSeek(lua_State *L)
{
    auto *timeline = CheckTyped<TimelineComponent>(L, 1);
    timeline->SetTime(static_cast<float>(luaL_checknumber(L, 2)));
    timeline->ApplyAt(timeline->GetTime());
    return 0;
}

int TimelinePlaying(lua_State *L)
{
    lua_pushboolean(L, CheckTyped<TimelineComponent>(L, 1)->IsPlaying());
    return 1;
}

int TimelineTime(lua_State *L)
{
    lua_pushnumber(L, CheckTyped<TimelineComponent>(L, 1)->GetTime());
    return 1;
}

int TimelineEvents(lua_State *L)
{
    const std::vector<std::string> events = CheckTyped<TimelineComponent>(L, 1)->TakeEvents();
    lua_createtable(L, static_cast<int>(events.size()), 0);
    for (size_t i = 0; i < events.size(); ++i)
    {
        lua_pushstring(L, events[i].c_str());
        lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
    }
    return 1;
}

// Weather (docs/design/0020): the targets are reflected properties.
int WeatherSnap(lua_State *L)
{
    CheckTyped<WeatherComponent>(L, 1)->Snap();
    return 0;
}

int TilemapCellCenter(lua_State *L)
{
    auto *map = CheckTyped<TilemapComponent>(L, 1);
    PushVec3(L, map->CellToWorld(static_cast<int>(luaL_checkinteger(L, 2)), static_cast<int>(luaL_checkinteger(L, 3))));
    return 1;
}

int CharacterTeleport(lua_State *L)
{
    CheckTyped<CharacterControllerComponent>(L, 1)->Teleport(CheckVec3(L, 2));
    return 0;
}

int CharacterSetEnabled(lua_State *L)
{
    CheckTyped<CharacterControllerComponent>(L, 1)->SetEnabled(lua_toboolean(L, 2));
    return 0;
}

int CharacterEnabled(lua_State *L)
{
    lua_pushboolean(L, CheckTyped<CharacterControllerComponent>(L, 1)->IsEnabled());
    return 1;
}

int CharacterSpeed(lua_State *L)
{
    lua_pushnumber(L, CheckTyped<CharacterControllerComponent>(L, 1)->GetSpeed());
    return 1;
}

int CharacterGrounded(lua_State *L)
{
    lua_pushboolean(L, CheckTyped<CharacterControllerComponent>(L, 1)->IsGrounded());
    return 1;
}

int CharacterVelocity(lua_State *L)
{
    PushVec3(L, CheckTyped<CharacterControllerComponent>(L, 1)->GetVelocity());
    return 1;
}

// VehicleComponent
int VehicleSetInput(lua_State *L)
{
    CheckTyped<VehicleComponent>(L, 1)->SetDriverInput(
        static_cast<float>(luaL_optnumber(L, 2, 0.0)), static_cast<float>(luaL_optnumber(L, 3, 0.0)),
        static_cast<float>(luaL_optnumber(L, 4, 0.0)), static_cast<float>(luaL_optnumber(L, 5, 0.0))
    );
    return 0;
}

int VehicleSpeed(lua_State *L)
{
    lua_pushnumber(L, CheckTyped<VehicleComponent>(L, 1)->GetSpeed());
    return 1;
}

int VehicleRpm(lua_State *L)
{
    lua_pushnumber(L, CheckTyped<VehicleComponent>(L, 1)->GetRpm());
    return 1;
}

int VehicleGear(lua_State *L)
{
    lua_pushinteger(L, CheckTyped<VehicleComponent>(L, 1)->GetGear());
    return 1;
}

int VehicleTeleport(lua_State *L)
{
    const float yaw = glm::radians(static_cast<float>(luaL_optnumber(L, 3, 0.0)));
    CheckTyped<VehicleComponent>(L, 1)->Teleport(CheckVec3(L, 2), glm::angleAxis(yaw, glm::vec3(0.0f, 1.0f, 0.0f)));
    return 0;
}

// AnimatorComponent
int AnimatorSet(lua_State *L)
{
    auto *animator = CheckTyped<AnimatorComponent>(L, 1);
    const char *name = luaL_checkstring(L, 2);
    if (lua_isboolean(L, 3))
    {
        animator->SetBool(name, lua_toboolean(L, 3));
    }
    else
    {
        animator->SetFloat(name, static_cast<float>(luaL_checknumber(L, 3)));
    }
    return 0;
}

int AnimatorGet(lua_State *L)
{
    lua_pushnumber(L, CheckTyped<AnimatorComponent>(L, 1)->GetFloat(luaL_checkstring(L, 2)));
    return 1;
}

int AnimatorTrigger(lua_State *L)
{
    CheckTyped<AnimatorComponent>(L, 1)->SetTrigger(luaL_checkstring(L, 2));
    return 0;
}

int AnimatorState(lua_State *L)
{
    lua_pushstring(L, CheckTyped<AnimatorComponent>(L, 1)->GetStateName().c_str());
    return 1;
}

int AnimatorPlay(lua_State *L)
{
    CheckTyped<AnimatorComponent>(L, 1)->Play(luaL_checkstring(L, 2), static_cast<float>(luaL_optnumber(L, 3, 0.0)));
    return 0;
}

int AnimatorEvents(lua_State *L)
{
    const std::vector<std::string> events = CheckTyped<AnimatorComponent>(L, 1)->ConsumeEvents();
    lua_createtable(L, static_cast<int>(events.size()), 0);
    for (size_t i = 0; i < events.size(); ++i)
    {
        lua_pushstring(L, events[i].c_str());
        lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
    }
    return 1;
}

// NavAgentComponent
int AgentGoTo(lua_State *L)
{
    lua_pushboolean(L, CheckTyped<NavAgentComponent>(L, 1)->MoveTo(CheckVec3(L, 2)));
    return 1;
}

int AgentStop(lua_State *L)
{
    CheckTyped<NavAgentComponent>(L, 1)->Stop();
    return 0;
}

int AgentArrived(lua_State *L)
{
    lua_pushboolean(L, CheckTyped<NavAgentComponent>(L, 1)->HasArrived());
    return 1;
}

int AgentOnMesh(lua_State *L)
{
    lua_pushboolean(L, CheckTyped<NavAgentComponent>(L, 1)->IsOnMesh());
    return 1;
}

int AgentSpeed(lua_State *L)
{
    lua_pushnumber(L, CheckTyped<NavAgentComponent>(L, 1)->GetSpeed());
    return 1;
}

// AudioSourceComponent
int SourcePlay(lua_State *L)
{
    CheckTyped<AudioSourceComponent>(L, 1)->Play();
    return 0;
}

int SourceStop(lua_State *L)
{
    CheckTyped<AudioSourceComponent>(L, 1)->Stop(static_cast<float>(luaL_optnumber(L, 2, 0.0)));
    return 0;
}

int SourcePlaying(lua_State *L)
{
    lua_pushboolean(L, CheckTyped<AudioSourceComponent>(L, 1)->IsPlaying());
    return 1;
}

// ThirdPersonCameraComponent
int CameraSetTarget(lua_State *L)
{
    auto *camera = CheckTyped<ThirdPersonCameraComponent>(L, 1);
    camera->SetTarget(lua_isnoneornil(L, 2) ? nullptr : CheckObject(L, 2));
    return 0;
}

int CameraTarget(lua_State *L)
{
    PushObject(L, CheckTyped<ThirdPersonCameraComponent>(L, 1)->GetTargetObject());
    return 1;
}

int CameraSnap(lua_State *L)
{
    CheckTyped<ThirdPersonCameraComponent>(L, 1)->SnapBehindTarget();
    return 0;
}

// PhysicsComponent
uint32_t BodyOf(GameObject &object)
{
    if (auto *vehicle = object.GetComponent<VehicleComponent>())
    {
        if (vehicle->GetVehicle())
        {
            return vehicle->GetVehicle()->GetBodyId();
        }
    }
    if (auto *character = object.GetComponent<CharacterControllerComponent>())
    {
        if (character->GetController())
        {
            return character->GetController()->GetInnerBodyId();
        }
    }
    if (auto *physics = object.GetComponent<PhysicsComponent>())
    {
        if (physics->GetRigidBody() && physics->GetRigidBody()->IsAddedToWorld())
        {
            return physics->GetRigidBody()->GetBodyId();
        }
    }
    return RigidBody::InvalidBodyId;
}

int BodyVelocity(lua_State *L)
{
    GameObject *object = nullptr;
    CheckComponent(L, 1, &object);
    const uint32_t body = BodyOf(*object);
    PushVec3(L, body == RigidBody::InvalidBodyId ? glm::vec3(0.0f) : E().GetPhysicsManager().GetBodyLinearVelocity(body));
    return 1;
}

int BodySetVelocity(lua_State *L)
{
    GameObject *object = nullptr;
    CheckComponent(L, 1, &object);
    const uint32_t body = BodyOf(*object);
    if (body != RigidBody::InvalidBodyId)
    {
        E().GetPhysicsManager().SetBodyLinearVelocity(body, CheckVec3(L, 2));
    }
    return 0;
}

int BodyImpulse(lua_State *L)
{
    GameObject *object = nullptr;
    CheckComponent(L, 1, &object);
    const uint32_t body = BodyOf(*object);
    if (body != RigidBody::InvalidBodyId)
    {
        E().GetPhysicsManager().AddBodyImpulse(body, CheckVec3(L, 2));
    }
    return 0;
}

// ScriptComponent
int ScriptInstance(lua_State *L)
{
    auto *script = CheckTyped<ScriptComponent>(L, 1);
    script->EnsureStarted();
    if (script->GetInstanceRef() < 0)
    {
        lua_pushnil(L);
        return 1;
    }
    lua_rawgeti(L, LUA_REGISTRYINDEX, script->GetInstanceRef());
    return 1;
}

// ---- libraries -------------------------------------------------------------------

std::string JoinArguments(lua_State *L)
{
    std::string text;
    const int count = lua_gettop(L);
    for (int i = 1; i <= count; ++i)
    {
        if (i > 1)
        {
            text += ' ';
        }
        text += luaL_tolstring(L, i, nullptr);
        lua_pop(L, 1);
    }
    return text;
}

int LogInfo(lua_State *L)
{
    GENI_LOG_INFO("[lua] %s", JoinArguments(L).c_str());
    return 0;
}

int LogWarn(lua_State *L)
{
    GENI_LOG_WARN("[lua] %s", JoinArguments(L).c_str());
    return 0;
}

int LogError(lua_State *L)
{
    GENI_LOG_ERROR("[lua] %s", JoinArguments(L).c_str());
    return 0;
}

int InputDown(lua_State *L)
{
    lua_pushboolean(L, E().GetInputActions().IsDown(luaL_checkstring(L, 1)));
    return 1;
}

int InputPressed(lua_State *L)
{
    lua_pushboolean(L, E().GetInputActions().WasPressed(luaL_checkstring(L, 1)));
    return 1;
}

int InputReleased(lua_State *L)
{
    lua_pushboolean(L, E().GetInputActions().WasReleased(luaL_checkstring(L, 1)));
    return 1;
}

int InputValue(lua_State *L)
{
    lua_pushnumber(L, E().GetInputActions().GetValue(luaL_checkstring(L, 1)));
    return 1;
}

int InputVector(lua_State *L)
{
    float x = 0.0f;
    float y = 0.0f;
    E().GetInputActions().GetVector(luaL_checkstring(L, 1), luaL_checkstring(L, 2), x, y);
    lua_pushnumber(L, x);
    lua_pushnumber(L, y);
    return 2;
}

int InputDevice(lua_State *L)
{
    lua_pushstring(L, E().GetInputActions().GetLastDevice() == InputDevice::Gamepad ? "gamepad" : "keyboard");
    return 1;
}

int InputSetGroup(lua_State *L)
{
    E().GetInputActions().SetGroupEnabled(luaL_checkstring(L, 1), lua_toboolean(L, 2));
    return 0;
}

// input.override(name, value) forces an action; input.override(name) or (name, nil)
// releases it; input.override() releases all.
int InputOverride(lua_State *L)
{
    InputActions &actions = E().GetInputActions();
    if (lua_isnoneornil(L, 1))
    {
        actions.ClearOverrides();
    }
    else if (lua_isnoneornil(L, 2))
    {
        actions.ClearOverride(luaL_checkstring(L, 1));
    }
    else
    {
        const float value = lua_isboolean(L, 2) ? (lua_toboolean(L, 2) ? 1.0f : 0.0f) : static_cast<float>(luaL_checknumber(L, 2));
        actions.SetOverride(luaL_checkstring(L, 1), value);
    }
    return 0;
}

int InputCaptureCursor(lua_State *L)
{
    E().SetCursorCaptured(lua_toboolean(L, 1));
    return 0;
}

Scene &CheckScene(lua_State *L)
{
    Scene *scene = E().GetScene();
    if (!scene)
    {
        luaL_error(L, "there is no scene");
    }
    return *scene;
}

int SceneFind(lua_State *L)
{
    PushObject(L, CheckScene(L).FindObject(luaL_checkstring(L, 1)));
    return 1;
}

int SceneGet(lua_State *L)
{
    Scene &scene = CheckScene(L);
    PushObject(L, Resolve(&scene, static_cast<uint64_t>(luaL_checkinteger(L, 1))));
    return 1;
}

int SceneCreate(lua_State *L)
{
    Scene &scene = CheckScene(L);
    GameObject *parent = lua_isnoneornil(L, 2) ? nullptr : CheckObject(L, 2);
    PushObject(L, scene.CreateObject(luaL_checkstring(L, 1), parent));
    return 1;
}

// scene.spawn(path, position, yaw): a cooked model's scene (a .gltf) or a prefab.
int SceneSpawn(lua_State *L)
{
    Scene &scene = CheckScene(L);
    const std::string path = luaL_checkstring(L, 1);
    const glm::vec3 position = OptVec3(L, 2, glm::vec3(0.0f));
    const float yaw = glm::radians(static_cast<float>(luaL_optnumber(L, 3, 0.0)));
    std::string error;
    GameObject *root = SceneSerializer::Spawn(scene, path, position, yaw, &error);
    if (!root)
    {
        return luaL_error(L, "scene.spawn: %s", error.c_str());
    }
    PushObject(L, root);
    return 1;
}

int SceneMainCamera(lua_State *L)
{
    PushObject(L, CheckScene(L).GetMainCamera());
    return 1;
}

int SceneSetMainCamera(lua_State *L)
{
    CheckScene(L).SetMainCamera(CheckObject(L, 1));
    return 0;
}

int SceneToWorld(lua_State *L)
{
    PushVec3(L, CheckScene(L).ToWorld(CheckVec3(L, 1)));
    return 1;
}

int SceneToScene(lua_State *L)
{
    PushVec3(L, CheckScene(L).ToScene(CheckDVec3(L, 1)));
    return 1;
}

int SceneOrigin(lua_State *L)
{
    PushVec3(L, CheckScene(L).GetOrigin());
    return 1;
}

uint32_t LayerMask(lua_State *L, int index)
{
    const char *layers = luaL_optstring(L, index, "all");
    if (std::strcmp(layers, "static") == 0)
    {
        return PhysicsLayerStatic;
    }
    if (std::strcmp(layers, "moving") == 0)
    {
        return PhysicsLayerMoving;
    }
    return PhysicsLayerAll;
}

int PushHit(lua_State *L, bool hit, const PhysicsHit &result)
{
    if (!hit)
    {
        lua_pushnil(L);
        return 1;
    }
    Scene &scene = CheckScene(L);
    lua_createtable(L, 0, 4);
    PushVec3(L, scene.ToScene(result.position));
    lua_setfield(L, -2, "position");
    PushVec3(L, result.normal);
    lua_setfield(L, -2, "normal");
    lua_pushnumber(L, result.distance);
    lua_setfield(L, -2, "distance");
    PushObject(L, Resolve(&scene, result.userData));
    lua_setfield(L, -2, "object");
    return 1;
}

int PhysicsRaycast(lua_State *L)
{
    Scene &scene = CheckScene(L);
    const glm::vec3 origin = CheckVec3(L, 1);
    const glm::vec3 direction = CheckVec3(L, 2);
    const float distance = static_cast<float>(luaL_checknumber(L, 3));
    const uint32_t ignore = lua_isnoneornil(L, 5) ? RigidBody::InvalidBodyId : BodyOf(*CheckObject(L, 5));
    PhysicsHit hit;
    const bool found =
        E().GetPhysicsManager().CastRay(scene.ToWorld(origin), direction, distance, hit, LayerMask(L, 4), ignore);
    return PushHit(L, found, hit);
}

int PhysicsSphereCast(lua_State *L)
{
    Scene &scene = CheckScene(L);
    const glm::vec3 origin = CheckVec3(L, 1);
    const float radius = static_cast<float>(luaL_checknumber(L, 2));
    const glm::vec3 direction = CheckVec3(L, 3);
    const float distance = static_cast<float>(luaL_checknumber(L, 4));
    const uint32_t ignore = lua_isnoneornil(L, 6) ? RigidBody::InvalidBodyId : BodyOf(*CheckObject(L, 6));
    PhysicsHit hit;
    const bool found = E().GetPhysicsManager().CastSphere(
        scene.ToWorld(origin), radius, direction, distance, hit, LayerMask(L, 5), ignore
    );
    return PushHit(L, found, hit);
}

AudioBus CheckBus(lua_State *L, int index)
{
    const char *bus = luaL_optstring(L, index, "master");
    if (std::strcmp(bus, "music") == 0)
    {
        return AudioBus::Music;
    }
    if (std::strcmp(bus, "effects") == 0)
    {
        return AudioBus::Effects;
    }
    return AudioBus::Master;
}

int AudioPlay(lua_State *L)
{
    const char *path = luaL_checkstring(L, 1);
    SoundParams params;
    if (lua_istable(L, 2))
    {
        lua_getfield(L, 2, "volume");
        params.volume = static_cast<float>(luaL_optnumber(L, -1, 1.0));
        lua_getfield(L, 2, "pitch");
        params.pitch = static_cast<float>(luaL_optnumber(L, -1, 1.0));
        lua_getfield(L, 2, "loop");
        params.loop = lua_toboolean(L, -1);
        lua_getfield(L, 2, "min_distance");
        params.minDistance = static_cast<float>(luaL_optnumber(L, -1, params.minDistance));
        lua_getfield(L, 2, "max_distance");
        params.maxDistance = static_cast<float>(luaL_optnumber(L, -1, params.maxDistance));
        lua_getfield(L, 2, "position");
        if (lua_istable(L, -1))
        {
            params.spatial = true;
            params.position = CheckVec3(L, -1);
        }
        lua_pop(L, 6);
    }
    lua_pushinteger(L, E().GetAudio().Play(path, params));
    return 1;
}

int AudioStop(lua_State *L)
{
    E().GetAudio().Stop(static_cast<SoundId>(luaL_checkinteger(L, 1)), static_cast<float>(luaL_optnumber(L, 2, 0.0)));
    return 0;
}

int AudioPlaying(lua_State *L)
{
    lua_pushboolean(L, E().GetAudio().IsPlaying(static_cast<SoundId>(luaL_checkinteger(L, 1))));
    return 1;
}

int AudioSetPitch(lua_State *L)
{
    E().GetAudio().SetPitch(static_cast<SoundId>(luaL_checkinteger(L, 1)), static_cast<float>(luaL_checknumber(L, 2)));
    return 0;
}

int AudioSetSoundVolume(lua_State *L)
{
    E().GetAudio().SetVolume(static_cast<SoundId>(luaL_checkinteger(L, 1)), static_cast<float>(luaL_checknumber(L, 2)));
    return 0;
}

int AudioMove(lua_State *L)
{
    E().GetAudio().SetPosition(static_cast<SoundId>(luaL_checkinteger(L, 1)), CheckVec3(L, 2));
    return 0;
}

int AudioMusic(lua_State *L)
{
    E().GetAudio().PlayMusic(
        luaL_checkstring(L, 1), static_cast<float>(luaL_optnumber(L, 2, 1.0)), static_cast<float>(luaL_optnumber(L, 3, 1.0))
    );
    return 0;
}

int AudioStopMusic(lua_State *L)
{
    E().GetAudio().StopMusic(static_cast<float>(luaL_optnumber(L, 1, 1.0)));
    return 0;
}

int AudioSetVolume(lua_State *L)
{
    E().GetAudio().SetBusVolume(CheckBus(L, 1), static_cast<float>(luaL_checknumber(L, 2)));
    return 0;
}

int AudioVolume(lua_State *L)
{
    lua_pushnumber(L, E().GetAudio().GetBusVolume(CheckBus(L, 1)));
    return 1;
}

int NavReady(lua_State *L)
{
    lua_pushboolean(L, E().GetNavigation().IsInitialized() && E().GetNavigation().GetStats().tiles > 0);
    return 1;
}

int NavRandomPoint(lua_State *L)
{
    Scene &scene = CheckScene(L);
    glm::dvec3 result;
    if (E().GetNavigation().RandomPointNear(scene.ToWorld(CheckVec3(L, 1)), static_cast<float>(luaL_checknumber(L, 2)), result))
    {
        PushVec3(L, scene.ToScene(result));
    }
    else
    {
        lua_pushnil(L);
    }
    return 1;
}

int NavNearest(lua_State *L)
{
    Scene &scene = CheckScene(L);
    glm::dvec3 result;
    if (E().GetNavigation().FindNearestPoint(
            scene.ToWorld(CheckVec3(L, 1)), result, static_cast<float>(luaL_optnumber(L, 2, 4.0))
        ))
    {
        PushVec3(L, scene.ToScene(result));
    }
    else
    {
        lua_pushnil(L);
    }
    return 1;
}

int NavPath(lua_State *L)
{
    Scene &scene = CheckScene(L);
    std::vector<glm::dvec3> corners;
    const bool complete =
        E().GetNavigation().FindPath(scene.ToWorld(CheckVec3(L, 1)), scene.ToWorld(CheckVec3(L, 2)), corners);
    lua_createtable(L, static_cast<int>(corners.size()), 0);
    for (size_t i = 0; i < corners.size(); ++i)
    {
        PushVec3(L, scene.ToScene(corners[i]));
        lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
    }
    lua_pushboolean(L, complete);
    return 2;
}

UiAnchor CheckAnchor(lua_State *L, int index)
{
    static const std::pair<const char *, UiAnchor> anchors[] = {
        {"top_left", UiAnchor::TopLeft},       {"top", UiAnchor::Top},         {"top_right", UiAnchor::TopRight},
        {"left", UiAnchor::Left},              {"center", UiAnchor::Center},   {"right", UiAnchor::Right},
        {"bottom_left", UiAnchor::BottomLeft}, {"bottom", UiAnchor::Bottom},   {"bottom_right", UiAnchor::BottomRight},
    };
    const char *name = luaL_optstring(L, index, "top_left");
    for (const auto &[text, anchor] : anchors)
    {
        if (std::strcmp(text, name) == 0)
        {
            return anchor;
        }
    }
    luaL_error(L, "unknown anchor '%s'", name);
    return UiAnchor::TopLeft;
}

glm::vec2 OptOffset(lua_State *L, int index)
{
    return glm::vec2(static_cast<float>(luaL_optnumber(L, index, 0.0)), static_cast<float>(luaL_optnumber(L, index + 1, 0.0)));
}

int UiText(lua_State *L)
{
    E().GetGameUI().Text(
        luaL_checkstring(L, 1), CheckAnchor(L, 2), OptOffset(L, 3), static_cast<float>(luaL_optnumber(L, 5, 32.0)),
        OptColor(L, 6, E().GetGameUI().GetStyle().text)
    );
    return 0;
}

int UiPanel(lua_State *L)
{
    E().GetGameUI().Panel(
        CheckAnchor(L, 1), OptOffset(L, 2),
        glm::vec2(static_cast<float>(luaL_checknumber(L, 4)), static_cast<float>(luaL_checknumber(L, 5))),
        OptColor(L, 6, E().GetGameUI().GetStyle().panel), static_cast<float>(luaL_optnumber(L, 7, -1.0))
    );
    return 0;
}

int UiBar(lua_State *L)
{
    const UiStyle &style = E().GetGameUI().GetStyle();
    E().GetGameUI().Bar(
        CheckAnchor(L, 1), OptOffset(L, 2),
        glm::vec2(static_cast<float>(luaL_checknumber(L, 4)), static_cast<float>(luaL_checknumber(L, 5))),
        static_cast<float>(luaL_checknumber(L, 6)), OptColor(L, 7, style.accent), OptColor(L, 8, style.item)
    );
    return 0;
}

int UiFade(lua_State *L)
{
    E().GetGameUI().Fade(static_cast<float>(luaL_checknumber(L, 1)), OptColor(L, 2, glm::vec4(0.0f, 0.0f, 0.0f, 1.0f)));
    return 0;
}

int UiMeasure(lua_State *L)
{
    lua_pushnumber(L, E().GetGameUI().MeasureText(luaL_checkstring(L, 1), static_cast<float>(luaL_optnumber(L, 2, 32.0))));
    return 1;
}

int UiCanvas(lua_State *L)
{
    const glm::vec2 canvas = E().GetGameUI().GetCanvasSize();
    lua_pushnumber(L, canvas.x);
    lua_pushnumber(L, canvas.y);
    return 2;
}

int UiBeginMenu(lua_State *L)
{
    E().GetGameUI().BeginMenu(luaL_checkstring(L, 1), luaL_optstring(L, 2, ""), static_cast<float>(luaL_optnumber(L, 3, 560.0)));
    return 0;
}

int UiButton(lua_State *L)
{
    lua_pushboolean(L, E().GetGameUI().Button(luaL_checkstring(L, 1)));
    return 1;
}

int UiSlider(lua_State *L)
{
    float value = static_cast<float>(luaL_checknumber(L, 2));
    const bool changed = E().GetGameUI().Slider(
        luaL_checkstring(L, 1), value, static_cast<float>(luaL_optnumber(L, 3, 0.0)),
        static_cast<float>(luaL_optnumber(L, 4, 1.0)), static_cast<float>(luaL_optnumber(L, 5, 0.1))
    );
    lua_pushnumber(L, value);
    lua_pushboolean(L, changed);
    return 2;
}

int UiToggle(lua_State *L)
{
    bool value = lua_toboolean(L, 2);
    const bool changed = E().GetGameUI().Toggle(luaL_checkstring(L, 1), value);
    lua_pushboolean(L, value);
    lua_pushboolean(L, changed);
    return 2;
}

int UiLabel(lua_State *L)
{
    E().GetGameUI().Label(luaL_checkstring(L, 1));
    return 0;
}

int UiEndMenu(lua_State *L)
{
    (void)L;
    E().GetGameUI().EndMenu();
    return 0;
}

int UiFocus(lua_State *L)
{
    lua_pushinteger(L, E().GetGameUI().GetFocus(luaL_checkstring(L, 1)));
    return 1;
}

int UiSetFocus(lua_State *L)
{
    E().GetGameUI().SetFocus(luaL_checkstring(L, 1), static_cast<int>(luaL_checkinteger(L, 2)));
    return 0;
}

int UiAccent(lua_State *L)
{
    const glm::vec4 c = E().GetGameUI().GetStyle().accent;
    lua_createtable(L, 4, 0);
    const float values[4] = {c.r, c.g, c.b, c.a};
    for (int i = 0; i < 4; ++i)
    {
        lua_pushnumber(L, values[i]);
        lua_rawseti(L, -2, i + 1);
    }
    return 1;
}

int TimeNow(lua_State *L)
{
    lua_pushnumber(L, std::chrono::duration<double>(std::chrono::steady_clock::now() - s_start).count());
    return 1;
}

int TimeFrame(lua_State *L)
{
    lua_pushinteger(L, static_cast<lua_Integer>(E().GetFrameStats().frameIndex));
    return 1;
}

int GameQuit(lua_State *L)
{
    (void)L;
    if (Application *app = E().GetApplication())
    {
        app->SetNeedsToBeClosed(true);
    }
    return 0;
}

int GameScreenshot(lua_State *L)
{
    E().RequestScreenshot(luaL_checkstring(L, 1));
    return 0;
}

int GameSetPaused(lua_State *L)
{
    E().SetPhysicsPaused(lua_toboolean(L, 1));
    return 0;
}

void SetFunctions(lua_State *L, const luaL_Reg *functions)
{
    luaL_setfuncs(L, functions, 0);
}

void NewLibrary(lua_State *L, const char *name, const luaL_Reg *functions)
{
    lua_newtable(L);
    SetFunctions(L, functions);
    lua_setglobal(L, name);
}

void AddMethods(lua_State *L, const char *type, const luaL_Reg *functions)
{
    lua_getfield(L, LUA_REGISTRYINDEX, "Geni.componentMethods");
    lua_newtable(L);
    SetFunctions(L, functions);
    lua_setfield(L, -2, type);
    lua_pop(L, 1);
}

} // namespace

void PushObject(lua_State *L, GameObject *object)
{
    if (!object || !object->GetScene())
    {
        lua_pushnil(L);
        return;
    }
    auto *handle = static_cast<ObjectHandle *>(lua_newuserdatauv(L, sizeof(ObjectHandle), 0));
    handle->scene = object->GetScene();
    handle->id = object->GetID();
    luaL_setmetatable(L, kObjectMeta);
}

GameObject *ToObject(lua_State *L, int index)
{
    auto *handle = static_cast<ObjectHandle *>(luaL_testudata(L, index, kObjectMeta));
    return handle ? Resolve(handle->scene, handle->id) : nullptr;
}

void PushJson(lua_State *L, const nlohmann::json &value)
{
    switch (value.type())
    {
    case nlohmann::json::value_t::boolean:
        lua_pushboolean(L, value.get<bool>());
        break;
    case nlohmann::json::value_t::number_integer:
    case nlohmann::json::value_t::number_unsigned:
        lua_pushinteger(L, value.get<lua_Integer>());
        break;
    case nlohmann::json::value_t::number_float:
        lua_pushnumber(L, value.get<double>());
        break;
    case nlohmann::json::value_t::string:
        lua_pushstring(L, value.get<std::string>().c_str());
        break;
    case nlohmann::json::value_t::array: {
        lua_createtable(L, static_cast<int>(value.size()), 0);
        lua_Integer i = 1;
        for (const auto &entry : value)
        {
            PushJson(L, entry);
            lua_rawseti(L, -2, i++);
        }
        break;
    }
    case nlohmann::json::value_t::object:
        lua_createtable(L, 0, static_cast<int>(value.size()));
        for (auto it = value.begin(); it != value.end(); ++it)
        {
            PushJson(L, it.value());
            lua_setfield(L, -2, it.key().c_str());
        }
        break;
    default:
        lua_pushnil(L);
        break;
    }
}

nlohmann::json ToJson(lua_State *L, int index)
{
    index = lua_absindex(L, index);
    switch (lua_type(L, index))
    {
    case LUA_TBOOLEAN:
        return nlohmann::json(static_cast<bool>(lua_toboolean(L, index)));
    case LUA_TNUMBER:
        if (lua_isinteger(L, index))
        {
            return nlohmann::json(static_cast<int64_t>(lua_tointeger(L, index)));
        }
        return nlohmann::json(lua_tonumber(L, index));
    case LUA_TSTRING:
        return nlohmann::json(std::string(lua_tostring(L, index)));
    case LUA_TTABLE: {
        // A vec3 or an array becomes a JSON array; anything else an object.
        lua_getfield(L, index, "x");
        const bool vector = lua_isnumber(L, -1);
        lua_pop(L, 1);
        if (vector)
        {
            const glm::dvec3 v = CheckDVec3(L, index);
            return nlohmann::json::array({v.x, v.y, v.z});
        }
        const lua_Unsigned length = lua_rawlen(L, index);
        if (length > 0)
        {
            nlohmann::json array = nlohmann::json::array();
            for (lua_Unsigned i = 1; i <= length; ++i)
            {
                lua_rawgeti(L, index, static_cast<lua_Integer>(i));
                array.push_back(ToJson(L, -1));
                lua_pop(L, 1);
            }
            return array;
        }
        nlohmann::json object = nlohmann::json::object();
        lua_pushnil(L);
        while (lua_next(L, index) != 0)
        {
            if (lua_type(L, -2) == LUA_TSTRING)
            {
                object[lua_tostring(L, -2)] = ToJson(L, -1);
            }
            lua_pop(L, 1);
        }
        return object;
    }
    default:
        return nlohmann::json();
    }
}

// Missions and saved games (docs/design/0024).
MissionSystem &Missions()
{
    return Engine::GetInstance().GetMissions();
}

int MissionsLoad(lua_State *L)
{
    std::string error;
    const bool ok = Missions().LoadFile(Engine::GetInstance().GetFileSystem(), luaL_checkstring(L, 1), &error);
    lua_pushboolean(L, ok);
    if (!ok)
    {
        lua_pushstring(L, error.c_str());
        return 2;
    }
    return 1;
}

int MissionsStart(lua_State *L)
{
    lua_pushboolean(L, Missions().Start(luaL_checkstring(L, 1)));
    return 1;
}

int MissionsFail(lua_State *L)
{
    lua_pushboolean(L, Missions().Fail(luaL_checkstring(L, 1), luaL_optstring(L, 2, "")));
    return 1;
}

int MissionsAbandon(lua_State *L)
{
    lua_pushboolean(L, Missions().Abandon(luaL_checkstring(L, 1)));
    return 1;
}

int MissionsStatus(lua_State *L)
{
    const std::string id = luaL_checkstring(L, 1);
    if (!Missions().Find(id))
    {
        lua_pushnil(L);
        return 1;
    }
    lua_pushstring(L, MissionStateName(Missions().GetState(id)));
    return 1;
}

void PushObjective(lua_State *L, const ObjectiveDef &objective, int progress, bool done)
{
    lua_createtable(L, 0, 6);
    lua_pushstring(L, objective.id.c_str());
    lua_setfield(L, -2, "id");
    lua_pushstring(L, objective.text.c_str());
    lua_setfield(L, -2, "text");
    lua_pushstring(
        L, objective.kind == ObjectiveKind::Reach ? "reach" : objective.kind == ObjectiveKind::Count ? "count" : "custom"
    );
    lua_setfield(L, -2, "kind");
    lua_pushinteger(L, progress);
    lua_setfield(L, -2, "progress");
    lua_pushinteger(L, objective.count);
    lua_setfield(L, -2, "count");
    lua_pushboolean(L, done);
    lua_setfield(L, -2, "done");
    if (objective.kind == ObjectiveKind::Reach && objective.object.empty())
    {
        PushVec3(L, objective.position);
        lua_setfield(L, -2, "position");
        lua_pushnumber(L, objective.radius);
        lua_setfield(L, -2, "radius");
    }
}

int MissionsInfo(lua_State *L)
{
    const std::string id = luaL_checkstring(L, 1);
    const MissionDef *mission = Missions().Find(id);
    const MissionStatus *status = Missions().GetStatus(id);
    if (!mission || !status)
    {
        lua_pushnil(L);
        return 1;
    }
    lua_createtable(L, 0, 7);
    lua_pushstring(L, mission->id.c_str());
    lua_setfield(L, -2, "id");
    lua_pushstring(L, mission->title.c_str());
    lua_setfield(L, -2, "title");
    lua_pushstring(L, mission->description.c_str());
    lua_setfield(L, -2, "description");
    lua_pushstring(L, MissionStateName(status->state));
    lua_setfield(L, -2, "state");
    lua_pushnumber(L, status->elapsed);
    lua_setfield(L, -2, "elapsed");
    lua_pushnumber(L, mission->timeLimit);
    lua_setfield(L, -2, "time_limit");
    lua_createtable(L, static_cast<int>(mission->objectives.size()), 0);
    for (size_t i = 0; i < mission->objectives.size(); ++i)
    {
        PushObjective(L, mission->objectives[i], status->progress[i], i < status->current);
        lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
    }
    lua_setfield(L, -2, "objectives");
    return 1;
}

int MissionsCurrent(lua_State *L)
{
    const std::string id = luaL_checkstring(L, 1);
    const ObjectiveDef *objective = Missions().GetCurrentObjective(id);
    if (!objective)
    {
        lua_pushnil(L);
        return 1;
    }
    const MissionStatus *status = Missions().GetStatus(id);
    PushObjective(L, *objective, status->progress[status->current], false);
    return 1;
}

int MissionsProgress(lua_State *L)
{
    lua_pushboolean(
        L, Missions().AddProgress(luaL_checkstring(L, 1), luaL_optstring(L, 2, ""), static_cast<int>(luaL_optinteger(L, 3, 1)))
    );
    return 1;
}

int MissionsComplete(lua_State *L)
{
    lua_pushboolean(L, Missions().CompleteObjective(luaL_checkstring(L, 1), luaL_optstring(L, 2, "")));
    return 1;
}

void PushStringList(lua_State *L, const std::vector<std::string> &items)
{
    lua_createtable(L, static_cast<int>(items.size()), 0);
    for (size_t i = 0; i < items.size(); ++i)
    {
        lua_pushstring(L, items[i].c_str());
        lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
    }
}

int MissionsActive(lua_State *L)
{
    PushStringList(L, Missions().GetActive());
    return 1;
}

int MissionsList(lua_State *L)
{
    PushStringList(L, Missions().GetMissionIds());
    return 1;
}

int MissionsEvents(lua_State *L)
{
    const std::vector<MissionEvent> events = Missions().TakeEvents();
    lua_createtable(L, static_cast<int>(events.size()), 0);
    for (size_t i = 0; i < events.size(); ++i)
    {
        lua_createtable(L, 0, 4);
        lua_pushstring(L, MissionEventName(events[i].type));
        lua_setfield(L, -2, "type");
        lua_pushstring(L, events[i].mission.c_str());
        lua_setfield(L, -2, "mission");
        lua_pushstring(L, events[i].objective.c_str());
        lua_setfield(L, -2, "objective");
        lua_pushstring(L, events[i].reason.c_str());
        lua_setfield(L, -2, "reason");
        lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
    }
    return 1;
}

int MissionsSetPlayer(lua_State *L)
{
    GameObject *object = lua_isnoneornil(L, 1) ? nullptr : CheckObject(L, 1);
    Missions().SetPlayer(object ? object->GetID() : 0);
    return 0;
}

int MissionsState(lua_State *L)
{
    PushJson(L, Missions().SaveState());
    return 1;
}

int MissionsRestore(lua_State *L)
{
    std::string error;
    const bool ok = Missions().RestoreState(ToJson(L, 1), &error);
    lua_pushboolean(L, ok);
    if (!ok)
    {
        lua_pushstring(L, error.c_str());
        return 2;
    }
    return 1;
}

int SaveWrite(lua_State *L)
{
    std::string error;
    const bool ok = Engine::GetInstance().GetSaveGame().Write(luaL_checkstring(L, 1), ToJson(L, 2), &error);
    lua_pushboolean(L, ok);
    if (!ok)
    {
        lua_pushstring(L, error.c_str());
        return 2;
    }
    return 1;
}

int SaveRead(lua_State *L)
{
    nlohmann::json document;
    if (!Engine::GetInstance().GetSaveGame().Read(luaL_checkstring(L, 1), document))
    {
        lua_pushnil(L);
        return 1;
    }
    PushJson(L, document);
    return 1;
}

int SaveExists(lua_State *L)
{
    lua_pushboolean(L, Engine::GetInstance().GetSaveGame().Exists(luaL_checkstring(L, 1)));
    return 1;
}

int SaveDelete(lua_State *L)
{
    lua_pushboolean(L, Engine::GetInstance().GetSaveGame().Delete(luaL_checkstring(L, 1)));
    return 1;
}

// Traffic (docs/design/0025).
double FieldNumber(lua_State *L, int table, const char *name, double fallback)
{
    lua_getfield(L, table, name);
    const double value = lua_isnumber(L, -1) ? lua_tonumber(L, -1) : fallback;
    lua_pop(L, 1);
    return value;
}

// traffic.start{models = {...}, count, radius, cruise_min, cruise_max, seed,
//               grid = {origin_x, origin_z, columns, rows, spacing, half_width, lane_offset,
//                       height, speed} or roads = "path.groads"}
int TrafficStart(lua_State *L)
{
    luaL_checktype(L, 1, LUA_TTABLE);
    TrafficConfig config;
    lua_getfield(L, 1, "models");
    if (lua_istable(L, -1))
    {
        const lua_Integer count = luaL_len(L, -1);
        for (lua_Integer i = 1; i <= count; ++i)
        {
            lua_rawgeti(L, -1, i);
            if (lua_isstring(L, -1))
            {
                config.models.push_back(lua_tostring(L, -1));
            }
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);
    config.count = static_cast<int>(FieldNumber(L, 1, "count", config.count));
    config.radius = static_cast<float>(FieldNumber(L, 1, "radius", config.radius));
    config.cruiseMin = static_cast<float>(FieldNumber(L, 1, "cruise_min", config.cruiseMin));
    config.cruiseMax = static_cast<float>(FieldNumber(L, 1, "cruise_max", config.cruiseMax));
    config.seed = static_cast<uint32_t>(FieldNumber(L, 1, "seed", config.seed));

    RoadNetwork network;
    lua_getfield(L, 1, "grid");
    if (lua_istable(L, -1))
    {
        const int grid = lua_gettop(L);
        RoadGridDesc desc;
        desc.originX = FieldNumber(L, grid, "origin_x", desc.originX);
        desc.originZ = FieldNumber(L, grid, "origin_z", desc.originZ);
        desc.columns = static_cast<int>(FieldNumber(L, grid, "columns", desc.columns));
        desc.rows = static_cast<int>(FieldNumber(L, grid, "rows", desc.rows));
        desc.spacing = FieldNumber(L, grid, "spacing", desc.spacing);
        desc.halfWidth = FieldNumber(L, grid, "half_width", desc.halfWidth);
        desc.laneOffset = FieldNumber(L, grid, "lane_offset", desc.laneOffset);
        desc.height = FieldNumber(L, grid, "height", desc.height);
        desc.speed = static_cast<float>(FieldNumber(L, grid, "speed", desc.speed));
        network = RoadNetwork::Grid(desc);
    }
    lua_pop(L, 1);
    lua_getfield(L, 1, "roads");
    if (lua_isstring(L, -1))
    {
        const std::string text = E().GetFileSystem().LoadAssetFileText(lua_tostring(L, -1));
        std::string error;
        if (text.empty() || !network.LoadJson(nlohmann::json::parse(text, nullptr, false), &error))
        {
            return luaL_error(L, "traffic.start: %s", text.empty() ? "cannot read the road network" : error.c_str());
        }
    }
    lua_pop(L, 1);
    E().GetTraffic().Start(std::move(network), config);
    lua_pushboolean(L, E().GetTraffic().IsRunning());
    return 1;
}

int TrafficStop(lua_State *L)
{
    (void)L;
    E().GetTraffic().Stop();
    return 0;
}

int TrafficStats(lua_State *L)
{
    const Geni::TrafficStats stats = E().GetTraffic().GetStats();
    lua_pushinteger(L, stats.cars);
    lua_pushnumber(L, stats.driven);
    return 2;
}

int TrafficKeepClear(lua_State *L)
{
    E().GetTraffic().KeepClear(CheckDVec3(L, 1), static_cast<float>(luaL_checknumber(L, 2)));
    return 0;
}

int TrafficSetFocus(lua_State *L)
{
    GameObject *object = lua_isnoneornil(L, 1) ? nullptr : CheckObject(L, 1);
    E().GetTraffic().SetFocus(object ? object->GetID() : 0);
    return 0;
}

int TrafficRunning(lua_State *L)
{
    lua_pushboolean(L, E().GetTraffic().IsRunning());
    return 1;
}

void Register(lua_State *L, ScriptSystem &system)
{
    (void)system;

    // GameObject handles.
    luaL_newmetatable(L, kObjectMeta);
    const luaL_Reg objectMeta[] = {{"__eq", ObjectEq}, {"__tostring", ObjectToString}, {nullptr, nullptr}};
    SetFunctions(L, objectMeta);
    lua_newtable(L);
    const luaL_Reg objectMethods[] = {
        {"valid", ObjectValid},
        {"name", ObjectName},
        {"set_name", ObjectSetName},
        {"id", ObjectId},
        {"active", ObjectActive},
        {"set_active", ObjectSetActive},
        {"destroy", ObjectDestroy},
        {"parent", ObjectParent},
        {"set_parent", ObjectSetParent},
        {"children", ObjectChildren},
        {"find", ObjectFind},
        {"position", ObjectPosition},
        {"set_position", ObjectSetPosition},
        {"world_position", ObjectWorldPosition},
        {"set_world_position", ObjectSetWorldPosition},
        {"world_coords", ObjectWorldCoords},
        {"rotation", ObjectRotation},
        {"set_rotation", ObjectSetRotation},
        {"yaw", ObjectYaw},
        {"set_yaw", ObjectSetYaw},
        {"forward", ObjectForward},
        {"right", ObjectRight},
        {"scale", ObjectScale},
        {"set_scale", ObjectSetScale},
        {"get", ObjectGet},
        {"add", ObjectAdd},
        {"remove", ObjectRemove},
        {nullptr, nullptr},
    };
    SetFunctions(L, objectMethods);
    lua_setfield(L, -2, "__index");
    lua_pop(L, 1);

    // Component proxies.
    luaL_newmetatable(L, kComponentMeta);
    const luaL_Reg componentMeta[] = {
        {"__index", ComponentIndex}, {"__newindex", ComponentNewIndex}, {"__tostring", ComponentToString}, {nullptr, nullptr}
    };
    SetFunctions(L, componentMeta);
    lua_pop(L, 1);

    lua_newtable(L);
    lua_setfield(L, LUA_REGISTRYINDEX, "Geni.componentMethods");
    const luaL_Reg common[] = {{"object", ComponentObject}, {"type", ComponentType}, {"valid", ComponentValid}, {nullptr, nullptr}};
    AddMethods(L, "*", common);
    const luaL_Reg character[] = {
        {"move", CharacterMove},         {"jump", CharacterJump},     {"teleport", CharacterTeleport},
        {"set_enabled", CharacterSetEnabled}, {"enabled", CharacterEnabled}, {"speed", CharacterSpeed},
        {"grounded", CharacterGrounded}, {"velocity", CharacterVelocity}, {nullptr, nullptr},
    };
    AddMethods(L, "CharacterControllerComponent", character);
    const luaL_Reg vehicle[] = {
        {"set_input", VehicleSetInput}, {"speed", VehicleSpeed}, {"rpm", VehicleRpm}, {"gear", VehicleGear},
        {"teleport", VehicleTeleport},  {"velocity", BodyVelocity}, {nullptr, nullptr},
    };
    AddMethods(L, "VehicleComponent", vehicle);
    const luaL_Reg animator[] = {
        {"set", AnimatorSet},   {"get", AnimatorGet},       {"trigger", AnimatorTrigger},
        {"state", AnimatorState}, {"play", AnimatorPlay},   {"events", AnimatorEvents}, {nullptr, nullptr},
    };
    AddMethods(L, "AnimatorComponent", animator);
    const luaL_Reg agent[] = {
        {"go_to", AgentGoTo}, {"stop", AgentStop}, {"arrived", AgentArrived}, {"on_mesh", AgentOnMesh},
        {"speed", AgentSpeed}, {nullptr, nullptr},
    };
    AddMethods(L, "NavAgentComponent", agent);
    const luaL_Reg source[] = {{"play", SourcePlay}, {"stop", SourceStop}, {"playing", SourcePlaying}, {nullptr, nullptr}};
    AddMethods(L, "AudioSourceComponent", source);
    const luaL_Reg camera[] = {
        {"set_target", CameraSetTarget}, {"target", CameraTarget}, {"snap", CameraSnap}, {nullptr, nullptr}
    };
    AddMethods(L, "ThirdPersonCameraComponent", camera);
    const luaL_Reg physics[] = {
        {"velocity", BodyVelocity}, {"set_velocity", BodySetVelocity}, {"impulse", BodyImpulse}, {nullptr, nullptr}
    };
    AddMethods(L, "PhysicsComponent", physics);
    const luaL_Reg script[] = {{"instance", ScriptInstance}, {nullptr, nullptr}};
    AddMethods(L, "ScriptComponent", script);
    const luaL_Reg spriteAnimator[] = {
        {"play", SpriteAnimatorPlay},
        {"stop", SpriteAnimatorStop},
        {"finished", SpriteAnimatorFinished},
        {nullptr, nullptr}
    };
    AddMethods(L, "SpriteAnimatorComponent", spriteAnimator);
    const luaL_Reg tilemap[] = {
        {"tile", TilemapTile},      {"set_tile", TilemapSetTile},       {"solid", TilemapSolid},
        {"cell_at", TilemapCellAt}, {"cell_center", TilemapCellCenter}, {nullptr, nullptr},
    };
    AddMethods(L, "TilemapComponent", tilemap);
    const luaL_Reg emitter[] = {
        {"emit", EmitterEmit}, {"clear", EmitterClear}, {"restart", EmitterRestart}, {"count", EmitterCount},
        {nullptr, nullptr},
    };
    AddMethods(L, "ParticleEmitterComponent", emitter);
    const luaL_Reg weather[] = {{"snap", WeatherSnap}, {nullptr, nullptr}};
    AddMethods(L, "WeatherComponent", weather);
    const luaL_Reg timeline[] = {
        {"play", TimelinePlay},       {"pause", TimelinePause}, {"stop", TimelineStop},     {"seek", TimelineSeek},
        {"playing", TimelinePlaying}, {"time", TimelineTime},   {"events", TimelineEvents}, {nullptr, nullptr},
    };
    AddMethods(L, "TimelineComponent", timeline);

    const luaL_Reg log[] = {{"info", LogInfo}, {"warn", LogWarn}, {"error", LogError}, {nullptr, nullptr}};
    NewLibrary(L, "log", log);
    lua_pushcfunction(L, LogInfo);
    lua_setglobal(L, "print");

    const luaL_Reg input[] = {
        {"down", InputDown},         {"pressed", InputPressed},     {"released", InputReleased},
        {"value", InputValue},       {"vector", InputVector},       {"device", InputDevice},
        {"set_group", InputSetGroup}, {"capture_cursor", InputCaptureCursor}, {"override", InputOverride},
        {nullptr, nullptr},
    };
    NewLibrary(L, "input", input);

    const luaL_Reg scene[] = {
        {"find", SceneFind},           {"get", SceneGet},           {"create", SceneCreate},
        {"spawn", SceneSpawn},         {"main_camera", SceneMainCamera}, {"set_main_camera", SceneSetMainCamera},
        {"to_world", SceneToWorld},    {"to_scene", SceneToScene},  {"origin", SceneOrigin},
        {nullptr, nullptr},
    };
    NewLibrary(L, "scene", scene);

    const luaL_Reg physicsLibrary[] = {{"raycast", PhysicsRaycast}, {"spherecast", PhysicsSphereCast}, {nullptr, nullptr}};
    NewLibrary(L, "physics", physicsLibrary);

    const luaL_Reg audio[] = {
        {"play", AudioPlay},       {"stop", AudioStop},           {"playing", AudioPlaying},
        {"set_pitch", AudioSetPitch}, {"set_sound_volume", AudioSetSoundVolume}, {"move", AudioMove},
        {"music", AudioMusic},     {"stop_music", AudioStopMusic}, {"set_volume", AudioSetVolume},
        {"volume", AudioVolume},   {nullptr, nullptr},
    };
    NewLibrary(L, "audio", audio);

    const luaL_Reg nav[] = {
        {"ready", NavReady}, {"random_point", NavRandomPoint}, {"nearest", NavNearest}, {"path", NavPath}, {nullptr, nullptr}
    };
    NewLibrary(L, "nav", nav);

    const luaL_Reg ui[] = {
        {"text", UiText},         {"panel", UiPanel},     {"bar", UiBar},         {"fade", UiFade},
        {"measure", UiMeasure},   {"canvas", UiCanvas},   {"begin_menu", UiBeginMenu}, {"button", UiButton},
        {"slider", UiSlider},     {"toggle", UiToggle},   {"label", UiLabel},     {"end_menu", UiEndMenu},
        {"focus", UiFocus},       {"set_focus", UiSetFocus}, {"accent", UiAccent}, {nullptr, nullptr},
    };
    NewLibrary(L, "ui", ui);

    const luaL_Reg time[] = {{"now", TimeNow}, {"frame", TimeFrame}, {nullptr, nullptr}};
    NewLibrary(L, "time", time);

    const luaL_Reg game[] = {
        {"quit", GameQuit}, {"screenshot", GameScreenshot}, {"set_paused", GameSetPaused}, {nullptr, nullptr}
    };
    NewLibrary(L, "game", game);

    const luaL_Reg missions[] = {
        {"load", MissionsLoad},         {"start", MissionsStart},       {"fail", MissionsFail},
        {"abandon", MissionsAbandon},   {"status", MissionsStatus},     {"info", MissionsInfo},
        {"current", MissionsCurrent},   {"progress", MissionsProgress}, {"complete", MissionsComplete},
        {"active", MissionsActive},     {"list", MissionsList},         {"events", MissionsEvents},
        {"set_player", MissionsSetPlayer}, {"state", MissionsState},    {"restore", MissionsRestore},
        {nullptr, nullptr},
    };
    NewLibrary(L, "missions", missions);
    const luaL_Reg save[] = {
        {"write", SaveWrite}, {"read", SaveRead}, {"exists", SaveExists}, {"delete", SaveDelete}, {nullptr, nullptr}
    };
    NewLibrary(L, "save", save);
    const luaL_Reg traffic[] = {
        {"start", TrafficStart},          {"stop", TrafficStop},         {"stats", TrafficStats},
        {"keep_clear", TrafficKeepClear}, {"set_focus", TrafficSetFocus}, {"running", TrafficRunning},
        {nullptr, nullptr},
    };
    NewLibrary(L, "traffic", traffic);
}

} // namespace ScriptBindings
} // namespace Geni
