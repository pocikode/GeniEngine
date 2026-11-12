#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <vector>

namespace Geni
{

class InputManager;

enum class ActionType
{
    Button,
    Axis
};

// Which kind of device produced the most recent input (for UI prompts).
enum class InputDevice
{
    KeyboardMouse,
    Gamepad
};

// One control an action listens to, e.g. "key:Space" or "pad:LeftX" (docs/design/0009).
struct InputBinding
{
    std::string control;
    float scale = 1.0f;
};

struct InputAction
{
    std::string name;
    ActionType type = ActionType::Button;
    // Axes: clamp the summed value to -1..1 (off for mouse look).
    bool clamp = true;
    // Actions in a disabled group read as idle (SetGroupEnabled).
    std::string group;
    std::vector<InputBinding> bindings;
};

// Named actions bound to keys, mouse and gamepads (docs/design/0009). The engine owns
// one and updates it once per frame after polling devices.
class InputActions
{
  public:
    InputActions();

    // The engine's default map: movement, camera, driving and menu actions.
    void LoadDefaults();
    // Adds or replaces actions from a JSON document {"actions": {...}}. Unknown controls
    // are reported and skipped. With `replace` the existing actions are removed first.
    bool LoadJson(const nlohmann::json &document, bool replace = false, std::string *error = nullptr);
    nlohmann::json SaveJson() const;

    void AddAction(const InputAction &action);
    bool RemoveAction(const std::string &name);
    const InputAction *FindAction(const std::string &name) const;
    std::vector<std::string> GetActionNames() const;
    // Replaces an action's bindings (rebinding from a settings menu).
    bool SetBindings(const std::string &name, const std::vector<InputBinding> &bindings);

    // Reads the devices. `keyboardMouse` false ignores keys and mouse (a tool's text
    // field has focus); `gamepads` false ignores pads.
    void Update(const InputManager &input, bool keyboardMouse = true, bool gamepads = true);

    // Buttons: held, and edges this frame. Axes: an axis is "down" when its magnitude is
    // over the press threshold.
    bool IsDown(const std::string &name) const;
    bool WasPressed(const std::string &name) const;
    bool WasReleased(const std::string &name) const;
    // The action's value: 0..1 for buttons, the summed axis for axes. 0 when unknown.
    float GetValue(const std::string &name) const;
    // Two axes as a vector whose length is at most 1.
    void GetVector(const std::string &xAction, const std::string &yAction, float &x, float &y) const;

    // Forces an action's value regardless of devices until cleared: replays, scripted
    // test runs (the city autopilot) and AI drivers use the same path as a player.
    void SetOverride(const std::string &name, float value);
    void ClearOverride(const std::string &name);
    void ClearOverrides();

    void SetGroupEnabled(const std::string &group, bool enabled);
    bool IsGroupEnabled(const std::string &group) const;

    InputDevice GetLastDevice() const
    {
        return m_lastDevice;
    }

    float GetPressThreshold() const
    {
        return m_pressThreshold;
    }
    void SetPressThreshold(float threshold)
    {
        m_pressThreshold = threshold;
    }
    float GetDeadZone() const
    {
        return m_deadZone;
    }
    void SetDeadZone(float deadZone)
    {
        m_deadZone = deadZone;
    }

    // True when `control` names something this system can read.
    static bool IsValidControl(const std::string &control);

  private:
    struct State
    {
        float value = 0.0f;
        bool down = false;
        bool previousDown = false;
    };

    float ReadControl(const InputManager &input, const std::string &control, bool keyboardMouse, bool gamepads,
                      bool &fromPad) const;

    std::vector<InputAction> m_actions;
    std::unordered_map<std::string, size_t> m_index;
    std::unordered_map<std::string, State> m_states;
    std::unordered_map<std::string, bool> m_disabledGroups;
    std::unordered_map<std::string, float> m_overrides;
    InputDevice m_lastDevice = InputDevice::KeyboardMouse;
    float m_pressThreshold = 0.5f;
    float m_deadZone = 0.15f;
};

} // namespace Geni
