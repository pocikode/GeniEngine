// The reference game's first scene and the M2 performance benchmark (ROADMAP M2):
// a static city block of 10,000 objects and 100 lights, drawn with cascaded shadows,
// clustered forward lighting, PBR materials, a sky and MSAA, then tonemapped.
//
// It is also the performance check: with --budget-ms the program exits with code 4 when
// the mean frame time is over budget.
//
// Usage: city [--frames N] [--warmup N] [--budget-ms MS] [--hidden] [--screenshot FILE]
//   --frames N       quit after N rendered frames (the CI benchmark measures the last N)
//   --warmup N       frames before the measurement starts (default 20)
//   --budget-ms MS   exit with code 4 when the mean frame time exceeds MS
//   --hidden         do not show the window
//   --width/--height  window size (default 1280x720)
//   --screenshot F   save the last frame as PNG (.png) or PPM

#include <geni.h>

#include <GLFW/glfw3.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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
    bool hidden = false;
    int width = 1280;
    int height = 720;
    std::string screenshotPath;
};

constexpr int kBlocksPerSide = 100; // 100 x 100 = 10,000 buildings
constexpr int kMaterials = 8;
constexpr int kLightsPerSide = 10; // 100 point lights

class CityApp : public Geni::Application
{
  public:
    explicit CityApp(const Options &options) : m_options(options)
    {
    }

    bool Init() override
    {
        auto &engine = Geni::Engine::GetInstance();

        m_scene = new Geni::Scene();
        engine.SetScene(m_scene);

        // A camera looking across the block at street level: left drag orbits, scroll
        // zooms. Without input it does not move, so the benchmark is repeatable.
        auto camera = m_scene->CreateObject("Camera");
        camera->AddComponent(new Geni::CameraComponent());
        auto orbit = new Geni::OrbitCameraComponent();
        orbit->SetTarget(glm::vec3(0.0f, 10.0f, 0.0f));
        orbit->SetDistance(135.0f);
        orbit->SetYaw(35.0f);
        orbit->SetPitch(13.0f);
        camera->AddComponent(orbit);
        m_scene->SetMainCamera(camera);

        auto sun = m_scene->CreateObject("Sun");
        sun->SetRotation(glm::quatLookAt(glm::normalize(glm::vec3(-0.5f, -0.78f, -0.36f)), glm::vec3(0, 1, 0)));
        auto sunLight = new Geni::LightComponent();
        sunLight->SetType(Geni::LightType::Directional);
        sunLight->SetColor(glm::vec3(1.0f, 0.95f, 0.85f));
        sunLight->SetIntensity(2.2f);
        sun->AddComponent(sunLight);

        // One shared unit cube: every building, and the ground, is an instance of it.
        m_cube = Geni::Mesh::CreateBox(glm::vec3(1.0f));

        // A muted, plausible city palette: concrete, brick, stone, glass.
        static const glm::vec3 palette[] = {
            {0.56f, 0.56f, 0.58f}, {0.42f, 0.40f, 0.38f}, {0.65f, 0.61f, 0.55f}, {0.34f, 0.32f, 0.31f},
            {0.52f, 0.36f, 0.30f}, {0.30f, 0.37f, 0.44f}, {0.68f, 0.66f, 0.62f}, {0.45f, 0.29f, 0.25f},
        };
        for (int i = 0; i < kMaterials; ++i)
        {
            auto material = std::make_shared<Geni::Material>();
            material->SetShader(engine.GetRenderer().GetDefaultShader());
            material->SetParam("uBaseColorFactor", glm::vec4(palette[i % 8], 1.0f));
            const bool glass = i == 5;
            material->SetParam(
                "uMetallicRoughness",
                glm::vec4(glass ? 0.55f : 0.02f, glass ? 0.18f : 0.62f + 0.1f * (i % 3), 0.0f, 0.0f)
            );
            m_materials.push_back(material);
        }

        auto groundMaterial = std::make_shared<Geni::Material>();
        groundMaterial->SetShader(engine.GetRenderer().GetDefaultShader());
        groundMaterial->SetParam("uBaseColorFactor", glm::vec4(0.20f, 0.22f, 0.24f, 1.0f));
        groundMaterial->SetParam("uMetallicRoughness", glm::vec4(0.0f, 0.92f, 0.0f, 0.0f));
        m_groundMaterial = groundMaterial;

        auto ground = m_scene->CreateObject("Ground");
        ground->SetPosition(glm::vec3(0.0f, -0.5f, 0.0f));
        ground->SetScale(glm::vec3(360.0f, 1.0f, 360.0f));
        ground->AddComponent(new Geni::MeshComponent(groundMaterial, m_cube));

        // A cheap hash gives deterministic, non-uniform heights.
        for (int z = 0; z < kBlocksPerSide; ++z)
        {
            for (int x = 0; x < kBlocksPerSide; ++x)
            {
                const int index = z * kBlocksPerSide + x;
                const float hash =
                    std::fabs(std::fmod(std::sin(static_cast<float>(index) * 12.9898f) * 43758.5453f, 1.0f));
                const float width = 2.4f - 1.2f * static_cast<float>(index % 3) * 0.15f;
                const float height = 2.0f + 30.0f * hash;

                auto building = m_scene->CreateObject("Block");
                building->SetPosition(
                    glm::vec3(
                        (static_cast<float>(x) - (kBlocksPerSide - 1) * 0.5f) * 2.6f, height * 0.5f,
                        (static_cast<float>(z) - (kBlocksPerSide - 1) * 0.5f) * 2.6f
                    )
                );
                building->SetScale(glm::vec3(width, height, width));
                building->AddComponent(
                    new Geni::MeshComponent(m_materials[static_cast<size_t>(index) % m_materials.size()], m_cube)
                );
            }
        }

        for (int z = 0; z < kLightsPerSide; ++z)
        {
            for (int x = 0; x < kLightsPerSide; ++x)
            {
                const int index = z * kLightsPerSide + x;
                auto lightObject = m_scene->CreateObject("StreetLight");
                lightObject->SetPosition(
                    glm::vec3(
                        (static_cast<float>(x) - (kLightsPerSide - 1) * 0.5f) * 26.0f, 6.0f,
                        (static_cast<float>(z) - (kLightsPerSide - 1) * 0.5f) * 26.0f
                    )
                );
                auto light = new Geni::LightComponent();
                light->SetType(Geni::LightType::Point);
                const float hue = static_cast<float>(index % kLightsPerSide) / static_cast<float>(kLightsPerSide);
                light->SetColor(
                    glm::vec3(0.9f, 0.6f + 0.4f * std::cos(6.2832f * hue), 0.4f + 0.6f * std::sin(6.2832f * hue))
                );
                light->SetIntensity(5.0f);
                light->SetRange(34.0f);
                lightObject->AddComponent(light);
            }
        }

        GENI_LOG_INFO(
            "City block: %d buildings from %d meshes and %d materials, %d point lights, one sun",
            kBlocksPerSide * kBlocksPerSide, 1, kMaterials, kLightsPerSide * kLightsPerSide
        );
        return true;
    }

    void Update(float deltaTime) override
    {
        const auto now = std::chrono::steady_clock::now();
        const double frameMs = std::chrono::duration<double, std::milli>(now - m_lastTime).count();
        m_lastTime = now;

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
        m_materials.clear();
        m_groundMaterial.reset();
        m_cube.reset();
    }

    double MeanFrameMs() const
    {
        return m_measured > 0 ? m_totalMs / static_cast<double>(m_measured) : 0.0;
    }

  private:
    Options m_options;
    Geni::Scene *m_scene = nullptr; // owned by the engine
    std::shared_ptr<Geni::Mesh> m_cube;
    std::vector<std::shared_ptr<Geni::Material>> m_materials;
    std::shared_ptr<Geni::Material> m_groundMaterial;
    long m_frame = 0;
    long m_measured = 0;
    double m_totalMs = 0.0;
    std::chrono::steady_clock::time_point m_lastTime = std::chrono::steady_clock::now();
};

} // namespace

int main(int argc, char **argv)
{
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
                "Unknown argument '%s'. Usage: city [--frames N] [--warmup N] [--budget-ms MS] [--hidden] "
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
            "city: %.2f ms/frame (%.1f fps) over %ld measured frames; %d objects, %d lights; budget %.2f ms\n", meanMs,
            fps, std::max(0L, options.frames - options.warmup), kBlocksPerSide * kBlocksPerSide,
            kLightsPerSide * kLightsPerSide, options.budgetMs
        );
        if (options.budgetMs > 0.0 && meanMs > options.budgetMs)
        {
            GENI_LOG_ERROR("city block over budget: %.2f ms > %.2f ms", meanMs, options.budgetMs);
            return 4;
        }
    }
    return 0;
}