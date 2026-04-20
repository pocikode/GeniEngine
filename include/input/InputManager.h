#pragma once
#include "glm/ext/vector_float2.hpp"
#include <array>
#include <glm/vec2.hpp>

namespace Geni
{

class InputManager
{
  private:
    InputManager() = default;
    InputManager(const InputManager &) = delete;
    InputManager(InputManager &&) = delete;
    InputManager &operator=(const InputManager &) = delete;
    InputManager &operator=(InputManager &&) = delete;

  public:
    void SetKeyPressed(int key, bool pressed);
    bool IsKeyPressed(int key);

    void SetMouseButtonPressed(int button, bool pressed);
    bool IsMouseButtonPressed(int button);

    void SetMousePositionOld(const glm::vec2 &pos);
    const glm::vec2 &GetMousePositionOld() const;

    void SetMousePositionCurrent(const glm::vec2 &pos);
    const glm::vec2 &GetMousePositionCurrent() const;

    void SetScrollDelta(float delta);
    float GetScrollDelta() const;
    void ResetScrollDelta();

  private:
    friend class Engine;
    std::array<bool, 256> m_keys = {false};
    std::array<bool, 16> m_mouseKeys = {false};
    glm::vec2 m_mousePositionOld = glm::vec2(0.0f);
    glm::vec2 m_mousePositionCurrent = glm::vec2(0.0f);
    float m_scrollDelta = 0.0f;
};

} // namespace Geni
