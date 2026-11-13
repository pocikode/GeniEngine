// The reference game's first scene and the M2 performance benchmark (ROADMAP M2):
// a static city block of 10,000 objects and 100 lights, drawn with cascaded shadows,
// clustered forward lighting, PBR materials, a sky and MSAA, then tonemapped.
//
// The block is assets/scenes/city.gscene, a scene built and edited in the editor (M4):
//   cmake --build build/dev --target edit_city
// This program only loads it; there is no scene setup in C++.
//
// It is also the performance check: with --budget-ms the program exits with code 4 when
// the mean frame time is over budget, and with --load-budget-ms it exits with code 5 when
// any frame while the block is loading took longer than that (a loading hitch).
//
// Usage: city [--frames N] [--warmup N] [--budget-ms MS] [--load-budget-ms MS] [--hidden]
//             [--screenshot FILE]
//   --frames N       quit after N rendered frames (the CI benchmark measures the last N)
//   --warmup N       frames before the measurement starts (default 20)
//   --budget-ms MS   exit with code 4 when the mean frame time exceeds MS
//   --load-budget-ms MS  exit with code 5 when the slowest loading frame exceeds MS
//   --hidden         do not show the window
//   --width/--height  window size (default 1280x720)
//   --screenshot F   save the last frame as PNG (.png) or PPM
//
// World mode (ROADMAP M5): city --world [FILE] [--route] [--speed M/S] [--distance M]
//             [--altitude M] [--hitch-ms MS] [--platform desktop|mobile|web] [--time HOURS]
//             [--frames N] [--hidden] [--width W] [--height H] [--screenshot FILE]
//   streams worlds/metro/metro.gworld (or FILE) around a flying camera; see WorldMode.cpp.
//
// The game (ROADMAP M6): city --play [--autopilot] [--no-crowd] [--no-traffic]
//             [--title-shot] [--frames N] [--hidden] [--width W] [--height H] [--screenshot FILE]
//   walk, drive and explore the streamed city; see PlayMode.cpp and assets/scripts/.

#include "PlayMode.h"
#include "WorldMode.h"

#include <geni.h>

#include <GLFW/glfw3.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <glm/gtc/quaternion.hpp>
#include <memory>
#include <string>
#include <vector>

namespace
{

struct Options
{
    long frames = 0; // 0 = run until the window closes
    long warmup = 20;
    double budgetMs = 0.0;
    double loadBudgetMs = 0.0;
    bool hidden = false;
    int width = 1280;
    int height = 720;
    std::string screenshotPath;
};

// Scene nodes instantiated per frame while loading. Two passes over 10,001 nodes take
// about 20 frames, and each step stays far below a frame's budget.
constexpr size_t kNodesPerLoadingFrame = 1000;

class CityApp : public Geni::Application
{
  public:
    explicit CityApp(const Options &options) : m_options(options)
    {
    }

    bool Init() override
    {
        auto &engine = Geni::Engine::GetInstance();

#if defined(GENI_SAMPLE_ASSETS_DIR)
        // Prefer the source tree when it is there, so a scene saved in the editor is what
        // the next run loads, without a rebuild. A packaged build falls back to the
        // assets/ folder next to the executable.
        std::error_code ec;
        if (!engine.GetFileSystem().IsPackaged() && std::filesystem::exists(GENI_SAMPLE_ASSETS_DIR, ec))
        {
            engine.GetFileSystem().SetAssetsFolder(GENI_SAMPLE_ASSETS_DIR);
        }
#endif

        m_scene = new Geni::Scene();
        engine.SetScene(m_scene);

        // The block is a scene file, scenes/city.gscene: laid out in the editor from the
        // building kit (models/city_kit.gltf, cooked to .gmesh/.gmat assets). The file is
        // read and parsed on a worker, instantiated a slice per frame, and its meshes and
        // materials stream in on workers while frames keep rendering.
        auto &assets = engine.GetAssetManager();
        const Geni::AssetID sceneID = assets.FindBySource(Geni::AssetType::Scene, "scenes/city.gscene");
        m_sceneAsset = assets.LoadScene(sceneID, true);
        if (!m_sceneAsset.IsValid() || m_sceneAsset.HasFailed())
        {
            GENI_LOG_ERROR("Could not start loading scenes/city.gscene (is it in the cooked manifest?)");
            return false;
        }
        m_loading = true;
        m_lastTime = std::chrono::steady_clock::now();
        return true;
    }

    // One loading step per frame. Never blocks: it returns false until the scene exists
    // and every asset it references has loaded.
    bool StepLoading()
    {
        auto &assets = Geni::Engine::GetInstance().GetAssetManager();
        if (!m_instantiation)
        {
            if (!m_sceneAsset.IsReady())
            {
                if (m_sceneAsset.HasFailed())
                {
                    GENI_LOG_ERROR("Could not load the city scene: %s", m_sceneAsset.GetError().c_str());
                    SetNeedsToBeClosed(true);
                }
                return false;
            }
            Geni::SerializationContext context{m_scene, &assets};
            m_instantiation =
                std::make_unique<Geni::SceneInstantiation>(*m_scene, m_sceneAsset->document, context);
        }

        if (m_instantiation->GetStatus() == Geni::SceneInstantiation::Status::InProgress)
        {
            const auto status = m_instantiation->Step(kNodesPerLoadingFrame);
            if (status == Geni::SceneInstantiation::Status::Failed)
            {
                GENI_LOG_ERROR("Could not instantiate the city scene: %s", m_instantiation->GetError().c_str());
                SetNeedsToBeClosed(true);
                return false;
            }
            if (status == Geni::SceneInstantiation::Status::InProgress)
            {
                return false;
            }
        }

        // Meshes and materials finish on workers; Engine's AssetManager::Update uploads
        // them a few at a time. Wait for that without blocking the frame.
        if (assets.IsLoading())
        {
            return false;
        }

        GENI_LOG_INFO(
            "City block: %zu mesh components, %zu lights, %llu assets; loaded over %ld frames from scene %s; "
            "slowest loading frame %.2f ms",
            m_scene->CountComponents<Geni::MeshComponent>() + m_scene->CountComponents<Geni::SkinnedMeshComponent>(),
            m_scene->CollectLights().size(), static_cast<unsigned long long>(assets.GetStats().assetsLoaded),
            m_loadingFrames, m_sceneAsset->GetSourcePath().c_str(), m_slowestLoadingFrameMs
        );
        // What the scene file holds; the editor can change it, so nothing here assumes
        // the 10,000 buildings and 100 lights it was first laid out with.
        m_objectCount =
            m_scene->CountComponents<Geni::MeshComponent>() + m_scene->CountComponents<Geni::SkinnedMeshComponent>();
        m_lightCount = m_scene->CollectLights().size();
        m_instantiation.reset();
        m_loading = false;
        return true;
    }

    void Update(float deltaTime) override
    {
        const auto now = std::chrono::steady_clock::now();
        const double frameMs = std::chrono::duration<double, std::milli>(now - m_lastTime).count();
        m_lastTime = now;

        if (m_loading)
        {
            // The first frame also pays for swapchain and pipeline creation; it is not a
            // loading hitch.
            if (++m_loadingFrames > 1)
            {
                m_slowestLoadingFrameMs = std::max(m_slowestLoadingFrameMs, frameMs);
            }
            m_scene->Update(deltaTime);
            StepLoading();
            return;
        }

        if (Geni::Engine::GetInstance().GetInputManager().IsKeyPressed(GLFW_KEY_ESCAPE))
        {
            SetNeedsToBeClosed(true);
        }

        m_scene->Update(deltaTime);

        ++m_frame;
        if (m_options.frames > 0 && m_frame > m_options.warmup && m_frame <= m_options.frames)
        {
            m_totalMs += frameMs;
            ++m_measured;
        }

        if (m_options.frames > 0 && m_frame >= m_options.frames)
        {
            if (!m_options.screenshotPath.empty())
            {
                Geni::Engine::GetInstance().RequestScreenshot(m_options.screenshotPath);
            }
            SetNeedsToBeClosed(true);
        }
    }

    void Destroy() override
    {
    }

    double MeanFrameMs() const
    {
        return m_measured > 0 ? m_totalMs / static_cast<double>(m_measured) : 0.0;
    }
    double SlowestLoadingFrameMs() const
    {
        return m_slowestLoadingFrameMs;
    }
    size_t ObjectCount() const
    {
        return m_objectCount;
    }
    size_t LightCount() const
    {
        return m_lightCount;
    }

  private:
    Options m_options;
    Geni::Scene *m_scene = nullptr; // owned by the engine
    Geni::AssetHandle<Geni::SceneAsset> m_sceneAsset;
    std::unique_ptr<Geni::SceneInstantiation> m_instantiation;
    bool m_loading = false;
    long m_loadingFrames = 0;
    double m_slowestLoadingFrameMs = 0.0;
    size_t m_objectCount = 0;
    size_t m_lightCount = 0;
    long m_frame = 0;
    long m_measured = 0;
    double m_totalMs = 0.0;
    std::chrono::steady_clock::time_point m_lastTime = std::chrono::steady_clock::now();
};

} // namespace

namespace
{
// Arguments of `city --world`. Returns -1 when they parse, otherwise an exit code.
int ParseWorldOptions(int argc, char **argv, WorldOptions &options)
{
    for (int i = 1; i < argc; ++i)
    {
        const char *arg = argv[i];
        const bool hasValue = i + 1 < argc;
        if (std::strcmp(arg, "--world") == 0)
        {
            if (hasValue && argv[i + 1][0] != '-')
            {
                options.world = argv[++i];
            }
        }
        else if (std::strcmp(arg, "--route") == 0)
        {
            options.route = true;
        }
        else if (std::strcmp(arg, "--speed") == 0 && hasValue)
        {
            options.speed = std::strtod(argv[++i], nullptr);
        }
        else if (std::strcmp(arg, "--distance") == 0 && hasValue)
        {
            options.distance = std::strtod(argv[++i], nullptr);
        }
        else if (std::strcmp(arg, "--altitude") == 0 && hasValue)
        {
            options.altitude = std::strtod(argv[++i], nullptr);
        }
        else if (std::strcmp(arg, "--hitch-ms") == 0 && hasValue)
        {
            options.hitchMs = std::strtod(argv[++i], nullptr);
        }
        else if (std::strcmp(arg, "--platform") == 0 && hasValue)
        {
            options.platform = argv[++i];
        }
        else if (std::strcmp(arg, "--time") == 0 && hasValue)
        {
            options.timeOfDay = std::strtod(argv[++i], nullptr);
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
            options.width = static_cast<int>(std::strtol(argv[++i], nullptr, 10));
        }
        else if (std::strcmp(arg, "--height") == 0 && hasValue)
        {
            options.height = static_cast<int>(std::strtol(argv[++i], nullptr, 10));
        }
        else if (std::strcmp(arg, "--screenshot") == 0 && hasValue)
        {
            options.screenshotPath = argv[++i];
        }
        else
        {
            GENI_LOG_ERROR(
                "Unknown argument '%s'. Usage: city --world [FILE] [--route] [--speed M/S] [--distance M] "
                "[--altitude M] [--hitch-ms MS] [--platform desktop|mobile|web] [--time HOURS] [--frames N] [--hidden] "
                "[--width W] [--height H] [--screenshot FILE]",
                arg
            );
            return 2;
        }
    }
    return -1;
}
} // namespace

// Arguments of `city --play`. Returns -1 when they parse, otherwise an exit code.
int ParsePlayOptions(int argc, char **argv, PlayOptions &options)
{
    for (int i = 1; i < argc; ++i)
    {
        const char *arg = argv[i];
        const bool hasValue = i + 1 < argc;
        if (std::strcmp(arg, "--play") == 0 || std::strcmp(arg, "--no-overlay") == 0)
        {
        }
        else if (std::strcmp(arg, "--autopilot") == 0)
        {
            options.autopilot = true;
        }
        else if (std::strcmp(arg, "--no-crowd") == 0)
        {
            options.crowd = false;
        }
        else if (std::strcmp(arg, "--no-traffic") == 0)
        {
            options.traffic = false;
        }
        else if (std::strcmp(arg, "--title-shot") == 0)
        {
            options.titleOnly = true;
        }
        else if (std::strcmp(arg, "--hidden") == 0)
        {
            options.hidden = true;
        }
        else if (std::strcmp(arg, "--frames") == 0 && hasValue)
        {
            options.frames = std::strtol(argv[++i], nullptr, 10);
        }
        else if (std::strcmp(arg, "--width") == 0 && hasValue)
        {
            options.width = static_cast<int>(std::strtol(argv[++i], nullptr, 10));
        }
        else if (std::strcmp(arg, "--height") == 0 && hasValue)
        {
            options.height = static_cast<int>(std::strtol(argv[++i], nullptr, 10));
        }
        else if (std::strcmp(arg, "--screenshot") == 0 && hasValue)
        {
            options.screenshotPath = argv[++i];
        }
        else
        {
            GENI_LOG_ERROR(
                "Unknown argument '%s'. Usage: city --play [--autopilot] [--no-crowd] [--no-traffic] [--title-shot] "
                "[--frames N] [--hidden] [--width W] [--height H] [--screenshot FILE]",
                arg
            );
            return 2;
        }
    }
    return -1;
}

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--play") == 0)
        {
            PlayOptions playOptions;
            const int code = ParsePlayOptions(argc, argv, playOptions);
            return code >= 0 ? code : RunPlay(playOptions);
        }
    }
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--world") == 0)
        {
            WorldOptions worldOptions;
            const int code = ParseWorldOptions(argc, argv, worldOptions);
            return code >= 0 ? code : RunWorld(worldOptions);
        }
    }

    Options options;
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc)
        {
            options.frames = std::strtol(argv[++i], nullptr, 10);
        }
        else if (std::strcmp(argv[i], "--warmup") == 0 && i + 1 < argc)
        {
            options.warmup = std::strtol(argv[++i], nullptr, 10);
        }
        else if (std::strcmp(argv[i], "--budget-ms") == 0 && i + 1 < argc)
        {
            options.budgetMs = std::strtod(argv[++i], nullptr);
        }
        else if (std::strcmp(argv[i], "--load-budget-ms") == 0 && i + 1 < argc)
        {
            options.loadBudgetMs = std::strtod(argv[++i], nullptr);
        }
        else if (std::strcmp(argv[i], "--hidden") == 0)
        {
            options.hidden = true;
        }
        else if (std::strcmp(argv[i], "--no-overlay") == 0)
        {
            // Accepted for tests/run_screenshot.cmake; hidden runs already have no overlay.
            options.hidden = true;
        }
        else if (std::strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc)
        {
            options.screenshotPath = argv[++i];
        }
        else if (std::strcmp(argv[i], "--width") == 0 && i + 1 < argc)
        {
            options.width = static_cast<int>(std::strtol(argv[++i], nullptr, 10));
        }
        else if (std::strcmp(argv[i], "--height") == 0 && i + 1 < argc)
        {
            options.height = static_cast<int>(std::strtol(argv[++i], nullptr, 10));
        }
        else
        {
            GENI_LOG_ERROR(
                "Unknown argument '%s'. Usage: city [--frames N] [--warmup N] [--budget-ms MS] [--load-budget-ms MS] "
                "[--hidden] "
                "[--width W] [--height H] [--screenshot FILE]",
                argv[i]
            );
            return 2;
        }
    }

    Geni::EngineConfig config;
    config.title = "GeniEngine City Block";
    config.width = options.width;
    config.height = options.height;
    config.visible = !options.hidden;
    config.vsync = false;
    config.debugOverlay = true;
    if (options.hidden)
    {
        // A hidden run is a benchmark or a capture: no overlay, and a frame time that does
        // not depend on the display.
        config.debugOverlay = false;
    }

    auto app = new CityApp(options);
    auto &engine = Geni::Engine::GetInstance();
    engine.SetApplication(app);

    if (!engine.Init(config))
    {
        engine.Destroy();
        return 1;
    }
    engine.Run();
    const double meanMs = app->MeanFrameMs();
    const double slowestLoadingMs = app->SlowestLoadingFrameMs();
    const size_t objectCount = app->ObjectCount();
    const size_t lightCount = app->LightCount();
    engine.Destroy();

    if (const auto errors = Geni::rhi::GetValidationErrorCount())
    {
        GENI_LOG_ERROR("%llu Vulkan validation errors", static_cast<unsigned long long>(errors));
        return 3;
    }

    if (options.frames > 0)
    {
        const double fps = meanMs > 0.0 ? 1000.0 / meanMs : 0.0;
        std::printf(
            "city: %.2f ms/frame (%.1f fps) over %ld measured frames; %zu objects, %zu lights; budget %.2f ms\n", meanMs,
            fps, std::max(0L, options.frames - options.warmup), objectCount, lightCount, options.budgetMs
        );
        if (options.budgetMs > 0.0 && meanMs > options.budgetMs)
        {
            GENI_LOG_ERROR("city block over budget: %.2f ms > %.2f ms", meanMs, options.budgetMs);
            return 4;
        }
        std::printf("city: slowest frame while loading %.2f ms; budget %.2f ms\n", slowestLoadingMs, options.loadBudgetMs);
        if (options.loadBudgetMs > 0.0 && slowestLoadingMs > options.loadBudgetMs)
        {
            GENI_LOG_ERROR(
                "city block loading hitch: %.2f ms > %.2f ms", slowestLoadingMs, options.loadBudgetMs
            );
            return 5;
        }
    }
    return 0;
}