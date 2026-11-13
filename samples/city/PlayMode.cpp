// The reference game (ROADMAP M6): `city --play`.
//
// C++ only sets the stage: the streamed metro world, navigation tiles that follow it,
// the game's input bindings, and one "Game" object whose ScriptComponent runs
// assets/scripts/game.lua. Everything the player sees and does (the menus, the HUD,
// getting in the car, pedestrians and traffic) is Lua on top of the engine's components.

#include "PlayMode.h"

#include <geni.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace
{

constexpr double kStep = 1.0 / 60.0;

class PlayApp : public Geni::Application
{
  public:
    explicit PlayApp(const PlayOptions &options) : m_options(options)
    {
    }

    bool Init() override
    {
        auto &engine = Geni::Engine::GetInstance();
#if defined(GENI_SAMPLE_ASSETS_DIR)
        std::error_code ec;
        if (!engine.GetFileSystem().IsPackaged() && std::filesystem::exists(GENI_SAMPLE_ASSETS_DIR, ec))
        {
            // Scripts, graphs and bindings are edited in the source tree while playing.
            engine.GetFileSystem().SetAssetsFolder(GENI_SAMPLE_ASSETS_DIR);
        }
#endif
        // The game's bindings (docs/design/0009), editable without a rebuild.
        const std::string bindings = engine.GetFileSystem().LoadAssetFileText("input/actions.json");
        if (!bindings.empty())
        {
            std::string error;
            if (!engine.GetInputActions().LoadJson(nlohmann::json::parse(bindings, nullptr, false), true, &error))
            {
                GENI_LOG_WARN("input/actions.json: %s", error.c_str());
            }
        }

        std::string error;
        if (!engine.OpenWorld("worlds/metro/metro.gworld", &error))
        {
            GENI_LOG_ERROR("Could not open the metro world: %s", error.c_str());
            return false;
        }
        m_scene = engine.GetScene();
        // One navigation tile per loaded cell, built on workers as cells stream in.
        engine.GetNavigation().AttachToWorld(engine.GetWorldStreamer(), *m_scene, &engine.GetAssetManager());

        // The loading screen: the cells around the start, their collision and their
        // navigation tiles.
        const auto start = std::chrono::steady_clock::now();
        engine.GetWorldStreamer().SetFocus(glm::dvec3(800.0, 0.0, 824.0));
        engine.GetWorldStreamer().Flush();
        engine.GetWorldStreamer().ClearFocus();
        engine.GetNavigation().FinishBuilds();
        engine.GetPhysicsManager().FlushPendingBodies();
        GENI_LOG_INFO(
            "Geni City loaded in %.0f ms: %u cells, %u navigation tiles (slowest tile %.0f ms)",
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count(),
            engine.GetWorldStreamer().GetStats().cellsLoaded, engine.GetNavigation().GetStats().tiles,
            engine.GetNavigation().GetStats().peakBuildMs
        );

        char options[256];
        std::snprintf(
            options, sizeof(options), "game_options = { autopilot = %s, crowd = %s, traffic = %s, title_only = %s%s }",
            m_options.autopilot ? "true" : "false", m_options.crowd ? "true" : "false",
            m_options.traffic ? "true" : "false", m_options.titleOnly ? "true" : "false",
            m_options.autopilot && !m_options.screenshotPath.empty() ? ", screenshot = SHOT" : ""
        );
        std::string code = options;
        if (m_options.autopilot && !m_options.screenshotPath.empty())
        {
            // The autopilot takes its own picture while driving.
            code.replace(code.find("SHOT"), 4, "[[" + m_options.screenshotPath + "]]");
        }
        if (!engine.GetScripts().Run(code, "game_options"))
        {
            return false;
        }

        Geni::GameObject *game = m_scene->CreateObject("Game");
        auto *script = new Geni::ScriptComponent();
        script->SetScript("scripts/game.lua");
        game->AddComponent(script);
        return true;
    }

    void Update(float deltaTime) override
    {
        const auto now = std::chrono::steady_clock::now();
        if (m_frame > 2)
        {
            // The first frames also create pipelines; they are not counted.
            m_frameMs.push_back(std::chrono::duration<double, std::milli>(now - m_lastTime).count());
        }
        m_lastTime = now;
        m_scene->Update(deltaTime);
        ++m_frame;
        if (m_options.frames > 0 && m_frame >= m_options.frames)
        {
            if (!m_options.screenshotPath.empty() && !m_options.autopilot)
            {
                Geni::Engine::GetInstance().RequestScreenshot(m_options.screenshotPath);
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
    std::vector<double> FrameTimes() const
    {
        return m_frameMs;
    }

  private:
    PlayOptions m_options;
    Geni::Scene *m_scene = nullptr;
    long m_frame = 0;
    std::vector<double> m_frameMs;
    std::chrono::steady_clock::time_point m_lastTime = std::chrono::steady_clock::now();
};

} // namespace

int RunPlay(const PlayOptions &options)
{
    Geni::EngineConfig config;
    config.title = "Geni City";
    config.width = options.width;
    config.height = options.height;
    config.visible = !options.hidden;
    config.vsync = !options.hidden;
    // The game UI draws with Dear ImGui; the debug overlay starts hidden (F1).
    config.debugOverlay = true;
    config.debugOverlayVisible = false;
    config.renderer.fogDensity = 0.0012f;
    if (options.hidden || options.autopilot)
    {
        // Repeatable: every frame is one 1/60 s step, whatever the machine's speed.
        config.deterministicFrameTime = kStep;
    }

    auto app = new PlayApp(options);
    auto &engine = Geni::Engine::GetInstance();
    engine.SetApplication(app);
    if (!engine.Init(config))
    {
        engine.Destroy();
        return 1;
    }
    engine.Run();

    int code = 0;
    if (options.autopilot)
    {
        std::string ok;
        std::string message;
        engine.GetScripts().Evaluate("autopilot_result and tostring(autopilot_result.ok) or 'nil'", ok);
        engine.GetScripts().Evaluate("autopilot_result and autopilot_result.message or 'the run did not finish'", message);
        std::printf("play: autopilot %s: %s\n", ok == "true" ? "passed" : "FAILED", message.c_str());
        if (ok != "true")
        {
            code = 9;
        }
    }
    std::vector<double> times = app->FrameTimes();
    if (!times.empty())
    {
        double mean = 0.0;
        for (double t : times)
        {
            mean += t;
        }
        mean /= static_cast<double>(times.size());
        std::sort(times.begin(), times.end());
        const double p99 = times[std::min(times.size() - 1, static_cast<size_t>(0.99 * static_cast<double>(times.size())))];
        const Geni::NavStats &nav = engine.GetNavigation().GetStats();
        std::printf(
            "play: %ld frames; frame mean %.2f ms, p99 %.2f ms, max %.2f ms; %llu navigation tiles built, slowest %.0f ms\n",
            app->Frames(), mean, p99, times.back(), static_cast<unsigned long long>(nav.tilesBuilt), nav.peakBuildMs
        );
    }
    const uint64_t scriptErrors = engine.GetScripts().GetErrorCount();
    engine.Destroy();

    if (const auto errors = Geni::rhi::GetValidationErrorCount())
    {
        GENI_LOG_ERROR("%llu Vulkan validation errors", static_cast<unsigned long long>(errors));
        return 3;
    }
    if (scriptErrors > 0)
    {
        GENI_LOG_ERROR("play: %llu Lua errors", static_cast<unsigned long long>(scriptErrors));
        return code != 0 ? code : 10;
    }
    return code;
}
