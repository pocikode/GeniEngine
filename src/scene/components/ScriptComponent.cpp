#include "scene/components/ScriptComponent.h"

#include "../../script/ScriptBindings.h"
#include "core/Engine.h"
#include "core/Log.h"
#include "scene/GameObject.h"
#include "scene/Reflection.h"
#include "script/ScriptSystem.h"

#include "lua.h"

namespace Geni
{

ScriptComponent::~ScriptComponent()
{
    Release();
}

void ScriptComponent::Release()
{
    if (m_instance >= 0)
    {
        ScriptSystem &scripts = Engine::GetInstance().GetScripts();
        scripts.ReleaseRef(m_instance);
        m_instance = -1;
    }
}

void ScriptComponent::SetScript(const std::string &script)
{
    if (script != m_script)
    {
        Release();
        m_started = false;
        m_failed = false;
    }
    m_script = script;
}

bool ScriptComponent::EnsureStarted()
{
    ScriptSystem &scripts = Engine::GetInstance().GetScripts();
    if (m_failed && scripts.GetReloadCount() != m_reloadSeen)
    {
        // A script was edited: try again (an instance that exists keeps its state).
        m_failed = false;
    }
    if (m_started || m_failed || !scripts.IsInitialized() || !m_owner || m_script.empty())
    {
        return m_started;
    }
    m_reloadSeen = scripts.GetReloadCount();
    if (m_instance < 0)
    {
        std::string error;
        const int metatable = scripts.GetScriptMetatable(m_script, &error);
        if (metatable < 0)
        {
            GENI_LOG_ERROR("Script on '%s' (%s) did not load", m_owner->GetName().c_str(), m_script.c_str());
            m_failed = true;
            return false;
        }
        lua_State *L = scripts.GetState();
        ScriptBindings::PushObject(L, m_owner);
        m_instance = scripts.CreateInstance(metatable);
        if (!m_params.empty())
        {
            nlohmann::json params = nlohmann::json::parse(m_params, nullptr, false);
            if (params.is_object())
            {
                lua_rawgeti(L, LUA_REGISTRYINDEX, m_instance);
                for (auto it = params.begin(); it != params.end(); ++it)
                {
                    ScriptBindings::PushJson(L, it.value());
                    lua_setfield(L, -2, it.key().c_str());
                }
                lua_pop(L, 1);
            }
            else
            {
                GENI_LOG_WARN("Script on '%s': params is not a JSON object", m_owner->GetName().c_str());
            }
        }
    }
    m_started = true;
    if (!scripts.CallMethod(m_instance, "start"))
    {
        m_failed = true;
    }
    return true;
}

void ScriptComponent::Update(float deltaTime)
{
    if (!EnsureStarted() || m_failed)
    {
        return;
    }
    if (!Engine::GetInstance().GetScripts().CallMethod(m_instance, "update", true, deltaTime))
    {
        m_failed = true;
        m_reloadSeen = Engine::GetInstance().GetScripts().GetReloadCount();
    }
}

void ScriptComponent::FixedUpdate(float fixedDeltaTime)
{
    if (!m_started || m_failed)
    {
        return;
    }
    if (!Engine::GetInstance().GetScripts().CallMethod(m_instance, "fixed_update", true, fixedDeltaTime))
    {
        m_failed = true;
        m_reloadSeen = Engine::GetInstance().GetScripts().GetReloadCount();
    }
}

void ScriptComponent::LateUpdate(float deltaTime)
{
    if (!m_started || m_failed)
    {
        return;
    }
    if (!Engine::GetInstance().GetScripts().CallMethod(m_instance, "late_update", true, deltaTime))
    {
        m_failed = true;
        m_reloadSeen = Engine::GetInstance().GetScripts().GetReloadCount();
    }
}

void ScriptComponent::OnDestroy()
{
    if (m_started && !m_failed)
    {
        Engine::GetInstance().GetScripts().CallMethod(m_instance, "destroy");
    }
    Release();
}

namespace
{
const bool s_registered = RegisterComponentType<ScriptComponent>(
    "ScriptComponent",
    {
        StringProperty<ScriptComponent>("script", &ScriptComponent::GetScript, &ScriptComponent::SetScript),
        StringProperty<ScriptComponent>("params", &ScriptComponent::GetParams, &ScriptComponent::SetParams),
    }
);
} // namespace

void LinkScriptComponent()
{
}

} // namespace Geni
