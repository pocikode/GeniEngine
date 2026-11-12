#include "script/ScriptSystem.h"

#include "ScriptBindings.h"
#include "core/Log.h"
#include "io/FileSystem.h"

// Lua is compiled as C++ (docs/design/0013): no extern "C" around its headers, and errors
// unwind as exceptions.
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"

#include <algorithm>

namespace Geni
{

namespace
{

// vec3 and small math helpers, in Lua: arithmetic on tables is cheap enough here and
// keeps the C++ side to plain tables (docs/design/0013).
const char *kPrelude = R"LUA(
local V = {}
V.__index = V
local function new(x, y, z)
  return setmetatable({ x = x or 0.0, y = y or 0.0, z = z or 0.0 }, V)
end
V.__add = function(a, b) return new(a.x + b.x, a.y + b.y, a.z + b.z) end
V.__sub = function(a, b) return new(a.x - b.x, a.y - b.y, a.z - b.z) end
V.__unm = function(a) return new(-a.x, -a.y, -a.z) end
V.__mul = function(a, b)
  if type(a) == "number" then return new(a * b.x, a * b.y, a * b.z) end
  if type(b) == "number" then return new(a.x * b, a.y * b, a.z * b) end
  return new(a.x * b.x, a.y * b.y, a.z * b.z)
end
V.__div = function(a, b) return new(a.x / b, a.y / b, a.z / b) end
V.__eq = function(a, b) return a.x == b.x and a.y == b.y and a.z == b.z end
V.__tostring = function(v) return string.format("(%.3f, %.3f, %.3f)", v.x, v.y, v.z) end
function V.length(v) return math.sqrt(v.x * v.x + v.y * v.y + v.z * v.z) end
function V.length2(v) return v.x * v.x + v.y * v.y + v.z * v.z end
function V.normalized(v)
  local l = V.length(v)
  if l < 1e-9 then return new(0, 0, 0) end
  return new(v.x / l, v.y / l, v.z / l)
end
function V.dot(a, b) return a.x * b.x + a.y * b.y + a.z * b.z end
function V.cross(a, b) return new(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x) end
function V.distance(a, b) return V.length(a - b) end
function V.lerp(a, b, t) return new(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t) end
function V.flat(v) return new(v.x, 0.0, v.z) end
function V.copy(v) return new(v.x, v.y, v.z) end
vec3 = setmetatable({ new = new, dot = V.dot, cross = V.cross, distance = V.distance, lerp = V.lerp,
                      copy = V.copy, length = V.length, normalized = V.normalized, flat = V.flat },
  { __call = function(_, x, y, z) return new(x, y, z) end })
function math.clamp(v, lo, hi) if v < lo then return lo elseif v > hi then return hi end return v end
function math.lerp(a, b, t) return a + (b - a) * t end
function math.sign(v) if v > 0 then return 1 elseif v < 0 then return -1 end return 0 end
return V
)LUA";

int Traceback(lua_State *L)
{
    const char *message = lua_tostring(L, 1);
    if (!message)
    {
        message = luaL_tolstring(L, 1, nullptr);
    }
    luaL_traceback(L, L, message, 1);
    return 1;
}

// require("name"): scripts/name.lua (dots become folders), loaded once.
int Require(lua_State *L)
{
    const std::string name = luaL_checkstring(L, 1);
    lua_getfield(L, LUA_REGISTRYINDEX, "Geni.modules");
    lua_getfield(L, -1, name.c_str());
    if (!lua_isnil(L, -1))
    {
        return 1;
    }
    lua_pop(L, 1);
    std::string path = name;
    std::replace(path.begin(), path.end(), '.', '/');
    path = "scripts/" + path + ".lua";
    auto *system = static_cast<ScriptSystem *>(lua_touserdata(L, lua_upvalueindex(1)));
    const std::string text = system->GetFileSystem() ? system->GetFileSystem()->LoadAssetFileText(path) : std::string();
    if (text.empty())
    {
        return luaL_error(L, "module '%s' not found (%s)", name.c_str(), path.c_str());
    }
    const std::string chunk = "@" + path;
    if (luaL_loadbuffer(L, text.data(), text.size(), chunk.c_str()) != LUA_OK)
    {
        return lua_error(L);
    }
    lua_call(L, 0, 1);
    if (lua_isnil(L, -1))
    {
        lua_pop(L, 1);
        lua_pushboolean(L, 1);
    }
    lua_pushvalue(L, -1);
    lua_setfield(L, -3, name.c_str());
    return 1;
}

} // namespace

ScriptSystem::ScriptSystem() = default;

ScriptSystem::~ScriptSystem()
{
    Shutdown();
}

bool ScriptSystem::Init(const ScriptConfig &config, FileSystem *fileSystem)
{
    Shutdown();
    if (!config.enabled)
    {
        return false;
    }
    m_config = config;
    m_fileSystem = fileSystem;
    m_state = luaL_newstate();
    if (!m_state)
    {
        GENI_LOG_ERROR("Lua: could not create a state");
        return false;
    }
    lua_State *L = m_state;

    // The safe standard libraries only: no io, no package loading, no debug.
    const luaL_Reg libraries[] = {
        {LUA_GNAME, luaopen_base},          {LUA_TABLIBNAME, luaopen_table}, {LUA_STRLIBNAME, luaopen_string},
        {LUA_MATHLIBNAME, luaopen_math},    {LUA_UTF8LIBNAME, luaopen_utf8}, {LUA_COLIBNAME, luaopen_coroutine},
        {LUA_OSLIBNAME, luaopen_os},
    };
    for (const luaL_Reg &library : libraries)
    {
        luaL_requiref(L, library.name, library.func, 1);
        lua_pop(L, 1);
    }
    // Of os, keep the clock and the time.
    lua_getglobal(L, "os");
    for (const char *unsafe : {"execute", "exit", "getenv", "remove", "rename", "tmpname", "setlocale"})
    {
        lua_pushnil(L);
        lua_setfield(L, -2, unsafe);
    }
    lua_pop(L, 1);
    for (const char *unsafe : {"dofile", "loadfile"})
    {
        lua_pushnil(L);
        lua_setglobal(L, unsafe);
    }

    lua_newtable(L);
    lua_setfield(L, LUA_REGISTRYINDEX, "Geni.modules");
    lua_pushlightuserdata(L, this);
    lua_pushcclosure(L, Require, 1);
    lua_setglobal(L, "require");

    // The prelude returns vec3's metatable; bindings give it to every vector they push.
    if (luaL_loadbuffer(L, kPrelude, std::char_traits<char>::length(kPrelude), "=prelude") != LUA_OK ||
        lua_pcall(L, 0, 1, 0) != LUA_OK)
    {
        GENI_LOG_ERROR("Lua prelude: %s", lua_tostring(L, -1));
        lua_close(L);
        m_state = nullptr;
        return false;
    }
    lua_setfield(L, LUA_REGISTRYINDEX, "Geni.vec3");

    ScriptBindings::Register(L, *this);
    GENI_LOG_INFO("Lua: %s ready", LUA_RELEASE);
    return true;
}

void ScriptSystem::Shutdown()
{
    if (m_state)
    {
        lua_close(m_state);
        m_state = nullptr;
    }
    m_files.clear();
}

void ScriptSystem::ReportError(const std::string &message)
{
    ++m_errorCount;
    GENI_LOG_ERROR("Lua: %s", message.c_str());
}

bool ScriptSystem::Run(const std::string &code, const std::string &chunkName, std::string *error)
{
    if (!m_state)
    {
        return false;
    }
    lua_State *L = m_state;
    const int top = lua_gettop(L);
    lua_pushcfunction(L, Traceback);
    const std::string chunk = "=" + chunkName;
    bool ok = luaL_loadbuffer(L, code.data(), code.size(), chunk.c_str()) == LUA_OK &&
              lua_pcall(L, 0, 0, top + 1) == LUA_OK;
    if (!ok)
    {
        const std::string message = lua_tostring(L, -1) ? lua_tostring(L, -1) : "unknown error";
        ReportError(message);
        if (error)
        {
            *error = message;
        }
    }
    lua_settop(L, top);
    return ok;
}

bool ScriptSystem::Evaluate(const std::string &code, std::string &result, std::string *error)
{
    if (!m_state)
    {
        return false;
    }
    lua_State *L = m_state;
    const int top = lua_gettop(L);
    lua_pushcfunction(L, Traceback);
    const std::string source = "return " + code;
    bool ok = luaL_loadbuffer(L, source.data(), source.size(), "=evaluate") == LUA_OK &&
              lua_pcall(L, 0, 1, top + 1) == LUA_OK;
    if (ok)
    {
        result = luaL_tolstring(L, -1, nullptr);
    }
    else
    {
        const std::string message = lua_tostring(L, -1) ? lua_tostring(L, -1) : "unknown error";
        ReportError(message);
        if (error)
        {
            *error = message;
        }
    }
    lua_settop(L, top);
    return ok;
}

bool ScriptSystem::LoadFile(ScriptFile &file, std::string *error)
{
    lua_State *L = m_state;
    auto fail = [&](const std::string &message) {
        file.failed = true;
        file.error = message;
        ReportError(message);
        if (error)
        {
            *error = message;
        }
        return false;
    };
    if (m_fileSystem)
    {
        std::error_code ec;
        const auto resolved = m_fileSystem->ResolveAssetPath(file.path);
        file.modified = std::filesystem::last_write_time(resolved, ec);
    }
    const std::string text = m_fileSystem ? m_fileSystem->LoadAssetFileText(file.path) : std::string();
    if (text.empty())
    {
        return fail(file.path + ": file not found or empty");
    }
    const int top = lua_gettop(L);
    lua_pushcfunction(L, Traceback);
    const std::string chunk = "@" + file.path;
    if (luaL_loadbuffer(L, text.data(), text.size(), chunk.c_str()) != LUA_OK ||
        lua_pcall(L, 0, 1, top + 1) != LUA_OK)
    {
        const std::string message = lua_tostring(L, -1) ? lua_tostring(L, -1) : "unknown error";
        lua_settop(L, top);
        return fail(message);
    }
    if (!lua_istable(L, -1))
    {
        lua_settop(L, top);
        return fail(file.path + ": a script must return a table (its class)");
    }
    // One metatable per file, shared by every instance: a reload only swaps its __index.
    if (file.metatableRef < 0)
    {
        lua_newtable(L);
        file.metatableRef = luaL_ref(L, LUA_REGISTRYINDEX);
    }
    lua_rawgeti(L, LUA_REGISTRYINDEX, file.metatableRef);
    lua_pushvalue(L, -2);
    lua_setfield(L, -2, "__index");
    lua_settop(L, top);
    file.failed = false;
    file.error.clear();
    return true;
}

int ScriptSystem::GetScriptMetatable(const std::string &path, std::string *error)
{
    if (!m_state || path.empty())
    {
        if (error)
        {
            *error = path.empty() ? "no script path" : "scripting is not initialised";
        }
        return -1;
    }
    auto it = m_files.find(path);
    if (it == m_files.end())
    {
        ScriptFile file;
        file.path = path;
        it = m_files.emplace(path, std::move(file)).first;
        LoadFile(it->second, error);
    }
    if (it->second.failed)
    {
        if (error)
        {
            *error = it->second.error;
        }
        return -1;
    }
    return it->second.metatableRef;
}

int ScriptSystem::ReloadChanged()
{
    if (!m_state || !m_fileSystem)
    {
        return 0;
    }
    int reloaded = 0;
    for (auto &[path, file] : m_files)
    {
        std::error_code ec;
        const auto modified = std::filesystem::last_write_time(m_fileSystem->ResolveAssetPath(path), ec);
        if (ec || modified == file.modified)
        {
            continue;
        }
        if (LoadFile(file, nullptr))
        {
            GENI_LOG_INFO("Lua: reloaded %s", path.c_str());
        }
        ++reloaded;
    }
    if (reloaded > 0)
    {
        // Modules loaded with require are re-read on their next require.
        lua_newtable(m_state);
        lua_setfield(m_state, LUA_REGISTRYINDEX, "Geni.modules");
        ++m_reloadCount;
    }
    return reloaded;
}

void ScriptSystem::Update(float deltaTime)
{
    if (!m_state || !m_config.hotReload)
    {
        return;
    }
    m_sinceCheck += deltaTime;
    if (m_sinceCheck >= m_config.hotReloadInterval)
    {
        m_sinceCheck = 0.0;
        ReloadChanged();
    }
}

int ScriptSystem::CreateInstance(int metatableRef)
{
    lua_State *L = m_state;
    // Stack: ... object
    lua_newtable(L);
    lua_insert(L, -2);
    lua_setfield(L, -2, "object");
    lua_rawgeti(L, LUA_REGISTRYINDEX, metatableRef);
    lua_setmetatable(L, -2);
    return luaL_ref(L, LUA_REGISTRYINDEX);
}

void ScriptSystem::ReleaseRef(int ref)
{
    if (m_state && ref >= 0)
    {
        luaL_unref(m_state, LUA_REGISTRYINDEX, ref);
    }
}

bool ScriptSystem::CallMethod(int instanceRef, const char *name, bool hasNumber, double number)
{
    if (!m_state || instanceRef < 0)
    {
        return false;
    }
    lua_State *L = m_state;
    const int top = lua_gettop(L);
    lua_pushcfunction(L, Traceback);
    lua_rawgeti(L, LUA_REGISTRYINDEX, instanceRef);
    lua_getfield(L, -1, name);
    if (!lua_isfunction(L, -1))
    {
        lua_settop(L, top);
        return true;
    }
    lua_insert(L, -2); // function, self
    int arguments = 1;
    if (hasNumber)
    {
        lua_pushnumber(L, number);
        ++arguments;
    }
    const bool ok = lua_pcall(L, arguments, 0, top + 1) == LUA_OK;
    if (!ok)
    {
        ReportError(lua_tostring(L, -1) ? lua_tostring(L, -1) : "unknown error");
    }
    lua_settop(L, top);
    return ok;
}

} // namespace Geni
