#include "core/Engine.h"
#include "core/Application.h"
#include "core/Common.h"
#include "glm/ext/vector_float2.hpp"
#include "graphics/GraphicsAPI.h"
#include "render/RenderQueue.h"
#include "scene/components/CameraComponent.h"
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <iostream>
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
    else if (action == GLFW_FALSE)
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
    else if (action == GLFW_FALSE)
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

Engine &Engine::GetInstance()
{
    static Engine instance;
    return instance;
}

bool Engine::Init(int width, int height)
{
    if (!m_application)
    {
        return false;
    }

    if (!glfwInit())
    {
        return false;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);

    m_window = glfwCreateWindow(width, height, "GeniEngine", nullptr, nullptr);
    if (!m_window)
    {
        std::cout << "Error creating window" << std::endl;
        glfwTerminate();
        return false;
    }

    // Get the actual framebuffer size (in physical pixels)
    int fbWidth, fbHeight;
    glfwGetFramebufferSize(m_window, &fbWidth, &fbHeight);

    // Get the window size (in logical pixels)
    int winWidth, winHeight;
    glfwGetWindowSize(m_window, &winWidth, &winHeight);

    // Calculate DPI scale factor
    float scaleX = static_cast<float>(fbWidth) / static_cast<float>(winWidth);
    float scaleY = static_cast<float>(fbHeight) / static_cast<float>(winHeight);

    glfwSetKeyCallback(m_window, keyCallback);
    glfwSetMouseButtonCallback(m_window, mouseButtonCallback);
    glfwSetCursorPosCallback(m_window, mouseCursorCallback);

    glfwMakeContextCurrent(m_window);

    if (glewInit() != GLEW_OK)
    {
        glfwTerminate();
        return false;
    }

    // Set viewport to match framebuffer size (handles DPI scaling)
    glViewport(0, 0, fbWidth, fbHeight);

    m_graphicsAPI.Init();
    return m_application->Init();
}

void Engine::Run()
{
    if (!m_application)
    {
        return;
    }

    m_lastTimePoint = std::chrono::steady_clock::now();
    while (!glfwWindowShouldClose(m_window) && !m_application->NeedsToBeClosed())
    {
        glfwPollEvents();

        auto now = std::chrono::steady_clock::now();
        float deltaTime = std::chrono::duration<float>(now - m_lastTimePoint).count();
        m_lastTimePoint = now;

        m_application->Update(deltaTime);

        m_graphicsAPI.SetClearColor(1.0f, 1.0f, 1.0f, 1.0f);
        m_graphicsAPI.ClearBuffers();

        int fbWidth = 0;
        int fbHeight = 0;
        glfwGetFramebufferSize(m_window, &fbWidth, &fbHeight);
        float aspect = static_cast<float>(fbWidth) / static_cast<float>(fbHeight);

        CameraData cameraData;
        std::vector<LightData> lights;

        if (m_currentScene)
        {
            if (auto cameraObj = m_currentScene->GetMainCamera())
            {
                // logic for matrices
                auto cameraComponent = cameraObj->GetComponent<CameraComponent>();
                if (cameraComponent)
                {
                    cameraData.viewMatrix = cameraComponent->GetViewMatrix();
                    cameraData.projectionMatrix = cameraComponent->GetProjectionMatrix(aspect);
                }
            }

            lights = m_currentScene->CollectLights();
        }

        m_renderQueue.Draw(m_graphicsAPI, cameraData, lights);

        glfwSwapBuffers(m_window);

        m_inputManager.SetMousePositionOld(m_inputManager.GetMousePositionCurrent());
    }
}

void Engine::Destroy()
{
    if (m_application)
    {
        m_application->Destroy();
        m_application.reset();
        glfwTerminate();
        m_window = nullptr;
    }
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

void Engine::SetScene(Scene *scene)
{
    m_currentScene.reset(scene);
}

Scene *Engine::GetScene()
{
    return m_currentScene.get();
}

} // namespace Geni
