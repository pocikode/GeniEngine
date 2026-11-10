// Sandbox: one scene that uses everything the engine can do today.
//
//   - procedural meshes with a material loaded from a .mat file (shader + texture)
//   - one point light and an orbit camera (left drag: orbit, right drag: pan, scroll: zoom)
//   - rigid-body physics stepped by the fixed-timestep loop
//   - a skinned, animated glTF model
//   - the debug overlay (F1) plus a small ImGui window of its own
//
// Usage: sandbox [--frames N] [--hidden] [--no-overlay] [--screenshot FILE]
//   --frames N         quit after N rendered frames (used by the CI smoke test)
//   --hidden           do not show the window
//   --no-overlay       start without Dear ImGui (used by the screenshot tests)
//   --screenshot FILE  save the last frame as PNG (.png) or PPM (needs --frames)

#include <geni.h>

#include <GLFW/glfw3.h>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <glm/gtc/quaternion.hpp>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

#if defined(GENI_ENABLE_IMGUI)
#include <imgui.h>
#endif

namespace
{

struct Options
{
    long maxFrames = 0; // 0 = run until the window is closed
    bool hidden = false;
    bool overlay = true;
    std::string screenshotPath;
};

class SandboxApp : public Geni::Application
{
  public:
    explicit SandboxApp(const Options &options) : m_options(options)
    {
    }

    bool Init() override
    {
        auto &engine = Geni::Engine::GetInstance();

#if defined(GENI_SAMPLE_ASSETS_DIR)
        // Prefer the source tree when it is there, so asset edits need no rebuild.
        // A packaged build falls back to the assets/ folder next to the executable.
        std::error_code ec;
        if (std::filesystem::exists(GENI_SAMPLE_ASSETS_DIR, ec))
        {
            engine.GetFileSystem().SetAssetsFolder(GENI_SAMPLE_ASSETS_DIR);
        }
#endif

        m_scene = new Geni::Scene();
        engine.SetScene(m_scene);

        auto camera = m_scene->CreateObject("Camera");
        camera->AddComponent(new Geni::CameraComponent());
        auto orbit = new Geni::OrbitCameraComponent();
        orbit->SetTarget(glm::vec3(0.0f, 1.0f, 0.0f));
        orbit->SetDistance(11.0f);
        orbit->SetYaw(35.0f);
        orbit->SetPitch(20.0f);
        camera->AddComponent(orbit);
        m_scene->SetMainCamera(camera);

        auto light = m_scene->CreateObject("Light");
        light->SetPosition(glm::vec3(5.0f, 9.0f, 6.0f));
        auto lightComponent = new Geni::LightComponent();
        lightComponent->SetType(Geni::LightType::Point);
        lightComponent->SetColor(glm::vec3(1.0f, 0.85f, 0.65f));
        lightComponent->SetIntensity(2.0f);
        lightComponent->SetRange(20.0f);
        light->AddComponent(lightComponent);

        // The sun drives cascaded shadows and the sky. A light points along its local -Z.
        auto sun = m_scene->CreateObject("Sun");
        sun->SetRotation(glm::quatLookAt(glm::normalize(glm::vec3(-0.55f, -0.75f, -0.40f)), glm::vec3(0, 1, 0)));
        auto sunComponent = new Geni::LightComponent();
        sunComponent->SetType(Geni::LightType::Directional);
        sunComponent->SetColor(glm::vec3(1.0f, 0.94f, 0.82f));
        sunComponent->SetIntensity(1.8f);
        sun->AddComponent(sunComponent);

        // A ring of coloured point lights: the clustered forward path with more than one
        // light per cluster.
        for (int i = 0; i < 10; ++i)
        {
            const float angle = static_cast<float>(i) * 0.6283185f;
            auto point = m_scene->CreateObject("PointLight");
            point->SetPosition(glm::vec3(std::cos(angle) * 6.0f, 1.6f, std::sin(angle) * 6.0f));
            auto pointLight = new Geni::LightComponent();
            pointLight->SetType(Geni::LightType::Point);
            pointLight->SetColor(
                glm::vec3(
                    0.5f + 0.5f * std::cos(angle), 0.5f + 0.5f * std::cos(angle + 2.09f),
                    0.5f + 0.5f * std::cos(angle + 4.19f)
                )
            );
            pointLight->SetIntensity(1.4f);
            pointLight->SetRange(9.0f);
            point->AddComponent(pointLight);
        }

        m_checkerMaterial = Geni::Material::Load("materials/checker.mat");
        if (!m_checkerMaterial)
        {
            GENI_LOG_ERROR(
                "Could not load materials/checker.mat from %s",
                engine.GetFileSystem().GetAssetsFolder().string().c_str()
            );
            return false;
        }
        m_checkerMaterial->SetParam("uBaseColorFactor", glm::vec4(1.0f));
        m_checkerMaterial->SetParam("uMetallicRoughness", glm::vec4(0.0f, 0.62f, 0.0f, 0.0f));

        // Ground: a wide, thin static box.
        const glm::vec3 groundSize(16.0f, 0.5f, 16.0f);
        auto ground = m_scene->CreateObject("Ground");
        ground->SetPosition(glm::vec3(0.0f, -0.25f, 0.0f));
        ground->AddComponent(new Geni::MeshComponent(m_checkerMaterial, Geni::Mesh::CreateBox(groundSize)));
        ground->AddComponent(new Geni::PhysicsComponent(
            std::make_shared<Geni::RigidBody>(
                Geni::BodyType::Static, std::make_shared<Geni::BoxCollider>(groundSize), 0.0f, 0.8f
            )
        ));

        // Each crate gets the same shader and texture with its own tint.
        m_crateMesh = Geni::Mesh::CreateBox(glm::vec3(1.0f));
        m_crateCollider = std::make_shared<Geni::BoxCollider>(glm::vec3(1.0f));
        for (int i = 0; i < 6; ++i)
        {
            SpawnCrate();
        }

        // Skinned model. LoadGLTF picks up materials/skinned.mat for skinned primitives.
        if (auto bar = Geni::GameObject::LoadGLTF("models/skinned_bar.gltf"))
        {
            bar->SetName("SkinnedBar");
            bar->SetPosition(glm::vec3(-4.0f, 0.0f, 0.0f));
            bar->SetScale(glm::vec3(1.5f));
            if (auto animation = bar->GetComponent<Geni::AnimationComponent>())
            {
                animation->PlayFirst(true);
            }
        }
        else
        {
            GENI_LOG_WARN("models/skinned_bar.gltf did not load; continuing without it");
        }

        GENI_LOG_INFO("Sandbox ready. F1 toggles the debug overlay.");
        return true;
    }

    void FixedUpdate(float fixedDeltaTime) override
    {
        // Gameplay that must not depend on frame rate goes here. The sandbox only
        // keeps simulated time, to show that it advances in equal steps.
        m_simulatedSeconds += fixedDeltaTime;
    }

    void Update(float deltaTime) override
    {
        GENI_PROFILE_SCOPE("SandboxApp::Update");

        auto &engine = Geni::Engine::GetInstance();
        if (engine.GetInputManager().IsKeyPressed(GLFW_KEY_ESCAPE))
        {
            SetNeedsToBeClosed(true);
        }

        const bool spaceDown = engine.GetInputManager().IsKeyPressed(GLFW_KEY_SPACE);
        if (spaceDown && !m_spaceWasDown && !engine.GetDebugOverlay().WantsKeyboard())
        {
            SpawnCrate();
        }
        m_spaceWasDown = spaceDown;

        // Scene::Update is what submits draw calls; the engine does not call it for us.
        m_scene->Update(deltaTime);

        DrawUi();

        ++m_frames;
        if (m_options.maxFrames > 0 && m_frames == m_options.maxFrames && !m_options.screenshotPath.empty())
        {
            engine.RequestScreenshot(m_options.screenshotPath);
        }

        if (m_options.maxFrames > 0 && m_frames >= m_options.maxFrames)
        {
            GENI_LOG_INFO(
                "Reached --frames %ld after %.2f s of simulated time, exiting", m_options.maxFrames, m_simulatedSeconds
            );
            SetNeedsToBeClosed(true);
        }
    }

    void Destroy() override
    {
        // The scene (owned by the engine) still refers to these; the engine releases
        // it right after this call.
        m_crateMesh.reset();
        m_crateCollider.reset();
        m_checkerMaterial.reset();
    }

  private:
    void SpawnCrate()
    {
        const int index = m_crateCount++;

        // Deterministic placement: a loose spiral, each crate higher than the last
        // on-screen stack so new ones always fall.
        const float angle = static_cast<float>(index) * 2.4f;
        const float radius = 0.6f + 0.25f * static_cast<float>(index % 5);
        const glm::vec3 position(
            std::cos(angle) * radius, 3.0f + 1.3f * static_cast<float>(index % 8), std::sin(angle) * radius
        );

        auto material = std::make_shared<Geni::Material>(*m_checkerMaterial);
        const float hue = static_cast<float>(index % 6) / 6.0f;
        material->SetParam(
            "uBaseColorFactor",
            glm::vec4(
                0.55f + 0.45f * std::cos(6.2832f * hue), 0.55f + 0.45f * std::cos(6.2832f * (hue + 0.33f)),
                0.55f + 0.45f * std::cos(6.2832f * (hue + 0.66f)), 1.0f
            )
        );

        auto crate = m_scene->CreateObject("Crate");
        crate->SetPosition(position);
        crate->SetRotation(glm::angleAxis(angle, glm::normalize(glm::vec3(0.3f, 1.0f, 0.2f))));
        crate->AddComponent(new Geni::MeshComponent(material, m_crateMesh));
        crate->AddComponent(new Geni::PhysicsComponent(
            std::make_shared<Geni::RigidBody>(Geni::BodyType::Dynamic, m_crateCollider, 1.0f, 0.6f)
        ));
    }

    void DrawUi()
    {
#if defined(GENI_ENABLE_IMGUI)
        auto &engine = Geni::Engine::GetInstance();
        if (!engine.GetDebugOverlay().IsInitialized() || !engine.GetDebugOverlay().IsVisible())
        {
            return;
        }

        ImGui::SetNextWindowPos(ImVec2(10.0f, 260.0f), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Sandbox", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("Crates: %d", m_crateCount);
            ImGui::Text("Simulated time: %.1f s", m_simulatedSeconds);
            if (ImGui::Button("Spawn crate (Space)"))
            {
                SpawnCrate();
            }
            ImGui::TextDisabled("Left drag: orbit   Right drag: pan   Scroll: zoom");
        }
        ImGui::End();
#endif
    }

  private:
    Options m_options;
    Geni::Scene *m_scene = nullptr; // owned by the engine
    std::shared_ptr<Geni::Material> m_checkerMaterial;
    std::shared_ptr<Geni::Mesh> m_crateMesh;
    std::shared_ptr<Geni::Collider> m_crateCollider;
    int m_crateCount = 0;
    long m_frames = 0;
    double m_simulatedSeconds = 0.0;
    bool m_spaceWasDown = false;
};

} // namespace

int main(int argc, char **argv)
{
    Options options;
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc)
        {
            options.maxFrames = std::strtol(argv[++i], nullptr, 10);
        }
        else if (std::strcmp(argv[i], "--hidden") == 0)
        {
            options.hidden = true;
        }
        else if (std::strcmp(argv[i], "--no-overlay") == 0)
        {
            options.overlay = false;
        }
        else if (std::strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc)
        {
            options.screenshotPath = argv[++i];
        }
        else
        {
            GENI_LOG_ERROR(
                "Unknown argument '%s'. Usage: sandbox [--frames N] [--hidden] [--no-overlay] [--screenshot FILE]",
                argv[i]
            );
            return 2;
        }
    }

    Geni::EngineConfig config;
    config.title = "GeniEngine Sandbox";
    config.width = 1280;
    config.height = 720;
    config.visible = !options.hidden;
    config.debugOverlay = options.overlay;
    if (options.hidden)
    {
        // A hidden window is a test run: do not wait for the display, and advance by
        // exactly one fixed step per frame so every run produces the same result.
        config.vsync = false;
        config.deterministicFrameTime = config.fixedTimestep;
    }

    auto &engine = Geni::Engine::GetInstance();
    engine.SetApplication(new SandboxApp(options));

    if (!engine.Init(config))
    {
        engine.Destroy();
        return 1;
    }

    engine.Run();
    engine.Destroy();

    // Tests run the sandbox with validation on (debug builds); any error fails the run.
    if (const auto errors = Geni::rhi::GetValidationErrorCount())
    {
        GENI_LOG_ERROR("%llu Vulkan validation errors", static_cast<unsigned long long>(errors));
        return 3;
    }
    return 0;
}
