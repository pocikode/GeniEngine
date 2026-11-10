#include "TestEngine.h"

#include "core/Engine.h"
#include "scene/Scene.h"
#include <doctest/doctest.h>

namespace GeniTest
{

namespace
{
RecordingApp *g_app = nullptr;
bool g_initFailed = false;
} // namespace

void RecordingApp::FixedUpdate(float fixedDeltaTime)
{
    ++fixedUpdates;
    lastFixedDelta = fixedDeltaTime;
}

void RecordingApp::Update(float deltaTime)
{
    ++updates;
    if (sceneToUpdate)
    {
        sceneToUpdate->Update(deltaTime);
    }
}

RecordingApp &RequireEngine()
{
    auto &engine = Geni::Engine::GetInstance();
    if (!g_app && !g_initFailed)
    {
        auto app = new RecordingApp();
        engine.SetApplication(app); // the engine owns it from here

        Geni::EngineConfig config;
        config.title = "geni_tests";
        config.width = 320;
        config.height = 240;
        config.visible = false;
        config.vsync = false;
        // Tests call abort-free code paths only; leave signal handling to the test runner.
        config.installCrashHandler = false;

        if (engine.Init(config))
        {
            engine.GetFileSystem().SetAssetsFolder(GENI_TEST_ASSETS_DIR);
            g_app = app;
        }
        else
        {
            g_initFailed = true;
            engine.Destroy();
        }
    }

    REQUIRE_MESSAGE(
        g_app != nullptr, "could not start the engine: gpu tests need a display and a Vulkan 1.1 device "
                          "(run `ctest -LE gpu` to skip them)"
    );
    return *g_app;
}

Geni::Scene *ResetScene()
{
    RecordingApp &app = RequireEngine();
    app.sceneToUpdate = nullptr;

    auto scene = new Geni::Scene();
    Geni::Engine::GetInstance().SetScene(scene);
    return scene;
}

void ShutdownEngine()
{
    if (g_app)
    {
        g_app = nullptr;
        Geni::Engine::GetInstance().Destroy();
    }
}

} // namespace GeniTest
