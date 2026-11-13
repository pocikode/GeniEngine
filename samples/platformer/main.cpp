// Geni Plains: a small 2D platformer (docs/design/0017).
//
// The level is assets/scenes/level.gscene, made by tools/gen_2d_assets.py and edited in
// the editor (cmake --build build/dev --target edit_platformer): a tilemap that collides,
// the player on the character controller, crates on 2D physics, animated coins and
// parallax backgrounds. This file is the game's rules: input, the hero's animation, the
// camera, coins, particle effects (coin sparkles, landing dust; docs/design/0019) and the
// HUD.
//
// Usage: platformer [--hidden] [--width W] [--height H] [--frames N] [--screenshot FILE]
//                   [--autopilot] [--benchmark N] [--particles N] [--budget-ms MS]
//   --autopilot     plays the level through the input actions and exits with code 9 if
//                   it does not reach the end or collects fewer than a quarter of the
//                   coins (it only runs and jumps; most coins are on platforms)
//   --benchmark N   adds N moving, animated sprites and reports the mean frame time;
//                   exits with code 4 when it is over --budget-ms
//   --particles N   keeps about N particles alive in emitters around the camera and
//                   reports the mean frame time; exits with code 4 over --budget-ms
//   --frames N      quits after N frames (--screenshot saves the last one)

#include <geni.h>

#include <GLFW/glfw3.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace
{

struct Options
{
    bool hidden = false;
    int width = 1280;
    int height = 720;
    long frames = 0;
    std::string screenshotPath;
    bool autopilot = false;
    int benchmarkSprites = 0;
    int benchmarkParticles = 0;
    double budgetMs = 0.0;

    bool Benchmarking() const
    {
        return benchmarkSprites > 0 || benchmarkParticles > 0;
    }
};

constexpr float kStep = 1.0f / 60.0f;
// The level ends here (the last columns before the right-hand wall).
constexpr float kGoalX = 92.0f;

struct Parallax
{
    Geni::GameObject *object = nullptr;
    float factor = 0.0f;
};

struct BenchSprite
{
    Geni::GameObject *object = nullptr;
    glm::vec2 origin{0.0f};
    float phase = 0.0f;
};

class PlatformerApp : public Geni::Application
{
  public:
    explicit PlatformerApp(const Options &options) : m_options(options)
    {
    }

    bool Init() override
    {
        auto &engine = Geni::Engine::GetInstance();
#if defined(GENI_SAMPLE_ASSETS_DIR)
        // Prefer the source tree when it is there, so a level saved in the editor is what
        // the next run loads. A packaged build reads the assets it ships.
        std::error_code ec;
        if (!engine.GetFileSystem().IsPackaged() && std::filesystem::exists(GENI_SAMPLE_ASSETS_DIR, ec))
        {
            engine.GetFileSystem().SetAssetsFolder(GENI_SAMPLE_ASSETS_DIR);
        }
#endif
        const std::string bindings = engine.GetFileSystem().LoadAssetFileText("input/actions.json");
        std::string error;
        if (bindings.empty() ||
            !engine.GetInputActions().LoadJson(nlohmann::json::parse(bindings, nullptr, false), true, &error))
        {
            GENI_LOG_ERROR("input/actions.json: %s", bindings.empty() ? "missing" : error.c_str());
            return false;
        }

        // Colours as painted (docs/design/0017): no filmic curve, no 3D sky.
        auto &renderer = engine.GetRenderer();
        renderer.SetTonemapper(Geni::Tonemapper::None);
        renderer.SetExposure(1.0f);
        renderer.SetBackground(false, glm::vec3(0.36f, 0.58f, 0.86f));

        m_scene = new Geni::Scene();
        engine.SetScene(m_scene);
        auto &assets = engine.GetAssetManager();
        const Geni::AssetID sceneID = assets.FindBySource(Geni::AssetType::Scene, "scenes/level.gscene");
        auto level = assets.LoadScene(sceneID, false);
        Geni::SerializationContext context{m_scene, &assets};
        if (!level.IsReady() || !Geni::SceneSerializer::Instantiate(*m_scene, *level.Get(), context, &error))
        {
            GENI_LOG_ERROR("Could not load scenes/level.gscene: %s", error.empty() ? "not cooked?" : error.c_str());
            return false;
        }
        // A handful of small textures: wait for them so the first frame is complete.
        assets.WaitForAll();
        engine.GetPhysicsManager().FlushPendingBodies();

        m_player = m_scene->FindObject("Player");
        m_body = m_scene->FindObject("Body");
        m_camera = m_scene->GetMainCamera();
        auto *tilemap = m_scene->FindObject("Tilemap");
        m_character = m_player ? m_player->GetComponent<Geni::CharacterControllerComponent>() : nullptr;
        m_sprite = m_body ? m_body->GetComponent<Geni::SpriteComponent>() : nullptr;
        m_animator = m_body ? m_body->GetComponent<Geni::SpriteAnimatorComponent>() : nullptr;
        m_tilemap = tilemap ? tilemap->GetComponent<Geni::TilemapComponent>() : nullptr;
        if (!m_character || !m_sprite || !m_animator || !m_camera || !m_tilemap)
        {
            GENI_LOG_ERROR("The level needs Player (character), Body (sprite, animator), Tilemap and a camera");
            return false;
        }
        m_cameraComponent = m_camera->GetComponent<Geni::CameraComponent>();

        Collect(m_scene->FindObject("Level"));
        m_startX = m_player->GetWorldPosition().x;
        AddEffects();
        if (m_options.benchmarkSprites > 0)
        {
            AddBenchmarkSprites(m_options.benchmarkSprites);
        }
        if (m_options.benchmarkParticles > 0)
        {
            AddBenchmarkParticles(m_options.benchmarkParticles);
        }
        m_lastTime = std::chrono::steady_clock::now();
        GENI_LOG_INFO("Geni Plains: %zu coins, %zu crates", m_coins.size(), m_crates);
        return true;
    }

    void Update(float deltaTime) override
    {
        auto &engine = Geni::Engine::GetInstance();
        const auto now = std::chrono::steady_clock::now();
        const double frameMs = std::chrono::duration<double, std::milli>(now - m_lastTime).count();
        m_lastTime = now;
        ++m_frame;
        m_time += deltaTime;

        auto &actions = engine.GetInputActions();
        if (m_options.autopilot)
        {
            Autopilot();
        }
        if (actions.WasPressed("pause") && !m_options.autopilot)
        {
            SetNeedsToBeClosed(true);
        }

        const float move = std::clamp(actions.GetValue("move_x"), -1.0f, 1.0f);
        m_character->SetMoveInput(glm::vec3(move, 0.0f, 0.0f), actions.IsDown("run"));
        if (actions.WasPressed("jump"))
        {
            m_character->Jump();
        }

        AnimateHero(move);
        CollectCoins();
        LandingDust();
        UpdateBenchmark();

        m_scene->Update(deltaTime);
        FollowCamera();
        DrawHud();

        if (m_player->GetWorldPosition().x >= kGoalX && m_finishedFrame == 0)
        {
            m_finishedFrame = m_frame;
            GENI_LOG_INFO("Level complete at %.1f s with %d of %zu coins", m_time, m_collected, m_coins.size());
        }
        if (m_player->GetWorldPosition().y < -5.0f)
        {
            // Fell into a pit: back to the start of the level.
            m_character->Teleport(glm::vec3(m_startX, 4.05f, 0.0f));
            ++m_falls;
        }

        if (m_options.Benchmarking() && m_frame > kBenchmarkWarmup)
        {
            m_totalMs += frameMs;
            ++m_measured;
        }
        if (m_options.frames > 0)
        {
            if (m_frame + 1 == m_options.frames && !m_options.screenshotPath.empty())
            {
                engine.RequestScreenshot(m_options.screenshotPath);
            }
            if (m_frame >= m_options.frames)
            {
                SetNeedsToBeClosed(true);
            }
        }
        // The autopilot gives up after a minute of game time.
        if (m_options.autopilot && (m_finishedFrame > 0 || m_time > 60.0f))
        {
            SetNeedsToBeClosed(true);
        }
    }

    void Render() override
    {
    }

    void Destroy() override
    {
    }

    int Collected() const
    {
        return m_collected;
    }
    size_t CoinCount() const
    {
        return m_coins.size();
    }
    bool Finished() const
    {
        return m_finishedFrame > 0;
    }
    float Time() const
    {
        return m_time;
    }
    int Falls() const
    {
        return m_falls;
    }
    double MeanFrameMs() const
    {
        return m_measured > 0 ? m_totalMs / static_cast<double>(m_measured) : 0.0;
    }

  private:
    static constexpr long kBenchmarkWarmup = 30;

    void Collect(Geni::GameObject *root)
    {
        if (!root)
        {
            return;
        }
        for (const auto &child : root->GetChildren())
        {
            const std::string &name = child->GetName();
            if (name.rfind("Coin", 0) == 0)
            {
                m_coins.push_back(child.get());
            }
            else if (name.rfind("Crate", 0) == 0)
            {
                ++m_crates;
            }
            else if (name.rfind("Parallax ", 0) == 0)
            {
                m_parallax.push_back({child.get(), std::strtof(name.c_str() + 9, nullptr)});
            }
        }
    }

    // Presses the same actions a player would: always right, run, and jump at walls, gaps
    // and crates.
    void Autopilot()
    {
        auto &actions = Geni::Engine::GetInstance().GetInputActions();
        const glm::vec3 feet = m_player->GetWorldPosition();
        auto solidAt = [&](float x, float y) {
            const glm::ivec2 cell = m_tilemap->WorldToCell(glm::vec3(x, y, 0.0f));
            return m_tilemap->IsSolid(cell.x, cell.y);
        };
        const bool wallAhead = solidAt(feet.x + 0.9f, feet.y + 0.5f);
        const bool gapAhead = !solidAt(feet.x + 1.2f, feet.y - 0.5f) && !solidAt(feet.x + 1.2f, feet.y - 1.5f);
        const bool crateAhead = std::fabs(m_character->GetVelocity().x) < 0.5f && m_time > 0.5f;
        actions.SetOverride("move_x", 1.0f);
        actions.SetOverride("run", 1.0f);
        const bool jump = m_character->IsGrounded() && (wallAhead || gapAhead || crateAhead);
        actions.SetOverride("jump", jump ? 1.0f : 0.0f);
    }

    void AnimateHero(float move)
    {
        if (move > 0.1f)
        {
            m_sprite->SetFlipX(false);
        }
        else if (move < -0.1f)
        {
            m_sprite->SetFlipX(true);
        }
        const glm::vec3 velocity = m_character->GetVelocity();
        if (!m_character->IsGrounded())
        {
            m_animator->Play(velocity.y > 0.0f ? 6 : 7, 1, 1.0f, false);
        }
        else if (std::fabs(velocity.x) > 0.5f)
        {
            m_animator->Play(2, 4, 4.0f + std::fabs(velocity.x) * 1.2f, true);
        }
        else
        {
            m_animator->Play(0, 2, 2.5f, true);
        }
    }

    void CollectCoins()
    {
        const glm::vec3 center = m_player->GetWorldPosition() + glm::vec3(0.0f, 0.45f, 0.0f);
        for (auto *coin : m_coins)
        {
            if (coin->IsActive() && glm::distance(center, coin->GetWorldPosition()) < 0.75f)
            {
                coin->SetActive(false);
                ++m_collected;
                m_sparkles->GetOwner()->SetPosition(coin->GetWorldPosition());
                m_sparkles->Emit(14);
            }
        }
    }

    // Two emitters the game moves to where something happens; both are world space, so
    // what they emitted stays put when they move.
    void AddEffects()
    {
        auto *effects = m_scene->CreateObject("Effects");

        m_sparkles = new Geni::ParticleEmitterComponent();
        m_sparkles->SetRate(0.0f);
        m_sparkles->SetSpread(180.0f);
        m_sparkles->SetSpeedMin(1.5f);
        m_sparkles->SetSpeedMax(3.5f);
        m_sparkles->SetLifetimeMin(0.35f);
        m_sparkles->SetLifetimeMax(0.6f);
        m_sparkles->SetGravity(glm::vec3(0.0f, -4.0f, 0.0f));
        m_sparkles->SetDrag(2.0f);
        m_sparkles->SetStartSize(0.16f);
        m_sparkles->SetEndSize(0.04f);
        m_sparkles->SetStartColor(glm::vec4(1.0f, 0.93f, 0.45f, 1.0f));
        m_sparkles->SetEndColor(glm::vec4(1.0f, 0.6f, 0.1f, 0.0f));
        m_sparkles->SetBlend(Geni::SpriteBlend::Additive);
        m_sparkles->SetFilter(Geni::SpriteFilter::Nearest);
        m_sparkles->SetLayer(30);
        m_sparkles->SetMaxParticles(256);
        m_scene->CreateObject("Coin Sparkles", effects)->AddComponent(m_sparkles);

        m_dust = new Geni::ParticleEmitterComponent();
        m_dust->SetRate(0.0f);
        m_dust->SetShape(Geni::EmitterShape::Box);
        m_dust->SetBoxSize(glm::vec3(0.5f, 0.05f, 0.0f));
        m_dust->SetSpread(70.0f);
        m_dust->SetSpeedMin(0.4f);
        m_dust->SetSpeedMax(1.2f);
        m_dust->SetLifetimeMin(0.3f);
        m_dust->SetLifetimeMax(0.5f);
        m_dust->SetDrag(3.0f);
        m_dust->SetStartSize(0.12f);
        m_dust->SetEndSize(0.3f);
        m_dust->SetStartColor(glm::vec4(0.87f, 0.80f, 0.66f, 0.8f));
        m_dust->SetEndColor(glm::vec4(0.87f, 0.80f, 0.66f, 0.0f));
        m_dust->SetFilter(Geni::SpriteFilter::Nearest);
        m_dust->SetLayer(15);
        m_dust->SetMaxParticles(128);
        m_scene->CreateObject("Landing Dust", effects)->AddComponent(m_dust);
    }

    // A puff at the hero's feet after a real fall (not on every small step).
    void LandingDust()
    {
        const bool grounded = m_character->IsGrounded();
        if (!grounded)
        {
            m_airTime += kStep;
            m_fallSpeed = std::min(m_fallSpeed, m_character->GetVelocity().y);
        }
        else
        {
            if (m_airTime > 0.25f && m_fallSpeed < -4.0f)
            {
                m_dust->GetOwner()->SetPosition(m_player->GetWorldPosition());
                m_dust->Emit(10);
            }
            m_airTime = 0.0f;
            m_fallSpeed = 0.0f;
        }
    }

    void FollowCamera()
    {
        const Geni::SceneViewport rect = Geni::Engine::GetInstance().GetRenderer().GetSceneRect();
        const float aspect = static_cast<float>(rect.width) / static_cast<float>(std::max(1u, rect.height));
        const float halfHeight = 0.5f * m_cameraComponent->GetOrthoSize();
        const float halfWidth = halfHeight * aspect;
        const float mapWidth = static_cast<float>(m_tilemap->GetWidth()) * m_tilemap->GetTileSize().x;
        const float mapHeight = static_cast<float>(m_tilemap->GetHeight()) * m_tilemap->GetTileSize().y;
        const glm::vec3 target = m_player->GetWorldPosition();
        glm::vec3 position = m_camera->GetPosition();
        position.x = std::clamp(target.x, halfWidth, std::max(halfWidth, mapWidth - halfWidth));
        const float wantedY = std::clamp(target.y + 2.5f, halfHeight, std::max(halfHeight, mapHeight - halfHeight));
        // Follow height smoothly; horizontal follow is exact so pixel art does not swim.
        position.y += (wantedY - position.y) * std::min(1.0f, 6.0f * kStep);
        m_camera->SetPosition(position);
        for (const auto &layer : m_parallax)
        {
            layer.object->SetPosition(glm::vec3(position.x * layer.factor, position.y * layer.factor * 0.3f, 0.0f));
        }
    }

    void DrawHud()
    {
        auto &ui = Geni::Engine::GetInstance().GetGameUI();
        if (!ui.IsAvailable())
        {
            return;
        }
        ui.Panel(Geni::UiAnchor::TopLeft, glm::vec2(32.0f, 32.0f), glm::vec2(300.0f, 72.0f), ui.GetStyle().panel);
        char text[64];
        std::snprintf(text, sizeof(text), "Coins  %d / %zu", m_collected, m_coins.size());
        ui.Text(text, Geni::UiAnchor::TopLeft, glm::vec2(56.0f, 46.0f), 40.0f, ui.GetStyle().accent);
        if (m_finishedFrame > 0)
        {
            ui.Text("Level complete!", Geni::UiAnchor::Center, glm::vec2(0.0f, -80.0f), 72.0f, ui.GetStyle().text);
        }
        else if (m_time < 4.0f && !m_options.autopilot)
        {
            ui.Text(
                "A / D to move, Space to jump, Shift to run", Geni::UiAnchor::Bottom, glm::vec2(0.0f, 60.0f), 34.0f,
                ui.GetStyle().text
            );
        }
    }

    void AddBenchmarkSprites(int count)
    {
        // Coins above the level, each its own object, sprite and animator: the cost a game
        // pays for many independent sprites.
        const Geni::SpriteComponent *coinSprite =
            m_coins.empty() ? nullptr : m_coins.front()->GetComponent<Geni::SpriteComponent>();
        auto *group = m_scene->CreateObject("Benchmark");
        Geni::SerializationContext context{m_scene, &Geni::Engine::GetInstance().GetAssetManager()};
        for (int i = 0; i < count; ++i)
        {
            auto *object = m_scene->CreateObject("Bench", group);
            auto *sprite = new Geni::SpriteComponent();
            if (coinSprite)
            {
                // The coin's region of the level's sprite atlas.
                sprite->SetAtlasID(coinSprite->GetAtlasID());
                sprite->SetRegion(coinSprite->GetRegion());
                sprite->SetTextureID(coinSprite->GetTextureID());
                sprite->ResolveAssets(context);
            }
            sprite->SetColumns(6);
            sprite->SetSize(glm::vec2(0.4f));
            sprite->SetLayer(20);
            sprite->SetFilter(Geni::SpriteFilter::Nearest);
            object->AddComponent(sprite);
            auto *animator = new Geni::SpriteAnimatorComponent();
            object->AddComponent(animator);
            animator->Play(i % 6, 6, 10.0f, true);
            BenchSprite bench;
            bench.object = object;
            bench.origin =
                glm::vec2(static_cast<float>(i % 100) * 0.2f + 1.0f, 5.0f + static_cast<float>(i / 100) * 0.06f);
            bench.phase = static_cast<float>(i) * 0.37f;
            m_bench.push_back(bench);
        }
    }

    // Emitters spread across the view, each keeping count / 16 particles alive.
    void AddBenchmarkParticles(int count)
    {
        constexpr int kEmitters = 16;
        const float lifetime = 2.0f;
        auto *group = m_scene->CreateObject("Particle Benchmark");
        for (int i = 0; i < kEmitters; ++i)
        {
            auto *emitter = new Geni::ParticleEmitterComponent();
            const int each = count / kEmitters;
            // A steady stream with room to spare, so the pool never drops a particle.
            emitter->SetMaxParticles(each + each / 4);
            emitter->SetRate(static_cast<float>(each) / lifetime);
            emitter->SetLifetimeMin(lifetime);
            emitter->SetLifetimeMax(lifetime);
            emitter->SetSpread(180.0f);
            emitter->SetSpeedMin(0.2f);
            emitter->SetSpeedMax(1.5f);
            emitter->SetSpinMin(-90.0f);
            emitter->SetSpinMax(90.0f);
            emitter->SetStartSize(0.15f);
            emitter->SetEndSize(0.05f);
            emitter->SetStartColor(glm::vec4(1.0f, 0.8f, 0.4f, 1.0f));
            emitter->SetEndColor(glm::vec4(1.0f, 0.3f, 0.1f, 0.0f));
            emitter->SetBlend(i % 2 == 0 ? Geni::SpriteBlend::Additive : Geni::SpriteBlend::Alpha);
            emitter->SetSeed(i + 1);
            emitter->SetLayer(25);
            auto *object = m_scene->CreateObject("Emitter", group);
            object->AddComponent(emitter);
            m_benchEmitters.push_back(object);
        }
        // Run one lifetime up front so the measured frames start at the full count.
        UpdateBenchmark();
        for (auto *object : m_benchEmitters)
        {
            auto *emitter = object->GetComponent<Geni::ParticleEmitterComponent>();
            for (int step = 0; step < 2 * 60; ++step)
            {
                emitter->Simulate(1.0f / 60.0f);
            }
        }
    }

    void UpdateBenchmark()
    {
        // The particle emitters move with the camera, in a 4 x 4 grid across the view.
        const glm::vec3 cameraPosition = m_camera->GetPosition();
        for (size_t i = 0; i < m_benchEmitters.size(); ++i)
        {
            const float x = (static_cast<float>(i % 4) - 1.5f) * 5.0f;
            const float y = (static_cast<float>(i / 4) - 1.5f) * 2.5f;
            m_benchEmitters[i]->SetPosition(glm::vec3(cameraPosition.x + x, cameraPosition.y + y, 0.0f));
        }

        if (m_bench.empty())
        {
            return;
        }
        // Keep the swarm around the camera so every sprite stays visible.
        const float cameraX = m_camera->GetPosition().x - 10.0f;
        for (auto &bench : m_bench)
        {
            const float t = m_time * 2.0f + bench.phase;
            bench.object->SetPosition(
                glm::vec3(
                    cameraX + bench.origin.x + 0.3f * std::sin(t), bench.origin.y + 0.3f * std::cos(1.3f * t), 0.0f
                )
            );
        }
    }

    Options m_options;
    Geni::Scene *m_scene = nullptr;
    Geni::GameObject *m_player = nullptr;
    Geni::GameObject *m_body = nullptr;
    Geni::GameObject *m_camera = nullptr;
    Geni::CameraComponent *m_cameraComponent = nullptr;
    Geni::CharacterControllerComponent *m_character = nullptr;
    Geni::SpriteComponent *m_sprite = nullptr;
    Geni::SpriteAnimatorComponent *m_animator = nullptr;
    Geni::TilemapComponent *m_tilemap = nullptr;
    std::vector<Geni::GameObject *> m_coins;
    std::vector<Parallax> m_parallax;
    std::vector<BenchSprite> m_bench;
    std::vector<Geni::GameObject *> m_benchEmitters;
    Geni::ParticleEmitterComponent *m_sparkles = nullptr;
    Geni::ParticleEmitterComponent *m_dust = nullptr;
    float m_airTime = 0.0f;
    float m_fallSpeed = 0.0f;
    size_t m_crates = 0;
    int m_collected = 0;
    int m_falls = 0;
    float m_startX = 0.0f;
    float m_time = 0.0f;
    long m_frame = 0;
    long m_finishedFrame = 0;
    double m_totalMs = 0.0;
    long m_measured = 0;
    std::chrono::steady_clock::time_point m_lastTime;
};

bool ParseOptions(int argc, char **argv, Options &options)
{
    for (int i = 1; i < argc; ++i)
    {
        const char *arg = argv[i];
        auto next = [&]() -> const char * { return i + 1 < argc ? argv[++i] : ""; };
        if (std::strcmp(arg, "--hidden") == 0 || std::strcmp(arg, "--no-overlay") == 0)
        {
            options.hidden = true;
        }
        else if (std::strcmp(arg, "--width") == 0)
        {
            options.width = std::atoi(next());
        }
        else if (std::strcmp(arg, "--height") == 0)
        {
            options.height = std::atoi(next());
        }
        else if (std::strcmp(arg, "--frames") == 0)
        {
            options.frames = std::atol(next());
        }
        else if (std::strcmp(arg, "--screenshot") == 0)
        {
            options.screenshotPath = next();
        }
        else if (std::strcmp(arg, "--autopilot") == 0)
        {
            options.autopilot = true;
        }
        else if (std::strcmp(arg, "--benchmark") == 0)
        {
            options.benchmarkSprites = std::atoi(next());
        }
        else if (std::strcmp(arg, "--particles") == 0)
        {
            options.benchmarkParticles = std::atoi(next());
        }
        else if (std::strcmp(arg, "--budget-ms") == 0)
        {
            options.budgetMs = std::atof(next());
        }
        else
        {
            GENI_LOG_ERROR(
                "Unknown argument '%s'. Usage: platformer [--hidden] [--width W] [--height H] [--frames N] "
                "[--screenshot FILE] [--autopilot] [--benchmark N] [--particles N] [--budget-ms MS]",
                arg
            );
            return false;
        }
    }
    return true;
}

} // namespace

int main(int argc, char **argv)
{
    Options options;
    if (!ParseOptions(argc, argv, options))
    {
        return 2;
    }

    Geni::EngineConfig config;
    config.title = "Geni Plains";
    config.width = options.width;
    config.height = options.height;
    config.visible = !options.hidden;
    config.vsync = !options.hidden && !options.Benchmarking();
    // The game UI draws through Dear ImGui; the stats window stays hidden (F1).
    config.debugOverlay = true;
    config.debugOverlayVisible = false;
    // 2D needs no shadows; MSAA keeps rotating crates smooth.
    config.renderer.shadows = false;
    if (options.hidden && !options.Benchmarking())
    {
        // Captures and the autopilot: every frame is one 1/60 s step, on any machine.
        config.deterministicFrameTime = kStep;
    }

    auto *app = new PlatformerApp(options);
    auto &engine = Geni::Engine::GetInstance();
    engine.SetApplication(app);
    if (!engine.Init(config))
    {
        engine.Destroy();
        return 1;
    }
    engine.Run();
    const int collected = app->Collected();
    const size_t coins = app->CoinCount();
    const bool finished = app->Finished();
    const float time = app->Time();
    const int falls = app->Falls();
    const double meanMs = app->MeanFrameMs();
    const Geni::RenderStats stats = engine.GetRenderer().GetRenderStats();
    engine.Destroy();

    if (const auto errors = Geni::rhi::GetValidationErrorCount())
    {
        GENI_LOG_ERROR("%llu Vulkan validation errors", static_cast<unsigned long long>(errors));
        return 3;
    }
    if (options.autopilot)
    {
        std::printf(
            "platformer: %s at %.1f s, %d of %zu coins, %d falls\n", finished ? "finished" : "did not finish", time,
            collected, coins, falls
        );
        if (!finished || static_cast<size_t>(collected) * 4 < coins)
        {
            return 9;
        }
    }
    if (options.Benchmarking())
    {
        std::printf(
            "platformer: %.2f ms/frame (%.1f fps) with %u sprites and particles in %u batches; budget %.2f ms\n",
            meanMs, meanMs > 0.0 ? 1000.0 / meanMs : 0.0, stats.sprites, stats.spriteBatches, options.budgetMs
        );
        if (options.budgetMs > 0.0 && meanMs > options.budgetMs)
        {
            return 4;
        }
    }
    return 0;
}
