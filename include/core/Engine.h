#pragma once
#include "graphics/GraphicsAPI.h"
#include "graphics/Texture.h"
#include "input/InputManager.h"
#include "io/FileSystem.h"
#include "physics/PhysicsManager.h"
#include "render/RenderQueue.h"
#include "scene/Scene.h"
#include <chrono>
#include <memory>

struct GLFWwindow;

namespace Geni
{

class Application;

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
    bool Init(int width, int height);
    void Run();
    void Destroy();

    void SetApplication(Application *app);
    Application *GetApplication();
    InputManager &GetInputManager();
    GraphicsAPI &GetGraphicsAPI();
    RenderQueue &GetRenderQueue();
    FileSystem &GetFileSystem();
    TextureManager &GetTextureManager();
    PhysicsManager &GetPhysicsManager();

    void SetScene(Scene *scene);
    Scene *GetScene();

  private:
    std::unique_ptr<Application> m_application;
    std::unique_ptr<Scene> m_currentScene;
    std::chrono::steady_clock::time_point m_lastTimePoint;
    GLFWwindow *m_window = nullptr;
    InputManager m_inputManager;
    GraphicsAPI m_graphicsAPI;
    RenderQueue m_renderQueue;
    FileSystem m_fileSystem;
    TextureManager m_textureManager;
    PhysicsManager m_physicsManager;
};

} // namespace Geni
