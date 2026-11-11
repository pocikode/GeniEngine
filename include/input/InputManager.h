#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <glm/vec2.hpp>

namespace Geni
{

// GLFW's standard gamepad layout (glfwGetGamepadState): an Xbox-style pad.
enum class GamepadButton
{
    A,
    B,
    X,
    Y,
    LeftBumper,
    RightBumper,
    Back,
    Start,
    Guide,
    LeftThumb,
    RightThumb,
    DpadUp,
    DpadRight,
    DpadDown,
    DpadLeft,
    Count
};

enum class GamepadAxis
{
    LeftX,
    LeftY, // down is positive, as GLFW reports it
    RightX,
    RightY,
    LeftTrigger,  // -1 released .. 1 fully pressed, as GLFW reports it
    RightTrigger,
    Count
};

struct GamepadState
{
    bool connected = false;
    std::array<bool, static_cast<size_t>(GamepadButton::Count)> buttons = {};
    std::array<float, static_cast<size_t>(GamepadAxis::Count)> axes = {0.0f, 0.0f, 0.0f, 0.0f, -1.0f, -1.0f};
};

// Raw device state (docs/design/0009). Gameplay normally reads named actions
// (InputActions); this is what they are built from. Keys use GLFW key codes.
class InputManager
{
  public:
    static constexpr int MaxKeys = 512;
    static constexpr int MaxGamepads = 4;

    InputManager() = default;
    InputManager(const InputManager &) = delete;
    InputManager &operator=(const InputManager &) = delete;

    void SetKeyPressed(int key, bool pressed);
    bool IsKeyPressed(int key) const;
    // Edges against the previous frame (EndFrame).
    bool WasKeyPressed(int key) const;
    bool WasKeyReleased(int key) const;

    void SetMouseButtonPressed(int button, bool pressed);
    bool IsMouseButtonPressed(int button) const;
    bool WasMouseButtonPressed(int button) const;

    void SetMousePositionOld(const glm::vec2 &pos);
    const glm::vec2 &GetMousePositionOld() const;

    void SetMousePositionCurrent(const glm::vec2 &pos);
    const glm::vec2 &GetMousePositionCurrent() const;
    glm::vec2 GetMouseDelta() const
    {
        return m_mousePositionCurrent - m_mousePositionOld;
    }

    void SetScrollDelta(float delta);
    float GetScrollDelta() const;
    void ResetScrollDelta();

    // Gamepads. Engine::Tick polls them every frame unless polling is off (tests and
    // replays inject state with SetGamepadState instead).
    void PollGamepads();
    void SetGamepadPolling(bool enabled)
    {
        m_pollGamepads = enabled;
    }
    void SetGamepadState(int index, const GamepadState &state);
    const GamepadState &GetGamepad(int index) const;
    const GamepadState &GetPreviousGamepad(int index) const;
    // The first connected gamepad, or -1.
    int GetActiveGamepad() const;

    // Copies this frame's state to the previous-frame state, resets per-frame deltas.
    void EndFrame();

  private:
    friend class Engine;
    std::array<bool, MaxKeys> m_keys = {};
    std::array<bool, MaxKeys> m_previousKeys = {};
    std::array<bool, 16> m_mouseKeys = {};
    std::array<bool, 16> m_previousMouseKeys = {};
    glm::vec2 m_mousePositionOld = glm::vec2(0.0f);
    glm::vec2 m_mousePositionCurrent = glm::vec2(0.0f);
    float m_scrollDelta = 0.0f;
    std::array<GamepadState, MaxGamepads> m_gamepads = {};
    std::array<GamepadState, MaxGamepads> m_previousGamepads = {};
    bool m_pollGamepads = true;
};

} // namespace Geni
