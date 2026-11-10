#pragma once
#include "core/DebugOverlay.h"
#include "core/FixedTimestep.h"
#include "graphics/Texture.h"
#include "input/InputManager.h"
#include "io/FileSystem.h"
#include "physics/PhysicsManager.h"
#include "render/RenderQueue.h"
#include "render/Renderer.h"
#include "rhi/Device.h"
#include "scene/Scene.h"
#include <chrono>
#include <glm/ext/vector_int2.hpp>
#include <memory>
#include <string>

struct GLFWwindow;

namespace Geni
{

class Application;

struct EngineConfig
{
    int width = 1280;
    int height = 720;
    std::string title = "GeniEngine";
    // false creates a hidden window: used by tests and CI smoke runs.
    bool visible = true;
    bool vsync = true;
    // Vulkan validation layers: every validation error is logged and counted
    // (rhi::GetValidationErrorCount). On by default in debug builds.
#if defined(NDEBUG)
    bool gpuValidation = false;
#else
    bool gpuValidation = true;
#endif
    // Initializes Dear ImGui and the built-in stats window (F1). Turn off if the
    // application runs its own ImGui context.
    bool debugOverlay = true;
    bool debugOverlayVisible = true;
    // Simulation rate. Physics and Application::FixedUpdate run in steps of this size.
    double fixedTimestep = 1.0 / 60.0;
    // Cap on fixed steps per rendered frame; beyond it the simulation slows down.
    int maxFixedStepsPerFrame = 8;
    // When above zero, every Tick advances by exactly this many seconds instead of the
    // measured frame time, so a run is repeatable: for tests and screenshot capture.
    double deterministicFrameTime = 0.0;
    bool installCrashHandler = true;
    // Folder for crash reports. Empty means the current working directory.
    std::string crashReportDirectory;
    // Log file path. Empty means no log file (stderr only).
    std::string logFile;
    // Renderer v1 quality and art direction (MSAA, cascades, clusters, sky).
    RendererConfig renderer;
};

class Engine
{
  public:
    static Engine &GetInstance();

  private:
    Engine() = default;
    Engine(const Engine &) = delete;
    Engine(Engine &&) = delete;
    Engine &operator=(const Engine &) = delete;
    Engine &operator=(Engine &&) = delete;

  public:
    bool Init(const EngineConfig &config);
    // Pre-M0 entry point, kept for existing applications: uses the title from
    // SetWindowTitle and leaves the debug overlay off.
    bool Init(int width, int height);
    bool IsInitialized() const;

    // Calls Tick until the window is closed or the application asks to quit.
    void Run();
    // One frame: poll input, Simulate with the measured frame time, RenderFrame.
    // Returns false once the application should stop.
    bool Tick();
    // Advances the simulation by frameSeconds: zero or more fixed steps (physics, then
    // Application::FixedUpdate), then one Application::Update. Does not touch the window.
    void Simulate(double frameSeconds);
    // Draws the current scene and the debug overlay, then presents.
    void RenderFrame();
    void Destroy();

    // Saves the next rendered frame (as shown, overlay included): PNG when the path ends
    // in .png, binary PPM otherwise. Used by the screenshot comparison tests.
    void RequestScreenshot(const std::string &path);

    void SetApplication(Application *app);
    void SetWindowTitle(const std::string &title);
    Application *GetApplication();
    InputManager &GetInputManager();
    Renderer &GetRenderer();
    // nullptr before Init and after Destroy.
    rhi::Device *GetRenderDevice();
    RenderQueue &GetRenderQueue();
    FileSystem &GetFileSystem();
    TextureManager &GetTextureManager();
    PhysicsManager &GetPhysicsManager();
    DebugOverlay &GetDebugOverlay();
    FixedTimestep &GetFixedTimestep();
    const FrameStats &GetFrameStats() const;

    void SetScene(Scene *scene);
    Scene *GetScene();

    GLFWwindow *GetWindow();
    glm::ivec2 GetFramebufferSize();

  private:
    std::unique_ptr<Application> m_application;
    std::unique_ptr<Scene> m_currentScene;
    std::chrono::steady_clock::time_point m_lastTimePoint;
    GLFWwindow *m_window = nullptr;
    InputManager m_inputManager;
    std::unique_ptr<rhi::Device> m_device;
    Renderer m_renderer;
    RenderQueue m_renderQueue;
    FileSystem m_fileSystem;
    TextureManager m_textureManager;
    PhysicsManager m_physicsManager;
    DebugOverlay m_debugOverlay;
    FixedTimestep m_fixedTimestep;
    FrameStats m_frameStats;
    std::string m_windowTitle = "GeniEngine";
    bool m_initialized = false;
    bool m_firstTick = true;
    double m_deterministicFrameTime = 0.0;
    std::string m_pendingScreenshotPath;
};

} // namespace Geni
