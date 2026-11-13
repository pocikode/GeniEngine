// The streamed world of the reference game (ROADMAP M5, docs/design/0007): 4 km x 4 km of
// city, 16 x 16 cells of 256 m, streamed around the camera with HLOD for distant cells,
// mesh LODs, camera-relative rendering and origin rebasing.
//
// Interactive by default (fly with W/A/S/D, Q/E, Shift, right mouse button). With --route
// the camera flies a straight line across the world at vehicle speed, one fixed 1/60 s
// step per frame, which makes it the M5 check: no hitches, no cell loaded late, and
// memory inside the platform budget all the way.

#include "WorldMode.h"

#include <geni.h>

#include <GLFW/glfw3.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <glm/gtc/quaternion.hpp>
#include <vector>

namespace
{

constexpr double kStep = 1.0 / 60.0;

class WorldApp : public Geni::Application
{
  public:
    explicit WorldApp(const WorldOptions &options) : m_options(options)
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
        std::string error;
        if (!engine.OpenWorld(m_options.world, &error))
        {
            GENI_LOG_ERROR("Could not open %s: %s", m_options.world.c_str(), error.c_str());
            return false;
        }
        m_scene = engine.GetScene();
        if (m_options.timeOfDay >= 0.0)
        {
            Geni::GameObject *sun = m_scene->FindObject("Sun");
            if (!sun)
            {
                GENI_LOG_ERROR("--time: the world has no object named Sun");
                return false;
            }
            auto *clock = new Geni::TimeOfDayComponent();
            clock->SetTime(static_cast<float>(m_options.timeOfDay));
            clock->SetDayLengthMinutes(0.0f);
            sun->AddComponent(clock);
        }

        auto &streamer = engine.GetWorldStreamer();
        const Geni::WorldDescription &world = *streamer.GetDescription();
        if (m_options.route)
        {
            // Corner to corner (or `distance` metres through the centre), looking ahead.
            const glm::dvec3 centre =
                world.origin + glm::dvec3(world.cellsX * 0.5, 0.0, world.cellsZ * 0.5) * double(world.cellSize);
            const double halfDiagonal =
                0.5 * std::sqrt(2.0) *
                (std::min(world.cellsX, world.cellsZ) * double(world.cellSize) - 2.0 * world.cellSize);
            const double half = m_options.distance > 0.0 ? 0.5 * m_options.distance : halfDiagonal;
            m_direction = glm::normalize(glm::dvec3(1.0, 0.0, 1.0));
            m_start = centre - m_direction * half + glm::dvec3(0.0, m_options.altitude, 0.0);
            m_length = 2.0 * half;
            if (Geni::GameObject *camera = m_scene->GetMainCamera())
            {
                if (auto *fly = camera->GetComponent<Geni::FlyCameraComponent>())
                {
                    camera->RemoveComponent(fly);
                }
                const float yaw = static_cast<float>(std::atan2(-m_direction.x, -m_direction.z));
                camera->SetRotation(
                    glm::angleAxis(yaw, glm::vec3(0, 1, 0)) * glm::angleAxis(glm::radians(-8.0f), glm::vec3(1, 0, 0))
                );
            }
            PlaceCamera(0.0);
        }

        // The loading screen: everything around the start is in before the first frame.
        const auto start = std::chrono::steady_clock::now();
        streamer.Flush();
        const double loadMs =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        const auto &stats = streamer.GetStats();
        GENI_LOG_INFO(
            "World loaded in %.0f ms: %u cells, %u HLOD cells, %u nodes, %.1f MiB of assets", loadMs, stats.cellsLoaded,
            stats.hlodCells, stats.loadedNodes, static_cast<double>(stats.residentGpuBytes) / (1024.0 * 1024.0)
        );
        m_lastTime = std::chrono::steady_clock::now();
        return true;
    }

    void PlaceCamera(double travelled)
    {
        if (Geni::GameObject *camera = m_scene->GetMainCamera())
        {
            // The camera may have a parent, which moves with origin shifts too.
            glm::vec3 position = m_scene->ToScene(m_start + m_direction * travelled);
            if (Geni::GameObject *parent = camera->GetParent())
            {
                position = glm::vec3(glm::inverse(parent->GetWorldTransform()) * glm::vec4(position, 1.0f));
            }
            camera->SetPosition(position);
        }
    }

    void Update(float deltaTime) override
    {
        auto &engine = Geni::Engine::GetInstance();
        const auto now = std::chrono::steady_clock::now();
        const double frameMs = std::chrono::duration<double, std::milli>(now - m_lastTime).count();
        m_lastTime = now;
        // The first frames after loading also create pipelines and the swapchain's
        // targets; like the city benchmark, they are not counted as hitches.
        if (m_frame > 2)
        {
            m_frameMs.push_back(frameMs);
        }

        if (engine.GetInputManager().IsKeyPressed(GLFW_KEY_ESCAPE))
        {
            SetNeedsToBeClosed(true);
        }

        bool finished = false;
        if (m_options.route)
        {
            m_travelled += m_options.speed * kStep;
            if (m_travelled >= m_length)
            {
                m_travelled = m_length;
                finished = true;
            }
            PlaceCamera(m_travelled);
        }

        m_scene->Update(deltaTime);
        ++m_frame;

        if (m_options.frames > 0 && m_frame >= m_options.frames)
        {
            finished = true;
        }
        if (finished)
        {
            if (!m_options.screenshotPath.empty())
            {
                engine.RequestScreenshot(m_options.screenshotPath);
            }
            SetNeedsToBeClosed(true);
        }
    }

    void Destroy() override
    {
    }

    long Frames() const
    {
        return m_frame;
    }
    double Travelled() const
    {
        return m_travelled;
    }
    std::vector<double> FrameTimes() const
    {
        return m_frameMs;
    }

  private:
    WorldOptions m_options;
    Geni::Scene *m_scene = nullptr;
    glm::dvec3 m_start{0.0};
    glm::dvec3 m_direction{1.0, 0.0, 0.0};
    double m_length = 0.0;
    double m_travelled = 0.0;
    long m_frame = 0;
    std::vector<double> m_frameMs;
    std::chrono::steady_clock::time_point m_lastTime;
};

double Percentile(std::vector<double> values, double fraction)
{
    if (values.empty())
    {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    const size_t index =
        std::min(values.size() - 1, static_cast<size_t>(fraction * static_cast<double>(values.size())));
    return values[index];
}

} // namespace

int RunWorld(const WorldOptions &options)
{
    Geni::EngineConfig config;
    config.title = "GeniEngine City: metro world";
    config.width = options.width;
    config.height = options.height;
    config.visible = !options.hidden;
    config.vsync = !options.route && !options.hidden;
    config.debugOverlay = !options.hidden;
    // The world is seen from far away: the HLOD reaches about 1.5 km.
    config.renderer.fogDensity = 0.0012f;
    if (options.route)
    {
        // One 1/60 s step per frame: the streamer must keep up with a car at 60 fps,
        // whatever this machine's frame rate is.
        config.deterministicFrameTime = kStep;
    }
    if (options.platform == "mobile")
    {
        config.streaming = Geni::StreamingBudget::ForPlatform(Geni::StreamingPlatform::Mobile);
    }
    else if (options.platform == "web")
    {
        config.streaming = Geni::StreamingBudget::ForPlatform(Geni::StreamingPlatform::Web);
    }
    else if (!options.platform.empty() && options.platform != "desktop")
    {
        GENI_LOG_ERROR("--platform must be desktop, mobile or web");
        return 2;
    }

    auto app = new WorldApp(options);
    auto &engine = Geni::Engine::GetInstance();
    engine.SetApplication(app);
    if (!engine.Init(config))
    {
        engine.Destroy();
        return 1;
    }
    engine.Run();
    const long frames = app->Frames();
    const double travelled = app->Travelled();
    const std::vector<double> times = app->FrameTimes();
    const Geni::StreamingStats stats = engine.GetWorldStreamer().GetStats();
    const Geni::StreamingBudget budget = engine.GetWorldStreamer().GetBudget();
    engine.Destroy();

    if (const auto errors = Geni::rhi::GetValidationErrorCount())
    {
        GENI_LOG_ERROR("%llu Vulkan validation errors", static_cast<unsigned long long>(errors));
        return 3;
    }

    const double maxMs = times.empty() ? 0.0 : *std::max_element(times.begin(), times.end());
    double mean = 0.0;
    for (double t : times)
    {
        mean += t;
    }
    mean = times.empty() ? 0.0 : mean / static_cast<double>(times.size());
    constexpr double kMiB = 1024.0 * 1024.0;
    std::printf(
        "world: %ld frames, %.0f m at %.0f m/s; frame mean %.2f ms, p50 %.2f, p99 %.2f, max %.2f ms\n", frames,
        travelled, options.speed, mean, Percentile(times, 0.5), Percentile(times, 0.99), maxMs
    );
    std::printf(
        "world: %llu cell loads, %llu unloads, %u origin shifts, peak %u nodes (budget %u), peak %.1f MiB assets "
        "(budget %.0f MiB), streaming peak %.2f ms, late frames %llu\n",
        static_cast<unsigned long long>(stats.cellLoadsStarted),
        static_cast<unsigned long long>(stats.cellsUnloadedTotal), stats.originShifts, stats.peakLoadedNodes,
        budget.maxLoadedNodes, static_cast<double>(stats.peakResidentGpuBytes) / kMiB,
        static_cast<double>(budget.gpuMemoryBytes) / kMiB, stats.peakUpdateMs,
        static_cast<unsigned long long>(stats.lateFrames)
    );

    if (options.hitchMs > 0.0 && maxMs > options.hitchMs)
    {
        GENI_LOG_ERROR("world: hitch, a frame took %.2f ms (budget %.2f ms)", maxMs, options.hitchMs);
        return 6;
    }
    if (options.route && stats.lateFrames > 0)
    {
        GENI_LOG_ERROR(
            "world: %llu frames with a nearby cell not loaded", static_cast<unsigned long long>(stats.lateFrames)
        );
        return 7;
    }
    if (stats.peakResidentGpuBytes > budget.gpuMemoryBytes || stats.peakLoadedNodes > budget.maxLoadedNodes)
    {
        GENI_LOG_ERROR("world: over the memory budget");
        return 8;
    }
    return 0;
}
