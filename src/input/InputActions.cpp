#include "input/InputActions.h"

#include "core/Log.h"
#include "input/InputManager.h"

#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace Geni
{

namespace
{

struct NamedCode
{
    const char *name;
    int code;
};

// GLFW key names accepted after "key:". Letters and digits are handled separately.
const NamedCode kKeys[] = {
    {"Space", GLFW_KEY_SPACE},
    {"Apostrophe", GLFW_KEY_APOSTROPHE},
    {"Comma", GLFW_KEY_COMMA},
    {"Minus", GLFW_KEY_MINUS},
    {"Period", GLFW_KEY_PERIOD},
    {"Slash", GLFW_KEY_SLASH},
    {"Semicolon", GLFW_KEY_SEMICOLON},
    {"Equal", GLFW_KEY_EQUAL},
    {"LeftBracket", GLFW_KEY_LEFT_BRACKET},
    {"Backslash", GLFW_KEY_BACKSLASH},
    {"RightBracket", GLFW_KEY_RIGHT_BRACKET},
    {"GraveAccent", GLFW_KEY_GRAVE_ACCENT},
    {"Escape", GLFW_KEY_ESCAPE},
    {"Enter", GLFW_KEY_ENTER},
    {"Tab", GLFW_KEY_TAB},
    {"Backspace", GLFW_KEY_BACKSPACE},
    {"Insert", GLFW_KEY_INSERT},
    {"Delete", GLFW_KEY_DELETE},
    {"Right", GLFW_KEY_RIGHT},
    {"Left", GLFW_KEY_LEFT},
    {"Down", GLFW_KEY_DOWN},
    {"Up", GLFW_KEY_UP},
    {"PageUp", GLFW_KEY_PAGE_UP},
    {"PageDown", GLFW_KEY_PAGE_DOWN},
    {"Home", GLFW_KEY_HOME},
    {"End", GLFW_KEY_END},
    {"F1", GLFW_KEY_F1},
    {"F2", GLFW_KEY_F2},
    {"F3", GLFW_KEY_F3},
    {"F4", GLFW_KEY_F4},
    {"F5", GLFW_KEY_F5},
    {"F6", GLFW_KEY_F6},
    {"F7", GLFW_KEY_F7},
    {"F8", GLFW_KEY_F8},
    {"F9", GLFW_KEY_F9},
    {"F10", GLFW_KEY_F10},
    {"F11", GLFW_KEY_F11},
    {"F12", GLFW_KEY_F12},
    {"LeftShift", GLFW_KEY_LEFT_SHIFT},
    {"LeftControl", GLFW_KEY_LEFT_CONTROL},
    {"LeftAlt", GLFW_KEY_LEFT_ALT},
    {"RightShift", GLFW_KEY_RIGHT_SHIFT},
    {"RightControl", GLFW_KEY_RIGHT_CONTROL},
    {"RightAlt", GLFW_KEY_RIGHT_ALT},
};

const NamedCode kMouseButtons[] = {
    {"Left", GLFW_MOUSE_BUTTON_LEFT},
    {"Right", GLFW_MOUSE_BUTTON_RIGHT},
    {"Middle", GLFW_MOUSE_BUTTON_MIDDLE},
};

const NamedCode kPadButtons[] = {
    {"A", static_cast<int>(GamepadButton::A)},
    {"B", static_cast<int>(GamepadButton::B)},
    {"X", static_cast<int>(GamepadButton::X)},
    {"Y", static_cast<int>(GamepadButton::Y)},
    {"LB", static_cast<int>(GamepadButton::LeftBumper)},
    {"RB", static_cast<int>(GamepadButton::RightBumper)},
    {"Back", static_cast<int>(GamepadButton::Back)},
    {"Start", static_cast<int>(GamepadButton::Start)},
    {"Guide", static_cast<int>(GamepadButton::Guide)},
    {"LS", static_cast<int>(GamepadButton::LeftThumb)},
    {"RS", static_cast<int>(GamepadButton::RightThumb)},
    {"DpadUp", static_cast<int>(GamepadButton::DpadUp)},
    {"DpadRight", static_cast<int>(GamepadButton::DpadRight)},
    {"DpadDown", static_cast<int>(GamepadButton::DpadDown)},
    {"DpadLeft", static_cast<int>(GamepadButton::DpadLeft)},
};

const NamedCode kPadAxes[] = {
    {"LeftX", static_cast<int>(GamepadAxis::LeftX)},
    {"LeftY", static_cast<int>(GamepadAxis::LeftY)},
    {"RightX", static_cast<int>(GamepadAxis::RightX)},
    {"RightY", static_cast<int>(GamepadAxis::RightY)},
    {"LT", static_cast<int>(GamepadAxis::LeftTrigger)},
    {"RT", static_cast<int>(GamepadAxis::RightTrigger)},
};

template <size_t N> int Lookup(const NamedCode (&table)[N], const std::string &name)
{
    for (const NamedCode &entry : table)
    {
        if (name == entry.name)
        {
            return entry.code;
        }
    }
    return -1;
}

int KeyCode(const std::string &name)
{
    if (name.size() == 1)
    {
        const char c = name[0];
        if (c >= 'A' && c <= 'Z')
        {
            return GLFW_KEY_A + (c - 'A');
        }
        if (c >= 'a' && c <= 'z')
        {
            return GLFW_KEY_A + (c - 'a');
        }
        if (c >= '0' && c <= '9')
        {
            return GLFW_KEY_0 + (c - '0');
        }
    }
    return Lookup(kKeys, name);
}

// Splits "device:name".
bool SplitControl(const std::string &control, std::string &device, std::string &name)
{
    const size_t colon = control.find(':');
    if (colon == std::string::npos)
    {
        return false;
    }
    device = control.substr(0, colon);
    name = control.substr(colon + 1);
    return !device.empty() && !name.empty();
}

float ApplyDeadZone(float value, float deadZone)
{
    const float magnitude = std::fabs(value);
    if (magnitude <= deadZone)
    {
        return 0.0f;
    }
    // Rescale so the output starts at 0 just outside the dead zone and still reaches 1.
    const float scaled = (magnitude - deadZone) / (1.0f - deadZone);
    return std::copysign(std::min(scaled, 1.0f), value);
}

InputBinding BindingFromJson(const nlohmann::json &value)
{
    InputBinding binding;
    if (value.is_string())
    {
        binding.control = value.get<std::string>();
    }
    else if (value.is_object())
    {
        binding.control = value.value("control", std::string());
        binding.scale = value.value("scale", 1.0f);
    }
    return binding;
}

} // namespace

InputActions::InputActions()
{
    LoadDefaults();
}

bool InputActions::IsValidControl(const std::string &control)
{
    std::string device;
    std::string name;
    if (!SplitControl(control, device, name))
    {
        return false;
    }
    if (device == "key")
    {
        return KeyCode(name) >= 0;
    }
    if (device == "mouse")
    {
        return Lookup(kMouseButtons, name) >= 0 || name == "DeltaX" || name == "DeltaY" || name == "Wheel";
    }
    if (device == "pad")
    {
        return Lookup(kPadButtons, name) >= 0 || Lookup(kPadAxes, name) >= 0;
    }
    return false;
}

void InputActions::LoadDefaults()
{
    const nlohmann::json defaults = nlohmann::json::parse(R"({
      "actions": {
        "move_x":    {"type": "axis", "group": "gameplay", "bindings": [{"control": "key:D"}, {"control": "key:A", "scale": -1}, "pad:LeftX"]},
        "move_y":    {"type": "axis", "group": "gameplay", "bindings": [{"control": "key:W"}, {"control": "key:S", "scale": -1}, {"control": "pad:LeftY", "scale": -1}]},
        "look_x":    {"type": "axis", "group": "gameplay", "clamp": false, "bindings": [{"control": "mouse:DeltaX", "scale": 0.15}, {"control": "pad:RightX", "scale": 2.5}]},
        "look_y":    {"type": "axis", "group": "gameplay", "clamp": false, "bindings": [{"control": "mouse:DeltaY", "scale": 0.15}, {"control": "pad:RightY", "scale": 2.0}]},
        "zoom":      {"type": "axis", "group": "gameplay", "clamp": false, "bindings": ["mouse:Wheel"]},
        "jump":      {"type": "button", "group": "gameplay", "bindings": ["key:Space", "pad:A"]},
        "sprint":    {"type": "button", "group": "gameplay", "bindings": ["key:LeftShift", "pad:LS", "pad:B"]},
        "interact":  {"type": "button", "group": "gameplay", "bindings": ["key:E", "key:F", "pad:Y"]},
        "throttle":  {"type": "axis", "group": "gameplay", "bindings": ["key:W", "key:Up", "pad:RT"]},
        "brake":     {"type": "axis", "group": "gameplay", "bindings": ["key:S", "key:Down", "pad:LT"]},
        "steer":     {"type": "axis", "group": "gameplay", "bindings": [{"control": "key:D"}, {"control": "key:Right"}, {"control": "key:A", "scale": -1}, {"control": "key:Left", "scale": -1}, "pad:LeftX"]},
        "handbrake": {"type": "button", "group": "gameplay", "bindings": ["key:Space", "pad:X"]},
        "horn":      {"type": "button", "group": "gameplay", "bindings": ["key:H", "pad:RS"]},
        "pause":     {"type": "button", "bindings": ["key:Escape", "key:P", "pad:Start"]},
        "ui_up":     {"type": "button", "group": "ui", "bindings": ["key:Up", "key:W", "pad:DpadUp", {"control": "pad:LeftY", "scale": -1}]},
        "ui_down":   {"type": "button", "group": "ui", "bindings": ["key:Down", "key:S", "pad:DpadDown", "pad:LeftY"]},
        "ui_left":   {"type": "button", "group": "ui", "bindings": ["key:Left", "key:A", "pad:DpadLeft", {"control": "pad:LeftX", "scale": -1}]},
        "ui_right":  {"type": "button", "group": "ui", "bindings": ["key:Right", "key:D", "pad:DpadRight", "pad:LeftX"]},
        "ui_accept": {"type": "button", "group": "ui", "bindings": ["key:Enter", "key:Space", "pad:A"]},
        "ui_back":   {"type": "button", "group": "ui", "bindings": ["key:Escape", "key:Backspace", "pad:B"]}
      }
    })");
    LoadJson(defaults, true);
}

bool InputActions::LoadJson(const nlohmann::json &document, bool replace, std::string *error)
{
    auto actions = document.find("actions");
    if (actions == document.end() || !actions->is_object())
    {
        if (error)
        {
            *error = "input actions: expected an \"actions\" object";
        }
        return false;
    }
    if (replace)
    {
        m_actions.clear();
        m_index.clear();
        m_states.clear();
    }
    bool ok = true;
    for (auto it = actions->begin(); it != actions->end(); ++it)
    {
        const nlohmann::json &value = it.value();
        InputAction action;
        action.name = it.key();
        const std::string type = value.value("type", std::string("button"));
        if (type == "axis")
        {
            action.type = ActionType::Axis;
        }
        else if (type != "button")
        {
            GENI_LOG_WARN("input actions: '%s' has unknown type '%s'", action.name.c_str(), type.c_str());
            ok = false;
            continue;
        }
        action.clamp = value.value("clamp", true);
        action.group = value.value("group", std::string());
        if (auto bindings = value.find("bindings"); bindings != value.end() && bindings->is_array())
        {
            for (const auto &entry : *bindings)
            {
                InputBinding binding = BindingFromJson(entry);
                if (!IsValidControl(binding.control))
                {
                    GENI_LOG_WARN(
                        "input actions: '%s' has unknown control '%s'", action.name.c_str(), binding.control.c_str()
                    );
                    ok = false;
                    continue;
                }
                action.bindings.push_back(binding);
            }
        }
        AddAction(action);
    }
    if (!ok && error)
    {
        *error = "input actions: some bindings were skipped (see the log)";
    }
    return ok;
}

nlohmann::json InputActions::SaveJson() const
{
    nlohmann::json actions = nlohmann::json::object();
    for (const InputAction &action : m_actions)
    {
        nlohmann::json entry;
        entry["type"] = action.type == ActionType::Axis ? "axis" : "button";
        if (!action.clamp)
        {
            entry["clamp"] = false;
        }
        if (!action.group.empty())
        {
            entry["group"] = action.group;
        }
        nlohmann::json bindings = nlohmann::json::array();
        for (const InputBinding &binding : action.bindings)
        {
            if (binding.scale == 1.0f)
            {
                bindings.push_back(binding.control);
            }
            else
            {
                bindings.push_back({{"control", binding.control}, {"scale", binding.scale}});
            }
        }
        entry["bindings"] = bindings;
        actions[action.name] = entry;
    }
    return {{"actions", actions}};
}

void InputActions::AddAction(const InputAction &action)
{
    auto it = m_index.find(action.name);
    if (it != m_index.end())
    {
        m_actions[it->second] = action;
        return;
    }
    m_index[action.name] = m_actions.size();
    m_actions.push_back(action);
}

bool InputActions::RemoveAction(const std::string &name)
{
    auto it = m_index.find(name);
    if (it == m_index.end())
    {
        return false;
    }
    m_actions.erase(m_actions.begin() + static_cast<std::ptrdiff_t>(it->second));
    m_states.erase(name);
    m_index.clear();
    for (size_t i = 0; i < m_actions.size(); ++i)
    {
        m_index[m_actions[i].name] = i;
    }
    return true;
}

const InputAction *InputActions::FindAction(const std::string &name) const
{
    auto it = m_index.find(name);
    return it == m_index.end() ? nullptr : &m_actions[it->second];
}

std::vector<std::string> InputActions::GetActionNames() const
{
    std::vector<std::string> names;
    names.reserve(m_actions.size());
    for (const InputAction &action : m_actions)
    {
        names.push_back(action.name);
    }
    return names;
}

bool InputActions::SetBindings(const std::string &name, const std::vector<InputBinding> &bindings)
{
    auto it = m_index.find(name);
    if (it == m_index.end())
    {
        return false;
    }
    m_actions[it->second].bindings.clear();
    for (const InputBinding &binding : bindings)
    {
        if (IsValidControl(binding.control))
        {
            m_actions[it->second].bindings.push_back(binding);
        }
    }
    return true;
}

float InputActions::ReadControl(
    const InputManager &input, const std::string &control, bool keyboardMouse, bool gamepads, bool &fromPad
) const
{
    std::string device;
    std::string name;
    if (!SplitControl(control, device, name))
    {
        return 0.0f;
    }
    if (device == "key")
    {
        return keyboardMouse && input.IsKeyPressed(KeyCode(name)) ? 1.0f : 0.0f;
    }
    if (device == "mouse")
    {
        if (!keyboardMouse)
        {
            return 0.0f;
        }
        if (name == "DeltaX")
        {
            return input.GetMouseDelta().x;
        }
        if (name == "DeltaY")
        {
            return input.GetMouseDelta().y;
        }
        if (name == "Wheel")
        {
            return input.GetScrollDelta();
        }
        return input.IsMouseButtonPressed(Lookup(kMouseButtons, name)) ? 1.0f : 0.0f;
    }
    if (device == "pad" && gamepads)
    {
        const int pad = input.GetActiveGamepad();
        if (pad < 0)
        {
            return 0.0f;
        }
        const GamepadState &state = input.GetGamepad(pad);
        fromPad = true;
        const int button = Lookup(kPadButtons, name);
        if (button >= 0)
        {
            return state.buttons[static_cast<size_t>(button)] ? 1.0f : 0.0f;
        }
        const int axis = Lookup(kPadAxes, name);
        if (axis >= 0)
        {
            const float raw = state.axes[static_cast<size_t>(axis)];
            if (axis == static_cast<int>(GamepadAxis::LeftTrigger) || axis == static_cast<int>(GamepadAxis::RightTrigger))
            {
                // GLFW reports triggers as -1 (released) .. 1 (pressed).
                return ApplyDeadZone(0.5f * (raw + 1.0f), m_deadZone * 0.5f);
            }
            return ApplyDeadZone(raw, m_deadZone);
        }
    }
    return 0.0f;
}

void InputActions::Update(const InputManager &input, bool keyboardMouse, bool gamepads)
{
    for (const InputAction &action : m_actions)
    {
        State &state = m_states[action.name];
        state.previousDown = state.down;
        const bool enabled = action.group.empty() || IsGroupEnabled(action.group);
        float value = 0.0f;
        if (enabled)
        {
            for (const InputBinding &binding : action.bindings)
            {
                bool fromPad = false;
                const float raw = ReadControl(input, binding.control, keyboardMouse, gamepads, fromPad);
                // A clear press or push switches the device prompts; stick noise does not.
                if (std::fabs(raw) >= m_pressThreshold)
                {
                    m_lastDevice = fromPad ? InputDevice::Gamepad : InputDevice::KeyboardMouse;
                }
                if (action.type == ActionType::Button)
                {
                    // Only the bound direction of an analogue control presses a button:
                    // "ui_up" on -LeftY must not fire when the stick is pushed down.
                    value = std::max(value, raw * binding.scale);
                }
                else
                {
                    value += raw * binding.scale;
                }
            }
        }
        if (auto it = m_overrides.find(action.name); it != m_overrides.end())
        {
            value = it->second;
        }
        if (action.type == ActionType::Axis && action.clamp)
        {
            value = std::clamp(value, -1.0f, 1.0f);
        }
        if (action.type == ActionType::Button)
        {
            value = std::min(value, 1.0f);
        }
        state.value = value;
        state.down = std::fabs(value) >= m_pressThreshold;
    }
}

bool InputActions::IsDown(const std::string &name) const
{
    auto it = m_states.find(name);
    return it != m_states.end() && it->second.down;
}

bool InputActions::WasPressed(const std::string &name) const
{
    auto it = m_states.find(name);
    return it != m_states.end() && it->second.down && !it->second.previousDown;
}

bool InputActions::WasReleased(const std::string &name) const
{
    auto it = m_states.find(name);
    return it != m_states.end() && !it->second.down && it->second.previousDown;
}

float InputActions::GetValue(const std::string &name) const
{
    auto it = m_states.find(name);
    return it == m_states.end() ? 0.0f : it->second.value;
}

void InputActions::GetVector(const std::string &xAction, const std::string &yAction, float &x, float &y) const
{
    x = GetValue(xAction);
    y = GetValue(yAction);
    const float length = std::sqrt(x * x + y * y);
    if (length > 1.0f)
    {
        x /= length;
        y /= length;
    }
}

void InputActions::SetOverride(const std::string &name, float value)
{
    m_overrides[name] = value;
}

void InputActions::ClearOverride(const std::string &name)
{
    m_overrides.erase(name);
}

void InputActions::ClearOverrides()
{
    m_overrides.clear();
}

void InputActions::SetGroupEnabled(const std::string &group, bool enabled)
{
    if (enabled)
    {
        m_disabledGroups.erase(group);
    }
    else
    {
        m_disabledGroups[group] = true;
    }
}

bool InputActions::IsGroupEnabled(const std::string &group) const
{
    return m_disabledGroups.find(group) == m_disabledGroups.end();
}

} // namespace Geni
