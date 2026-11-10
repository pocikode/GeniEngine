#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

struct GLFWwindow;

namespace Geni
{

// Timings and counters for the most recent frame, filled in by Engine::Tick.
struct FrameStats
{
    uint64_t frameIndex = 0;
    double frameMs = 0.0;    // wall time between the last two frames
    double simulateMs = 0.0; // fixed steps + Application::Update
    double renderMs = 0.0;   // queue draw + Application::Render + overlay, excluding buffer swap
    double fps = 0.0;        // smoothed
    int fixedSteps = 0;      // fixed steps run this frame
    double interpolationAlpha = 0.0;
    double droppedSimulationSeconds = 0.0;
    int drawCalls = 0;
};

// Dear ImGui integration plus a built-in stats and log window (toggle with F1).
// The engine owns the ImGui context and the frame: between Application::Update and the
// end of Application::Render, game code can call ImGui:: functions directly.
// When the engine is built without GENI_ENABLE_IMGUI every method is a no-op.
class DebugOverlay
{
  public:
    bool Init(GLFWwindow *window);
    void Shutdown();
    bool IsInitialized() const;

    void BeginFrame();
    // Draws the built-in window (if visible) and submits all ImGui draw data.
    void Render(const FrameStats &stats);

    void SetVisible(bool visible);
    bool IsVisible() const;

    // True while ImGui is using the mouse / keyboard, so game input should ignore it.
    bool WantsMouse() const;
    bool WantsKeyboard() const;

  private:
    void DrawStatsWindow(const FrameStats &stats);

  private:
    bool m_initialized = false;
    bool m_visible = true;
    bool m_frameStarted = false;
    std::array<float, 120> m_frameTimes = {};
    size_t m_frameTimeCursor = 0;
};

} // namespace Geni
