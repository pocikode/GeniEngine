// The terrain view (docs/design/0022): loads the cooked hills, puts a camera above them
// looking across the valley, a sun with a stopped clock, and optionally weather. With
// --frames and --screenshot it is the terrain screenshot test.

#include "TerrainMode.h"

#include <geni.h>

#include <GLFW/glfw3.h>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <glm/gtc/quaternion.hpp>

namespace
{

class TerrainApp : public Geni::Application
{
  public:
    explicit TerrainApp(const TerrainOptions &options) : m_options(options)
    {
    }

    bool Init() override
    {
        auto &engine = Geni::Engine::GetInstance();
#if defined(GENI_SAMPLE_ASSETS_DIR)
        std::error_code ec;
        if (!engine.GetFileSystem().IsPackaged() && std::filesystem::exists(GENI_SAMPLE_ASSETS_DIR, ec))
        {
            engine.GetFileSystem().SetAssetsFolder(GENI_SAMPLE_ASSETS_DIR);
        }
#endif
        m_scene = new Geni::Scene();
        engine.SetScene(m_scene);
        auto &assets = engine.GetAssetManager();
        const Geni::AssetID sceneID = assets.FindBySource(Geni::AssetType::Scene, m_options.terrain);
        auto terrainScene = assets.LoadScene(sceneID, false);
        Geni::SerializationContext context{m_scene, &assets};
        std::string error;
        if (!terrainScene.IsReady() ||
            !Geni::SceneSerializer::Instantiate(*m_scene, *terrainScene.Get(), context, &error))
        {
            GENI_LOG_ERROR(
                "Could not load %s: %s", m_options.terrain.c_str(), error.empty() ? "not cooked?" : error.c_str()
            );
            return false;
        }
        // Meshes and materials first, so the first frame is complete.
        assets.WaitForAll();
        engine.GetPhysicsManager().FlushPendingBodies();

        // The sun, with a stopped clock.
        auto *sun = m_scene->CreateObject("Sun");
        auto *light = new Geni::LightComponent();
        light->SetType(Geni::LightType::Directional);
        sun->AddComponent(light);
        auto *clock = new Geni::TimeOfDayComponent();
        clock->SetTime(static_cast<float>(m_options.timeOfDay));
        clock->SetDayLengthMinutes(0.0f);
        clock->SetPathYaw(30.0f);
        // Light haze: the far ridge is 500 m away (the clock sets the frame's fog).
        clock->SetFogDensity(0.0006f);
        sun->AddComponent(clock);

        if (!m_options.weather.empty())
        {
            Geni::WeatherState state;
            state.cloudCover = 0.8f;
            state.fog = m_options.weather == "fog" ? 1.0f : 0.3f;
            state.precipitation = m_options.weather == "fog" ? 0.0f : 0.8f;
            state.snow = m_options.weather == "snow";
            auto *weather = new Geni::WeatherComponent();
            weather->SetTarget(state);
            m_scene->CreateObject("Weather")->AddComponent(weather);
        }

        // Above the near corner, looking across the terrain towards the ridge.
        auto *camera = m_scene->CreateObject("Camera");
        float ground = 0.0f;
        if (auto *root = m_scene->FindObject("hills"))
        {
            if (auto *terrain = root->GetComponent<Geni::TerrainComponent>())
            {
                terrain->GetHeight(glm::vec3(60.0f, 0.0f, 40.0f), ground);
            }
        }
        camera->SetPosition(glm::vec3(60.0f, ground + 35.0f, 40.0f));
        camera->SetRotation(glm::quatLookAtRH(glm::normalize(glm::vec3(0.55f, -0.22f, 1.0f)), glm::vec3(0, 1, 0)));
        auto *cameraComponent = new Geni::CameraComponent();
        cameraComponent->SetFarPlane(2000.0f);
        camera->AddComponent(cameraComponent);
        camera->AddComponent(new Geni::FlyCameraComponent());
        m_scene->SetMainCamera(camera);
        return true;
    }

    void Update(float deltaTime) override
    {
        auto &engine = Geni::Engine::GetInstance();
        if (engine.GetInputManager().IsKeyPressed(GLFW_KEY_ESCAPE))
        {
            SetNeedsToBeClosed(true);
        }
        m_scene->Update(deltaTime);
        ++m_frame;
        if (m_options.frames > 0 && m_frame >= m_options.frames)
        {
            if (!m_options.screenshotPath.empty())
            {
                engine.RequestScreenshot(m_options.screenshotPath);
            }
            SetNeedsToBeClosed(true);
        }
    }

    void Render() override
    {
    }

    void Destroy() override
    {
    }

  private:
    TerrainOptions m_options;
    Geni::Scene *m_scene = nullptr;
    long m_frame = 0;
};

} // namespace

int ParseTerrainOptions(int argc, char **argv, TerrainOptions &options)
{
    for (int i = 1; i < argc; ++i)
    {
        const char *arg = argv[i];
        const bool hasValue = i + 1 < argc;
        if (std::strcmp(arg, "--terrain") == 0)
        {
            if (hasValue && argv[i + 1][0] != '-')
            {
                options.terrain = argv[++i];
            }
        }
        else if (std::strcmp(arg, "--time") == 0 && hasValue)
        {
            options.timeOfDay = std::strtod(argv[++i], nullptr);
        }
        else if (std::strcmp(arg, "--weather") == 0 && hasValue)
        {
            options.weather = argv[++i];
        }
        else if (std::strcmp(arg, "--frames") == 0 && hasValue)
        {
            options.frames = std::strtol(argv[++i], nullptr, 10);
        }
        else if (std::strcmp(arg, "--hidden") == 0 || std::strcmp(arg, "--no-overlay") == 0)
        {
            options.hidden = true;
        }
        else if (std::strcmp(arg, "--width") == 0 && hasValue)
        {
            options.width = std::atoi(argv[++i]);
        }
        else if (std::strcmp(arg, "--height") == 0 && hasValue)
        {
            options.height = std::atoi(argv[++i]);
        }
        else if (std::strcmp(arg, "--screenshot") == 0 && hasValue)
        {
            options.screenshotPath = argv[++i];
        }
        else
        {
            GENI_LOG_ERROR(
                "Unknown argument '%s'. Usage: city --terrain [FILE] [--time HOURS] [--weather rain|snow|fog] "
                "[--frames N] [--hidden] [--width W] [--height H] [--screenshot FILE]",
                arg
            );
            return 2;
        }
    }
    return -1;
}

int RunTerrain(const TerrainOptions &options)
{
    Geni::EngineConfig config;
    config.title = "GeniEngine City: terrain";
    config.width = options.width;
    config.height = options.height;
    config.visible = !options.hidden;
    config.vsync = !options.hidden;
    config.debugOverlay = !options.hidden;
    if (options.hidden)
    {
        config.deterministicFrameTime = 1.0f / 60.0f;
    }
    auto *app = new TerrainApp(options);
    auto &engine = Geni::Engine::GetInstance();
    engine.SetApplication(app);
    if (!engine.Init(config))
    {
        engine.Destroy();
        return 1;
    }
    engine.Run();
    engine.Destroy();
    if (const auto errors = Geni::rhi::GetValidationErrorCount())
    {
        GENI_LOG_ERROR("%llu Vulkan validation errors", static_cast<unsigned long long>(errors));
        return 3;
    }
    return 0;
}
