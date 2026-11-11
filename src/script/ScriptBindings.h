#pragma once

#include <nlohmann/json.hpp>

struct lua_State;

namespace Geni
{

class GameObject;
class ScriptSystem;

// The engine API seen from Lua (docs/design/0013). Internal to src/script/ and the
// script component.
namespace ScriptBindings
{
void Register(lua_State *L, ScriptSystem &system);
// Pushes a GameObject handle (nil for nullptr).
void PushObject(lua_State *L, GameObject *object);
// The object a handle at `index` refers to, or nullptr (not a handle, or destroyed).
GameObject *ToObject(lua_State *L, int index);
void PushJson(lua_State *L, const nlohmann::json &value);
nlohmann::json ToJson(lua_State *L, int index);
} // namespace ScriptBindings

} // namespace Geni
