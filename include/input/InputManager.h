#pragma once
#include <array>

namespace Orbis
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

  private:
    friend class Engine;
    std::array<bool, 256> m_keys = {false};
};

} // namespace Orbis
