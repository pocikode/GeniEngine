#include "core/Engine.h"
#include "core/Application.h"
#include "core/Assert.h"
#include "core/Common.h"
#include "core/CrashHandler.h"
#include "core/Log.h"
#include "core/Profiler.h"
#include "glm/ext/vector_float2.hpp"
#include "graphics/GraphicsAPI.h"
#include "render/RenderQueue.h"
#include "scene/components/CameraComponent.h"
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <cstdio>
#include <vector>

namespace Geni
{

void keyCallback(GLFWwindow *window, int key, int, int action, int)
{
    auto &inputManager = Geni::Engine::GetInstance().GetInputManager();
    if (action == GLFW_PRESS)
    {
        inputManager.SetKeyPressed(key, true);
    }
    else if (action == GLFW_RELEASE)
    {
        inputManager.SetKeyPressed(key, false);
    }
}

void mouseButtonCallback(GLFWwindow *window, int button, int action, int mods)
{
    auto &inputManager = Geni::Engine::GetInstance().GetInputManager();
    if (action == GLFW_PRESS)
    {
        inputManager.SetMouseButtonPressed(button, true);
    }
    else if (action == GLFW_RELEASE)
    {
        inputManager.SetMouseButtonPressed(button, false);
    }
}

void mouseCursorCallback(GLFWwindow *window, double xpos, double ypos)
{
    auto &inputManager = Geni::Engine::GetInstance().GetInputManager();
    inputManager.SetMousePositionOld(inputManager.GetMousePositionCurrent());

    glm::vec2 currentPos(static_cast<float>(xpos), static_cast<float>(ypos));
    inputManager.SetMousePositionCurrent(currentPos);
}

void scrollCallback(GLFWwindow *window, double xoffset, double yoffset)
{
    Geni::Engine::GetInstance().GetInputManager().SetScrollDelta(static_cast<float>(yoffset));
}

static void glfwErrorCallback(int code, const char *description)
{
    GENI_LOG_ERROR("GLFW error %d: %s", code, description ? description : "");
}

static void SaveBackBufferAsPpm(GLFWwindow *window, const std::string &path)
{
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    if (width <= 0 || height <= 0)
    {
        GENI_LOG_ERROR("Screenshot '%s' skipped: the framebuffer is empty", path.c_str());
        return;
    }

    std::vector<unsigned char> pixels(static_cast<size_t>(width) * static_cast<size_t>(height) * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(GL_BACK);
    glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());

    FILE *file = std::fopen(path.c_str(), "wb");
    if (!file)
    {
        GENI_LOG_ERROR("Screenshot: cannot write '%s'", path.c_str());
        return;
    }

    std::fprintf(file, "P6\n%d %d\n255\n", width, height);
    // OpenGL rows start at the bottom; image files start at the top.
    const size_t rowBytes = static_cast<size_t>(width) * 3;
    for (int row = height - 1; row >= 0; --row)
    {
        std::fwrite(pixels.data() + static_cast<size_t>(row) * rowBytes, 1, rowBytes, file);
    }
    std::fclose(file);
    GENI_LOG_INFO("Screenshot saved to %s (%dx%d)", path.c_str(), width, height);
}

Engine &Engine::GetInstance()
{
    static Engine instance;
    return instance;
}

bool Engine::Init(int width, int height)
{
    EngineConfig config;
    config.width = width;
    config.height = height;
    config.title = m_windowTitle;
    config.debugOverlay = false;
    return Init(config);
}

bool Engine::Init(const EngineConfig &config)
{
    if (m_initialized)
    {
        GENI_LOG_ERROR("Engine::Init called twice without Destroy");
        return false;
    }

    if (config.installCrashHandler)
    {
        CrashHandlerConfig crashConfig;
        crashConfig.reportDirectory = config.crashReportDirectory;
        crashConfig.applicationName = config.title;
        InstallCrashHandler(crashConfig);
    }

    if (!config.logFile.empty() && !Log::OpenLogFile(config.logFile))
    {
        GENI_LOG_WARN("Could not open log file '%s'", config.logFile.c_str());
    }

    if (!m_application)
    {
        GENI_LOG_ERROR("Engine::Init: no application set, call SetApplication first");
        return false;
    }

    glfwSetErrorCallback(glfwErrorCallback);
    if (!glfwInit())
    {
        GENI_LOG_ERROR("Failed to initialize GLFW");
        return false;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#if defined(__APPLE__)
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
    glfwWindowHint(GLFW_VISIBLE, config.visible ? GLFW_TRUE : GLFW_FALSE);

    m_windowTitle = config.title;
    m_window = glfwCreateWindow(config.width, config.height, m_windowTitle.c_str(), nullptr, nullptr);
    if (!m_window)
    {
        GENI_LOG_ERROR("Failed to create a %dx%d window with an OpenGL 3.3 core context", config.width,
                       config.height);
        glfwTerminate();
        return false;
    }

    glfwSetKeyCallback(m_window, keyCallback);
    glfwSetMouseButtonCallback(m_window, mouseButtonCallback);
    glfwSetCursorPosCallback(m_window, mouseCursorCallback);
    glfwSetScrollCallback(m_window, scrollCallback);

    glfwMakeContextCurrent(m_window);
    glfwSwapInterval(config.vsync ? 1 : 0);

    // GLEW reports GLEW_ERROR_NO_GLX_DISPLAY under Wayland/EGL even though the function
    // pointers loaded fine, so that one code is not a failure.
    const GLenum glewStatus = glewInit();
    if (glewStatus != GLEW_OK && glewStatus != GLEW_ERROR_NO_GLX_DISPLAY)
    {
        GENI_LOG_ERROR("Failed to initialize GLEW: %s",
                       reinterpret_cast<const char *>(glewGetErrorString(glewStatus)));
        glfwDestroyWindow(m_window);
        m_window = nullptr;
        glfwTerminate();
        return false;
    }

    int fbWidth = 0;
    int fbHeight = 0;
    glfwGetFramebufferSize(m_window, &fbWidth, &fbHeight);
    glViewport(0, 0, fbWidth, fbHeight);

    GENI_LOG_INFO("OpenGL %s on %s", reinterpret_cast<const char *>(glGetString(GL_VERSION)),
                  reinterpret_cast<const char *>(glGetString(GL_RENDERER)));

    m_graphicsAPI.Init();
    m_physicsManager.Init();

    m_fixedTimestep = FixedTimestep(config.fixedTimestep, config.maxFixedStepsPerFrame);
    m_frameStats = FrameStats();
    m_firstTick = true;
    m_deterministicFrameTime = config.deterministicFrameTime;

    if (config.debugOverlay)
    {
        if (m_debugOverlay.Init(m_window))
        {
            m_debugOverlay.SetVisible(config.debugOverlayVisible);
        }
        else
        {
            GENI_LOG_WARN("Debug overlay unavailable (engine built without GENI_ENABLE_IMGUI?)");
        }
    }

    m_initialized = true;

    // ImGui calls are legal from Application::Init onwards.
    m_debugOverlay.BeginFrame();

    if (!m_application->Init())
    {
        GENI_LOG_ERROR("Application::Init failed");
        return false;
    }

    return true;
}

bool Engine::IsInitialized() const
{
    return m_initialized;
}

void Engine::Run()
{
    if (!m_initialized || !m_application)
    {
        return;
    }

    while (Tick())
    {
    }
}

bool Engine::Tick()
{
    if (!m_initialized || !m_application)
    {
        return false;
    }

    if (glfwWindowShouldClose(m_window) || m_application->NeedsToBeClosed())
    {
        return false;
    }

    auto now = std::chrono::steady_clock::now();
    double frameSeconds = 0.0;
    if (m_firstTick)
    {
        m_firstTick = false;
    }
    else
    {
        frameSeconds = std::chrono::duration<double>(now - m_lastTimePoint).count();
    }
    m_lastTimePoint = now;

    if (m_deterministicFrameTime > 0.0)
    {
        frameSeconds = m_deterministicFrameTime;
    }

    {
        GENI_PROFILE_SCOPE("PollEvents");
        glfwPollEvents();
    }

    m_frameStats.frameMs = frameSeconds * 1000.0;
    if (frameSeconds > 0.0)
    {
        const double instantFps = 1.0 / frameSeconds;
        m_frameStats.fps = m_frameStats.fps <= 0.0 ? instantFps : m_frameStats.fps * 0.9 + instantFps * 0.1;
    }

    Simulate(frameSeconds);
    RenderFrame();

    m_inputManager.SetMousePositionOld(m_inputManager.GetMousePositionCurrent());
    m_inputManager.ResetScrollDelta();

    ++m_frameStats.frameIndex;
    GENI_PROFILE_FRAME();

    return !glfwWindowShouldClose(m_window) && !m_application->NeedsToBeClosed();
}

void Engine::Simulate(double frameSeconds)
{
    if (!m_initialized || !m_application)
    {
        return;
    }

    GENI_PROFILE_SCOPE("Simulate");
    const auto start = std::chrono::steady_clock::now();

    const int steps = m_fixedTimestep.Advance(frameSeconds);
    const float fixedDelta = static_cast<float>(m_fixedTimestep.GetStep());
    for (int i = 0; i < steps; ++i)
    {
        GENI_PROFILE_SCOPE("FixedStep");
        m_physicsManager.Step(fixedDelta);
        m_application->FixedUpdate(fixedDelta);
    }

    {
        GENI_PROFILE_SCOPE("Application::Update");
        m_application->Update(static_cast<float>(frameSeconds));
    }

    m_frameStats.fixedSteps = steps;
    m_frameStats.interpolationAlpha = m_fixedTimestep.GetAlpha();
    m_frameStats.droppedSimulationSeconds = m_fixedTimestep.GetDroppedSeconds();
    m_frameStats.simulateMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

void Engine::RenderFrame()
{
    if (!m_initialized || !m_application)
    {
        return;
    }

    const auto start = std::chrono::steady_clock::now();
    {
        GENI_PROFILE_SCOPE("Render");

        int fbWidth = 0;
        int fbHeight = 0;
        glfwGetFramebufferSize(m_window, &fbWidth, &fbHeight);

        // A minimized window has a zero-sized framebuffer: still drain the queue, with a
        // harmless aspect ratio, so commands do not pile up.
        const float aspect = fbHeight > 0 ? static_cast<float>(fbWidth) / static_cast<float>(fbHeight) : 1.0f;
        glViewport(0, 0, fbWidth, fbHeight);

        m_graphicsAPI.SetClearColor(1.0f, 1.0f, 1.0f, 1.0f);
        m_graphicsAPI.ClearBuffers();

        CameraData cameraData;
        cameraData.viewMatrix = glm::mat4(1.0f);
        cameraData.projectionMatrix = glm::mat4(1.0f);
        cameraData.position = glm::vec3(0.0f);
        std::vector<LightData> lights;

        if (m_currentScene)
        {
            if (auto cameraObj = m_currentScene->GetMainCamera())
            {
                auto cameraComponent = cameraObj->GetComponent<CameraComponent>();
                if (cameraComponent)
                {
                    cameraData.viewMatrix = cameraComponent->GetViewMatrix();
                    cameraData.projectionMatrix = cameraComponent->GetProjectionMatrix(aspect);
                    cameraData.position = cameraObj->GetWorldPosition();
                }
            }

            lights = m_currentScene->CollectLights();
        }

        m_renderQueue.Draw(m_graphicsAPI, cameraData, lights);
        m_frameStats.drawCalls = m_renderQueue.GetLastDrawCallCount();

        m_application->Render();

        m_frameStats.renderMs =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        m_debugOverlay.Render(m_frameStats);
    }

    if (!m_pendingScreenshotPath.empty())
    {
        SaveBackBufferAsPpm(m_window, m_pendingScreenshotPath);
        m_pendingScreenshotPath.clear();
    }

    {
        GENI_PROFILE_SCOPE("SwapBuffers");
        glfwSwapBuffers(m_window);
    }

    // Open the next ImGui frame right away so ImGui calls are legal anywhere in the
    // application's FixedUpdate, Update and Render.
    m_debugOverlay.BeginFrame();
}

void Engine::Destroy()
{
    if (m_application)
    {
        if (m_initialized)
        {
            m_application->Destroy();
        }
        m_application.reset();
    }

    if (!m_initialized)
    {
        return;
    }

    // Order matters: everything that owns GL objects or rigid bodies goes before the
    // context and the physics world.
    m_currentScene.reset();
    m_renderQueue.Clear();
    m_textureManager.Clear();
    m_graphicsAPI.Shutdown();
    m_physicsManager.Shutdown();
    m_debugOverlay.Shutdown();

    glfwDestroyWindow(m_window);
    glfwTerminate();
    m_window = nullptr;
    m_initialized = false;

    Log::Flush();
}

void Engine::SetApplication(Application *app)
{
    m_application.reset(app);
}

Application *Engine::GetApplication()
{
    return m_application.get();
}

InputManager &Engine::GetInputManager()
{
    return m_inputManager;
}

GraphicsAPI &Engine::GetGraphicsAPI()
{
    return m_graphicsAPI;
}

RenderQueue &Engine::GetRenderQueue()
{
    return m_renderQueue;
}

FileSystem &Engine::GetFileSystem()
{
    return m_fileSystem;
}

TextureManager &Engine::GetTextureManager()
{
    return m_textureManager;
}

PhysicsManager &Engine::GetPhysicsManager()
{
    return m_physicsManager;
}

DebugOverlay &Engine::GetDebugOverlay()
{
    return m_debugOverlay;
}

FixedTimestep &Engine::GetFixedTimestep()
{
    return m_fixedTimestep;
}

const FrameStats &Engine::GetFrameStats() const
{
    return m_frameStats;
}

void Engine::RequestScreenshot(const std::string &path)
{
    m_pendingScreenshotPath = path;
}

void Engine::SetScene(Scene *scene)
{
    m_currentScene.reset(scene);
}

Scene *Engine::GetScene()
{
    return m_currentScene.get();
}

void Engine::SetWindowTitle(const std::string &title)
{
    m_windowTitle = title;
}

GLFWwindow *Engine::GetWindow()
{
    return m_window;
}

glm::ivec2 Engine::GetFramebufferSize()
{
    int w, h;
    glfwGetFramebufferSize(m_window, &w, &h);
    return {w, h};
}

} // namespace Geni
