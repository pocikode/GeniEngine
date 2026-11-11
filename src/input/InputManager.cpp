#include "input/InputManager.h"

#include <GLFW/glfw3.h>

namespace Geni
{

namespace
{
template <size_t N> bool Get(const std::array<bool, N> &values, int index)
{
    return index >= 0 && index < static_cast<int>(N) && values[static_cast<size_t>(index)];
}

const GamepadState s_disconnected{};
} // namespace

void InputManager::SetKeyPressed(int key, bool pressed)
{
    if (key >= 0 && key < MaxKeys)
    {
        m_keys[static_cast<size_t>(key)] = pressed;
    }
}

bool InputManager::IsKeyPressed(int key) const
{
    return Get(m_keys, key);
}

bool InputManager::WasKeyPressed(int key) const
{
    return Get(m_keys, key) && !Get(m_previousKeys, key);
}

bool InputManager::WasKeyReleased(int key) const
{
    return !Get(m_keys, key) && Get(m_previousKeys, key);
}

void InputManager::SetMouseButtonPressed(int button, bool pressed)
{
    if (button >= 0 && button < static_cast<int>(m_mouseKeys.size()))
    {
        m_mouseKeys[static_cast<size_t>(button)] = pressed;
    }
}

bool InputManager::IsMouseButtonPressed(int button) const
{
    return Get(m_mouseKeys, button);
}

bool InputManager::WasMouseButtonPressed(int button) const
{
    return Get(m_mouseKeys, button) && !Get(m_previousMouseKeys, button);
}

void InputManager::SetMousePositionOld(const glm::vec2 &pos)
{
    m_mousePositionOld = pos;
}

const glm::vec2 &InputManager::GetMousePositionOld() const
{
    return m_mousePositionOld;
}

void InputManager::SetMousePositionCurrent(const glm::vec2 &pos)
{
    m_mousePositionCurrent = pos;
}

const glm::vec2 &InputManager::GetMousePositionCurrent() const
{
    return m_mousePositionCurrent;
}

void InputManager::SetScrollDelta(float delta)
{
    m_scrollDelta = delta;
}

float InputManager::GetScrollDelta() const
{
    return m_scrollDelta;
}

void InputManager::ResetScrollDelta()
{
    m_scrollDelta = 0.0f;
}

void InputManager::PollGamepads()
{
    if (!m_pollGamepads)
    {
        return;
    }
    for (int i = 0; i < MaxGamepads; ++i)
    {
        GamepadState state;
        GLFWgamepadstate raw;
        const int joystick = GLFW_JOYSTICK_1 + i;
        if (glfwJoystickIsGamepad(joystick) && glfwGetGamepadState(joystick, &raw))
        {
            state.connected = true;
            for (size_t b = 0; b < state.buttons.size(); ++b)
            {
                state.buttons[b] = raw.buttons[b] == GLFW_PRESS;
            }
            for (size_t a = 0; a < state.axes.size(); ++a)
            {
                state.axes[a] = raw.axes[a];
            }
        }
        m_gamepads[static_cast<size_t>(i)] = state;
    }
}

void InputManager::SetGamepadState(int index, const GamepadState &state)
{
    if (index >= 0 && index < MaxGamepads)
    {
        m_gamepads[static_cast<size_t>(index)] = state;
    }
}

const GamepadState &InputManager::GetGamepad(int index) const
{
    return index >= 0 && index < MaxGamepads ? m_gamepads[static_cast<size_t>(index)] : s_disconnected;
}

const GamepadState &InputManager::GetPreviousGamepad(int index) const
{
    return index >= 0 && index < MaxGamepads ? m_previousGamepads[static_cast<size_t>(index)] : s_disconnected;
}

int InputManager::GetActiveGamepad() const
{
    for (int i = 0; i < MaxGamepads; ++i)
    {
        if (m_gamepads[static_cast<size_t>(i)].connected)
        {
            return i;
        }
    }
    return -1;
}

void InputManager::EndFrame()
{
    m_previousKeys = m_keys;
    m_previousMouseKeys = m_mouseKeys;
    m_previousGamepads = m_gamepads;
    m_mousePositionOld = m_mousePositionCurrent;
    m_scrollDelta = 0.0f;
}

} // namespace Geni
