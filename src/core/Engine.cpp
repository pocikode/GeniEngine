#include "core/Engine.h"
#include "core/Application.h"
#include "core/Assert.h"
#include "core/Common.h"
#include "core/CrashHandler.h"
#include "core/Log.h"
#include "core/Profiler.h"
#include "glm/ext/vector_float2.hpp"
#include "io/ImageWriter.h"
#include "render/RenderQueue.h"
#include "rhi/Device.h"
#include <algorithm>
#include "scene/components/CameraComponent.h"
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
    rhi::PrepareWindowSystem();
    if (!glfwInit())
    {
        GENI_LOG_ERROR("Failed to initialize GLFW");
        return false;
    }

    // The renderer owns the GPU API (Vulkan); the window gets no OpenGL context.
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    // A hidden window is a test or capture run: its framebuffer must be exactly the
    // requested size on every machine, so no DPI scaling and no Retina framebuffer.
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, config.visible ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_COCOA_RETINA_FRAMEBUFFER, config.visible ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_VISIBLE, config.visible ? GLFW_TRUE : GLFW_FALSE);

    m_windowTitle = config.title;
    m_window = glfwCreateWindow(config.width, config.height, m_windowTitle.c_str(), nullptr, nullptr);
    if (!m_window)
    {
        GENI_LOG_ERROR("Failed to create a %dx%d window", config.width, config.height);
        glfwTerminate();
        return false;
    }

    glfwSetKeyCallback(m_window, keyCallback);
    glfwSetMouseButtonCallback(m_window, mouseButtonCallback);
    glfwSetCursorPosCallback(m_window, mouseCursorCallback);
    glfwSetScrollCallback(m_window, scrollCallback);

    rhi::DeviceConfig deviceConfig;
    deviceConfig.applicationName = m_windowTitle.c_str();
    deviceConfig.vsync = config.vsync;
    deviceConfig.validation = config.gpuValidation;
    m_device = rhi::CreateDevice(m_window, deviceConfig);
    if (!m_device || !m_renderer.Init(m_device.get()))
    {
        GENI_LOG_ERROR("Failed to initialize the Vulkan renderer");
        m_renderer.Shutdown();
        m_device.reset();
        glfwDestroyWindow(m_window);
        m_window = nullptr;
        glfwTerminate();
        return false;
    }

    m_physicsManager.Init();

    m_fixedTimestep = FixedTimestep(config.fixedTimestep, config.maxFixedStepsPerFrame);
    m_frameStats = FrameStats();
    m_firstTick = true;
    m_deterministicFrameTime = config.deterministicFrameTime;

    if (config.debugOverlay)
    {
        if (m_debugOverlay.Init(m_window, m_device.get()))
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
    rhi::CommandList *cmd = nullptr;
    {
        GENI_PROFILE_SCOPE("Render");

        // nullptr while minimized: the frame is still simulated, just not drawn.
        cmd = m_device->BeginFrame();

        uint32_t width = 0;
        uint32_t height = 0;
        m_device->GetBackbufferSize(width, height);
        const float aspect = height > 0 ? static_cast<float>(width) / static_cast<float>(height) : 1.0f;

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

        if (cmd)
        {
            rhi::RenderPassDesc pass;
            pass.clearColor[0] = 1.0f;
            pass.clearColor[1] = 1.0f;
            pass.clearColor[2] = 1.0f;
            pass.clearColor[3] = 1.0f;
            cmd->BeginRenderPass(pass);
            m_renderQueue.Draw(m_renderer, *cmd, cameraData, lights);
        }
        else
        {
            m_renderQueue.Clear();
        }
        m_frameStats.drawCalls = m_renderQueue.GetLastDrawCallCount();

        m_application->Render();

        m_frameStats.renderMs =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        m_debugOverlay.Render(m_frameStats, cmd);

        if (cmd)
        {
            cmd->EndRenderPass();
        }
    }

    {
        GENI_PROFILE_SCOPE("Present");
        if (!m_pendingScreenshotPath.empty() && cmd)
        {
            rhi::BackbufferCapture capture;
            m_device->EndFrame(&capture);
            if (!capture.rgb.empty() &&
                WriteImage(m_pendingScreenshotPath, capture.width, capture.height, 3, capture.rgb.data()))
            {
                GENI_LOG_INFO("Screenshot saved to %s (%dx%d)", m_pendingScreenshotPath.c_str(), capture.width,
                              capture.height);
            }
            m_pendingScreenshotPath.clear();
        }
        else
        {
            m_device->EndFrame();
        }
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

    // Order matters: everything that owns GPU objects or rigid bodies goes before the
    // renderer, the device and the physics world.
    m_currentScene.reset();
    m_renderQueue.Clear();
    m_textureManager.Clear();
    m_debugOverlay.Shutdown();
    m_renderer.Shutdown();
    m_device.reset();
    m_physicsManager.Shutdown();

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

Renderer &Engine::GetRenderer()
{
    return m_renderer;
}

rhi::Device *Engine::GetRenderDevice()
{
    return m_device.get();
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
