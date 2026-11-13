// Particles (docs/design/0019): billboard sprites and additive blending in the renderer,
// and the emitter component. The "gpu particles" suite draws them.

#include "TestEngine.h"
#include "TestImage.h"

#include "core/Engine.h"
#include "render/RenderQueue.h"
#include "render/Renderer.h"
#include "rhi/Device.h"
#include "scene/BuiltinComponents.h"
#include "scene/GameObject.h"
#include "scene/Reflection.h"
#include "scene/Scene.h"
#include "scene/components/CameraComponent.h"
#include "scene/components/ParticleEmitterComponent.h"
#include "script/ScriptSystem.h"

#include <doctest/doctest.h>
#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

namespace
{

Geni::SpriteQuad Billboard(const glm::vec3 &center, float halfSize, const glm::vec4 &color)
{
    Geni::SpriteQuad quad;
    quad.center = center;
    quad.min = glm::vec2(-halfSize);
    quad.max = glm::vec2(halfSize);
    quad.color = color;
    return quad;
}

// An emitter on a fresh object; the scene owns both.
Geni::ParticleEmitterComponent *AddEmitter(Geni::Scene &scene, const char *name = "fx")
{
    Geni::RegisterBuiltinComponents();
    Geni::GameObject *object = scene.CreateObject(name);
    auto *emitter = new Geni::ParticleEmitterComponent();
    object->AddComponent(emitter);
    return emitter;
}

} // namespace

TEST_SUITE("particles")
{
    TEST_CASE("emission follows the rate, fires the burst once and respects the pool size")
    {
        Geni::Scene scene;
        auto *emitter = AddEmitter(scene);
        emitter->SetRate(10.0f);
        emitter->SetBurst(5);
        emitter->SetLifetimeMin(100.0f);
        emitter->SetLifetimeMax(100.0f);
        for (int i = 0; i < 10; ++i)
        {
            emitter->Simulate(0.1f);
        }
        CHECK(emitter->GetParticleCount() == 15);

        emitter->SetMaxParticles(20);
        for (int i = 0; i < 20; ++i)
        {
            emitter->Simulate(0.1f);
        }
        CHECK(emitter->GetParticleCount() == 20);

        // Stopped: nothing new, and a reduced pool drops the excess.
        emitter->SetEmitting(false);
        emitter->SetMaxParticles(12);
        emitter->Simulate(1.0f);
        CHECK(emitter->GetParticleCount() == 12);
        emitter->Emit(100);
        CHECK(emitter->GetParticleCount() == 12);
        emitter->Clear();
        CHECK(emitter->GetParticleCount() == 0);
    }

    TEST_CASE("a duration stops emission; with loop the burst repeats every cycle")
    {
        Geni::Scene scene;
        auto *once = AddEmitter(scene, "once");
        once->SetRate(10.0f);
        once->SetDuration(0.5f);
        once->SetLoop(false);
        once->SetLifetimeMin(100.0f);
        for (int i = 0; i < 20; ++i)
        {
            once->Simulate(0.1f);
        }
        CHECK(once->GetParticleCount() == 5);

        auto *looping = AddEmitter(scene, "loop");
        looping->SetRate(0.0f);
        looping->SetBurst(3);
        looping->SetDuration(1.0f);
        looping->SetLifetimeMin(100.0f);
        for (int i = 0; i < 25; ++i)
        {
            looping->Simulate(0.1f);
        }
        // At the start, after 1 s and after 2 s.
        CHECK(looping->GetParticleCount() == 9);
    }

    TEST_CASE("particles age, fall, slow down, bounce on the floor and die")
    {
        Geni::Scene scene;
        auto *emitter = AddEmitter(scene);
        emitter->SetRate(0.0f);
        emitter->SetSpread(0.0f);
        emitter->SetDirection(glm::vec3(1.0f, 0.0f, 0.0f));
        emitter->SetSpeedMin(2.0f);
        emitter->SetSpeedMax(2.0f);
        emitter->SetLifetimeMin(1.0f);
        emitter->SetLifetimeMax(1.0f);
        emitter->Emit(1);
        REQUIRE(emitter->GetParticleCount() == 1);
        emitter->Simulate(0.25f);
        CHECK(GeniTest::Near(emitter->GetParticles()[0].position, glm::vec3(0.5f, 0.0f, 0.0f)));

        emitter->SetDrag(1.0f);
        emitter->Simulate(0.25f);
        // v = 2 / (1 + 0.25) = 1.6; x = 0.5 + 0.4.
        CHECK(GeniTest::Near(emitter->GetParticles()[0].velocity.x, 1.6f));
        CHECK(GeniTest::Near(emitter->GetParticles()[0].position.x, 0.9f));
        emitter->Simulate(0.6f);
        CHECK(emitter->GetParticleCount() == 0);

        emitter->SetDrag(0.0f);
        emitter->SetDirection(glm::vec3(0.0f, -1.0f, 0.0f));
        emitter->SetCollideFloor(true);
        emitter->SetFloorY(-0.1f);
        emitter->SetBounce(0.5f);
        emitter->Emit(1);
        emitter->Simulate(0.1f);
        const auto &p = emitter->GetParticles()[0];
        CHECK(GeniTest::Near(p.position.y, -0.1f));
        CHECK(GeniTest::Near(p.velocity.y, 1.0f));

        emitter->Clear();
        emitter->SetCollideFloor(false);
        emitter->SetSpeedMin(0.0f);
        emitter->SetSpeedMax(0.0f);
        emitter->SetGravity(glm::vec3(0.0f, -10.0f, 0.0f));
        emitter->Emit(1);
        emitter->Simulate(0.1f);
        CHECK(GeniTest::Near(emitter->GetParticles()[0].velocity.y, -1.0f));
    }

    TEST_CASE("the same seed gives the same particles, and directions stay inside the cone")
    {
        Geni::Scene scene;
        auto *a = AddEmitter(scene, "a");
        auto *b = AddEmitter(scene, "b");
        for (auto *emitter : {a, b})
        {
            emitter->SetSeed(42);
            emitter->SetShape(Geni::EmitterShape::Sphere);
            emitter->SetRadius(2.0f);
            emitter->SetSpread(30.0f);
            emitter->SetRate(100.0f);
            emitter->SetLifetimeMin(100.0f);
            emitter->Simulate(0.5f);
        }
        REQUIRE(a->GetParticleCount() == 50);
        REQUIRE(b->GetParticleCount() == 50);
        const float cosLimit = std::cos(glm::radians(30.0f)) - 1e-4f;
        for (int i = 0; i < 50; ++i)
        {
            const auto &pa = a->GetParticles()[i];
            CHECK(GeniTest::Near(pa.velocity, b->GetParticles()[i].velocity));
            CHECK(glm::length(pa.position) <= 2.0f + 1e-3f);
            const glm::vec3 direction = glm::normalize(pa.velocity);
            CHECK(direction.y >= cosLimit);
        }
        const glm::vec3 first = a->GetParticles()[0].velocity;
        a->Restart();
        a->Simulate(0.5f);
        CHECK(GeniTest::Near(a->GetParticles()[0].velocity, first));
    }

    TEST_CASE("world-space particles stay behind, local ones follow, and both rebase")
    {
        Geni::Scene scene;
        auto *world = AddEmitter(scene, "world");
        auto *local = AddEmitter(scene, "local");
        local->SetWorldSpace(false);
        for (auto *emitter : {world, local})
        {
            emitter->SetRate(0.0f);
            emitter->SetSpeedMin(0.0f);
            emitter->SetSpeedMax(0.0f);
            emitter->SetLifetimeMin(100.0f);
            emitter->GetOwner()->SetPosition(glm::vec3(5.0f, 0.0f, 0.0f));
            emitter->Emit(1);
            emitter->GetOwner()->SetPosition(glm::vec3(10.0f, 0.0f, 0.0f));
            emitter->Update(0.01f);
        }
        CHECK(GeniTest::Near(world->GetQuads()[0].center, glm::vec3(5.0f, 0.0f, 0.0f)));
        CHECK(GeniTest::Near(local->GetQuads()[0].center, glm::vec3(0.0f)));

        world->OnOriginShift(glm::vec3(4.0f, 0.0f, 0.0f));
        local->OnOriginShift(glm::vec3(4.0f, 0.0f, 0.0f));
        CHECK(GeniTest::Near(world->GetParticles()[0].position, glm::vec3(1.0f, 0.0f, 0.0f)));
        CHECK(GeniTest::Near(local->GetParticles()[0].position, glm::vec3(0.0f)));
    }

    TEST_CASE("size, colour and atlas frame change over a particle's life")
    {
        Geni::Scene scene;
        auto *emitter = AddEmitter(scene);
        emitter->SetRate(0.0f);
        emitter->SetLifetimeMin(1.0f);
        emitter->SetLifetimeMax(1.0f);
        emitter->SetStartSize(1.0f);
        emitter->SetEndSize(3.0f);
        emitter->SetStartColor(glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
        emitter->SetEndColor(glm::vec4(0.0f, 0.0f, 0.0f, 0.0f));
        emitter->SetColumns(2);
        emitter->SetRows(2);
        emitter->SetRandomFrame(false);
        emitter->Emit(1);
        emitter->Update(0.5f);
        REQUIRE(emitter->GetQuads().size() == 1);
        const Geni::SpriteQuad &quad = emitter->GetQuads()[0];
        CHECK(GeniTest::Near(quad.max.x - quad.min.x, 2.0f));
        CHECK(GeniTest::Near(quad.color.a, 0.5f));
        // Half-way through four frames: frame 2, the bottom-left cell.
        CHECK(GeniTest::Near(quad.uvTopLeft.x, 0.0f));
        CHECK(GeniTest::Near(quad.uvTopLeft.y, 0.5f));
    }

    TEST_CASE("settings are reflected and round-trip; bad values are clamped")
    {
        Geni::RegisterBuiltinComponents();
        Geni::ParticleEmitterComponent emitter;
        emitter.SetRate(33.0f);
        emitter.SetShape(Geni::EmitterShape::Box);
        emitter.SetBoxSize(glm::vec3(1.0f, 2.0f, 3.0f));
        emitter.SetBlend(Geni::SpriteBlend::Additive);
        emitter.SetGravity(glm::vec3(0.0f, -9.8f, 0.0f));
        emitter.SetEndColor(glm::vec4(0.1f, 0.2f, 0.3f, 0.4f));
        emitter.SetWorldSpace(false);
        emitter.SetSeed(7);

        const auto properties = Geni::TypeRegistry::Get().Serialize("ParticleEmitterComponent", emitter);
        CHECK(properties["blend"] == "additive");
        CHECK(properties["shape"] == "box");
        Geni::ParticleEmitterComponent restored;
        Geni::SerializationContext context;
        REQUIRE(Geni::TypeRegistry::Get().Deserialize("ParticleEmitterComponent", restored, properties, context));
        CHECK(restored.GetRate() == doctest::Approx(33.0f));
        CHECK(restored.GetShape() == Geni::EmitterShape::Box);
        CHECK(GeniTest::Near(restored.GetBoxSize(), glm::vec3(1.0f, 2.0f, 3.0f)));
        CHECK(restored.GetBlend() == Geni::SpriteBlend::Additive);
        CHECK(GeniTest::Near(restored.GetGravity(), glm::vec3(0.0f, -9.8f, 0.0f)));
        CHECK(restored.GetEndColor().w == doctest::Approx(0.4f));
        CHECK_FALSE(restored.GetWorldSpace());
        CHECK(restored.GetSeed() == 7);

        restored.SetRate(-1.0f);
        restored.SetSpread(500.0f);
        restored.SetMaxParticles(-3);
        restored.SetDirection(glm::vec3(0.0f));
        CHECK(restored.GetRate() == doctest::Approx(0.0f));
        CHECK(restored.GetSpread() == doctest::Approx(180.0f));
        CHECK(restored.GetMaxParticles() == 0);
        CHECK(GeniTest::Near(restored.GetDirection(), glm::vec3(0.0f, 1.0f, 0.0f)));
    }

    TEST_CASE("Lua emits, counts and clears")
    {
        auto &engine = Geni::Engine::GetInstance();
        auto *scene = new Geni::Scene();
        engine.SetScene(scene);
        Geni::ScriptConfig config;
        config.hotReload = false;
        REQUIRE(engine.GetScripts().Init(config, &engine.GetFileSystem()));
        auto *emitter = AddEmitter(*scene, "Sparks");
        emitter->SetLifetimeMin(100.0f);

        std::string result;
        std::string error;
        REQUIRE(engine.GetScripts().Run(
            "fx = scene.find('Sparks'):get('ParticleEmitterComponent'); fx.rate = 0; fx:emit(7)"
        ));
        CHECK_MESSAGE(engine.GetScripts().Evaluate("fx:count()", result, &error), error);
        CHECK(result == "7");
        CHECK(emitter->GetRate() == doctest::Approx(0.0f));
        REQUIRE(engine.GetScripts().Run("fx:clear()"));
        CHECK(emitter->GetParticleCount() == 0);

        engine.SetScene(nullptr);
        engine.GetScripts().Shutdown();
    }
}

TEST_SUITE("gpu particles")
{
    TEST_CASE("billboards face the camera, add light, and alpha billboards sort back to front")
    {
        auto &app = GeniTest::RequireEngine();
        auto &engine = Geni::Engine::GetInstance();
        auto &renderer = engine.GetRenderer();
        auto *scene = GeniTest::ResetScene();
        app.sceneToUpdate = scene;
        const Geni::RendererConfig before = renderer.GetConfig();
        renderer.SetTonemapper(Geni::Tonemapper::None);
        renderer.SetExposure(1.0f);
        renderer.SetBackground(false, glm::vec3(0.0f));

        // An orthographic camera on +X looking towards -X: the screen's right is world -Z
        // and its up is +Y. A quad in the XY plane would be seen edge-on.
        auto *camera = scene->CreateObject("camera");
        camera->SetPosition(glm::vec3(10.0f, 0.0f, 0.0f));
        camera->SetRotation(glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f)));
        auto *cameraComponent = new Geni::CameraComponent();
        cameraComponent->SetProjection(Geni::CameraProjection::Orthographic);
        cameraComponent->SetOrthoSize(8.0f);
        camera->AddComponent(cameraComponent);
        scene->SetMainCamera(camera);

        // Additive: red and green over the same spot, above the centre, add up to yellow.
        const std::vector<Geni::SpriteQuad> additive = {
            Billboard(glm::vec3(0.0f, 2.0f, 0.0f), 0.5f, glm::vec4(1.0f, 0.0f, 0.0f, 1.0f)),
            Billboard(glm::vec3(0.0f, 2.0f, 0.0f), 0.5f, glm::vec4(0.0f, 1.0f, 0.0f, 1.0f)),
        };
        // Alpha: the near blue quad is listed first; sorted, the far white one is drawn
        // first and the blue one stays on top.
        const std::vector<Geni::SpriteQuad> alpha = {
            Billboard(glm::vec3(2.0f, -2.0f, 0.0f), 0.5f, glm::vec4(0.0f, 0.0f, 1.0f, 1.0f)),
            Billboard(glm::vec3(-2.0f, -2.0f, 0.0f), 1.0f, glm::vec4(1.0f, 1.0f, 1.0f, 1.0f)),
        };

        Geni::SpriteCommand add;
        add.quads = additive.data();
        add.quadCount = static_cast<uint32_t>(additive.size());
        add.billboard = true;
        add.blend = Geni::SpriteBlend::Additive;
        Geni::SpriteCommand blend;
        blend.quads = alpha.data();
        blend.quadCount = static_cast<uint32_t>(alpha.size());
        blend.billboard = true;

        const auto path = std::filesystem::temp_directory_path() / "geni_test_particles.ppm";
        std::error_code ec;
        std::filesystem::remove(path, ec);
        const bool overlayVisible = engine.GetDebugOverlay().IsVisible();
        engine.GetDebugOverlay().SetVisible(false);
        const auto errorsBefore = Geni::rhi::GetValidationErrorCount();
        engine.GetRenderQueue().Clear();
        engine.GetRenderQueue().SubmitSprites(add);
        engine.GetRenderQueue().SubmitSprites(blend);
        engine.RequestScreenshot(path.string());
        CHECK(engine.Tick());
        engine.GetDebugOverlay().SetVisible(overlayVisible);
        CHECK(Geni::rhi::GetValidationErrorCount() == errorsBefore);
        CHECK(renderer.GetRenderStats().sprites == 4);
        CHECK(renderer.GetRenderStats().spriteBatches == 2);

        GeniTest::Image image;
        REQUIRE(GeniTest::ReadPpm(path, image));
        std::filesystem::remove(path, ec);
        const int cx = image.width / 2;
        const int cy = image.height / 2;
        const int unit = image.height / 8;
        CHECK(GeniTest::NearColor(image.At(cx, cy - 2 * unit), glm::ivec3(255, 255, 0), 2));
        CHECK(GeniTest::NearColor(image.At(cx, cy + 2 * unit), glm::ivec3(0, 0, 255), 2));
        // The white quad's edge, outside the blue one.
        CHECK(GeniTest::NearColor(image.At(cx + unit * 3 / 4, cy + 2 * unit), glm::ivec3(255, 255, 255), 2));
        CHECK(GeniTest::NearColor(image.At(cx, cy), glm::ivec3(0, 0, 0), 2));

        renderer.SetTonemapper(before.tonemapper);
        renderer.SetExposure(before.exposure);
        renderer.SetBackground(before.sky, before.clearColor);
        app.sceneToUpdate = nullptr;
        GeniTest::ResetScene();
    }
}
