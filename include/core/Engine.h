#pragma once
#include "assets/AssetManager.h"
#include "audio/AudioSystem.h"
#include "core/DebugOverlay.h"
#include "core/FixedTimestep.h"
#include "core/JobSystem.h"
#include "graphics/Texture.h"
#include "input/InputActions.h"
#include "input/InputManager.h"
#include "io/FileSystem.h"
#include "nav/NavigationSystem.h"
#include "physics/PhysicsManager.h"
#include "render/RenderQueue.h"
#include "render/Renderer.h"
#include "rhi/Device.h"
#include "scene/Scene.h"
#include "script/ScriptSystem.h"
#include "ui/GameUI.h"
#include "world/WorldStreamer.h"
#include <chrono>
#include <glm/ext/vector_int2.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
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
    // Dear ImGui docking (windows can be docked into each other and into a dock space).
    // Tools turn it on; it has to be set before the first ImGui frame.
    bool imguiDocking = false;
    // Where Dear ImGui keeps window positions and the dock layout. Empty: nothing is saved.
    std::string imguiIniFile;
    // Simulation rate. Physics and Application::FixedUpdate run in steps of this size.
    double fixedTimestep = 1.0 / 60.0;
    // Worker threads for asset decoding and animation. 0 uses hardware_concurrency - 1.
    uint32_t jobThreadCount = 0;
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
    // Sound (docs/design/0012). Hidden windows never open an output device.
    AudioConfig audio;
    // Lua (docs/design/0013).
    ScriptConfig scripting;
    // TrueType font of the game UI (docs/design/0015). Empty: fonts/Roboto-Medium.ttf next
    // to the executable, else the copy in the engine's source tree.
    std::string uiFont;
    // World streaming radii and memory budgets (docs/design/0007), per platform.
    StreamingBudget streaming = StreamingBudget::ForPlatform(StreamingBudget::CurrentPlatform());
};

// A camera that replaces the scene's main camera for rendering: the editor's own view
// while a scene is being edited. The projection uses the scene viewport's aspect ratio.
struct ViewOverride
{
    glm::mat4 viewMatrix{1.0f};
    glm::vec3 position{0.0f};
    float fieldOfViewDegrees = 60.0f;
    float nearPlane = 0.1f;
    float farPlane = 1000.0f;
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
    // Named actions (docs/design/0009), updated once per frame after input is polled.
    InputActions &GetInputActions()
    {
        return m_inputActions;
    }
    // Hides the cursor and gives unlimited mouse motion (mouse look), or releases it.
    void SetCursorCaptured(bool captured);
    bool IsCursorCaptured() const
    {
        return m_cursorCaptured;
    }
    Renderer &GetRenderer();
    // nullptr before Init and after Destroy.
    rhi::Device *GetRenderDevice();
    RenderQueue &GetRenderQueue();
    FileSystem &GetFileSystem();
    TextureManager &GetTextureManager();
    AssetManager &GetAssetManager();
    JobSystem *GetJobSystem();
    PhysicsManager &GetPhysicsManager();
    AudioSystem &GetAudio()
    {
        return m_audio;
    }
    NavigationSystem &GetNavigation()
    {
        return m_navigation;
    }
    ScriptSystem &GetScripts()
    {
        return m_scripts;
    }
    GameUI &GetGameUI()
    {
        return m_gameUI;
    }
    DebugOverlay &GetDebugOverlay();
    FixedTimestep &GetFixedTimestep();
    const FrameStats &GetFrameStats() const;

    void SetScene(Scene *scene);
    Scene *GetScene();

    // Streams a cooked world into the current scene (created if there is none) around the
    // main camera, with EngineConfig::streaming as the budget (docs/design/0007). The
    // scene is cleared and the world's persistent scene loaded into it. The streamer runs
    // once per frame in Simulate, after assets update and before Application::Update.
    bool OpenWorld(AssetID world, std::string *error = nullptr);
    // The same, from the .gworld file's path relative to the assets folder.
    bool OpenWorld(const std::string &sourcePath, std::string *error = nullptr);
    void CloseWorld();
    WorldStreamer &GetWorldStreamer()
    {
        return m_worldStreamer;
    }

    // Renders with this camera instead of the scene's main camera until cleared.
    void SetViewOverride(const ViewOverride &view);
    void ClearViewOverride();
    bool HasViewOverride() const
    {
        return m_hasViewOverride;
    }
    // A directional light used only while the scene has none of its own, so an unlit
    // scene opened in a tool is still readable (the editor's preview sun). Off by default.
    void SetFallbackSun(const LightData &sun);
    void ClearFallbackSun();

    // The camera the next frame renders with (the override, else the scene's main
    // camera, else identity), for the given aspect ratio.
    CameraData GetRenderCamera(float aspect);

    // While paused the fixed-step loop still calls Application::FixedUpdate but does not
    // step physics: an editor shows the scene without simulating it.
    void SetPhysicsPaused(bool paused)
    {
        m_physicsPaused = paused;
    }
    bool IsPhysicsPaused() const
    {
        return m_physicsPaused;
    }

    GLFWwindow *GetWindow();
    glm::ivec2 GetFramebufferSize();

  private:
    std::unique_ptr<Application> m_application;
    std::unique_ptr<Scene> m_currentScene;
    std::chrono::steady_clock::time_point m_lastTimePoint;
    GLFWwindow *m_window = nullptr;
    InputManager m_inputManager;
    InputActions m_inputActions;
    bool m_cursorCaptured = false;
    bool m_windowVisible = true;
    std::unique_ptr<rhi::Device> m_device;
    Renderer m_renderer;
    RenderQueue m_renderQueue;
    FileSystem m_fileSystem;
    TextureManager m_textureManager;
    AssetManager m_assetManager;
    WorldStreamer m_worldStreamer;
    StreamingBudget m_streamingBudget;
    uint32_t m_framesSinceAssetCollection = 0;
    std::unique_ptr<JobSystem> m_jobSystem;
    PhysicsManager m_physicsManager;
    AudioSystem m_audio;
    NavigationSystem m_navigation;
    ScriptSystem m_scripts;
    GameUI m_gameUI;
    DebugOverlay m_debugOverlay;
    FixedTimestep m_fixedTimestep;
    FrameStats m_frameStats;
    std::string m_windowTitle = "GeniEngine";
    bool m_initialized = false;
    bool m_firstTick = true;
    double m_deterministicFrameTime = 0.0;
    std::string m_pendingScreenshotPath;
    ViewOverride m_viewOverride;
    bool m_hasViewOverride = false;
    LightData m_fallbackSun;
    bool m_hasFallbackSun = false;
    bool m_physicsPaused = false;
};

} // namespace Geni
