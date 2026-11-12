#pragma once

#include "scene/Component.h"
#include <cstdint>
#include <string>

namespace Geni
{

// Runs a Lua script on its object (docs/design/0013). The script file (a path in the
// assets folder) returns a class table; this component makes an instance of it with
// `self.object` set, fills it from `params` (a JSON object), and calls its optional
// start, update(dt), fixed_update(dt), late_update(dt) and destroy methods.
class ScriptComponent : public Component
{
    COMPONENT(ScriptComponent)

  public:
    ~ScriptComponent() override;
    void Update(float deltaTime) override;
    void FixedUpdate(float fixedDeltaTime) override;
    bool HasFixedUpdate() const override
    {
        return true;
    }
    void LateUpdate(float deltaTime) override;
    bool HasLateUpdate() const override
    {
        return true;
    }
    void OnDestroy() override;

    std::string GetScript() const
    {
        return m_script;
    }
    void SetScript(const std::string &script);
    std::string GetParams() const
    {
        return m_params;
    }
    void SetParams(const std::string &params)
    {
        m_params = params;
    }

    // Registry reference of the instance table (-1 before start): bindings push it so
    // scripts can talk to each other.
    int GetInstanceRef() const
    {
        return m_instance;
    }
    bool HasFailed() const
    {
        return m_failed;
    }
    // Creates the instance and runs start now, if that has not happened yet.
    bool EnsureStarted();

  private:
    void Release();

    std::string m_script;
    std::string m_params;
    int m_instance = -1;
    bool m_started = false;
    bool m_failed = false;
    uint32_t m_reloadSeen = 0;
};

} // namespace Geni
