#include "TestEngine.h"
#include "core/Engine.h"
#include "graphics/Texture.h"
#include "physics/Collider.h"
#include "physics/RigidBody.h"
#include "render/Material.h"
#include "render/Mesh.h"
#include "scene/Scene.h"
#include "scene/components/CameraComponent.h"
#include "scene/components/LightComponent.h"
#include "scene/components/MeshComponent.h"
#include "scene/components/PhysicsComponent.h"
#include <GL/glew.h>
#include <doctest/doctest.h>
#include <memory>

using GeniTest::Near;

namespace
{

const double kStep = 1.0 / 60.0;

// A 1 m crate dropped from 5 m onto a static floor whose top is at y = 0.
Geni::GameObject *BuildDropScene(Geni::Scene *scene)
{
    const glm::vec3 floorSize(10.0f, 1.0f, 10.0f);
    auto floor = scene->CreateObject("floor");
    floor->SetPosition(glm::vec3(0.0f, -0.5f, 0.0f));
    floor->AddComponent(new Geni::PhysicsComponent(
        std::make_shared<Geni::RigidBody>(
            Geni::BodyType::Static, std::make_shared<Geni::BoxCollider>(floorSize), 0.0f, 0.5f
        )
    ));

    auto crate = scene->CreateObject("crate");
    crate->SetPosition(glm::vec3(0.0f, 5.0f, 0.0f));
    crate->AddComponent(new Geni::PhysicsComponent(
        std::make_shared<Geni::RigidBody>(
            Geni::BodyType::Dynamic, std::make_shared<Geni::BoxCollider>(glm::vec3(1.0f)), 1.0f, 0.5f
        )
    ));
    return crate;
}

// Simulates `seconds` of game time delivered in frames of `frameSeconds`, and returns
// the crate's height afterwards.
float DropCrate(double frameSeconds, double seconds)
{
    auto &engine = Geni::Engine::GetInstance();
    auto &app = GeniTest::RequireEngine();
    auto scene = GeniTest::ResetScene();
    auto crate = BuildDropScene(scene);
    app.sceneToUpdate = scene;
    engine.GetFixedTimestep().Reset();

    const int frames = static_cast<int>(seconds / frameSeconds + 0.5);
    for (int i = 0; i < frames; ++i)
    {
        engine.Simulate(frameSeconds);
    }

    const float height = crate->GetPosition().y;
    engine.GetRenderQueue().Clear();
    GeniTest::ResetScene();
    return height;
}

} // namespace

TEST_SUITE("gpu engine loop")
{
    TEST_CASE("Simulate runs fixed steps at the configured rate and one Update per call")
    {
        auto &app = GeniTest::RequireEngine();
        auto &engine = Geni::Engine::GetInstance();
        GeniTest::ResetScene();
        engine.GetFixedTimestep().Reset();

        const int fixedBefore = app.fixedUpdates;
        const int updatesBefore = app.updates;

        engine.Simulate(kStep * 3.0);
        CHECK(app.fixedUpdates - fixedBefore == 3);
        CHECK(app.updates - updatesBefore == 1);
        CHECK(app.lastFixedDelta == doctest::Approx(1.0f / 60.0f));
        CHECK(engine.GetFrameStats().fixedSteps == 3);

        // A frame shorter than a step still updates, but does not step the simulation.
        engine.Simulate(kStep * 0.25);
        CHECK(app.fixedUpdates - fixedBefore == 3);
        CHECK(app.updates - updatesBefore == 2);
        CHECK(engine.GetFrameStats().fixedSteps == 0);
        CHECK(engine.GetFrameStats().interpolationAlpha == doctest::Approx(0.25));
    }

    TEST_CASE("a dropped crate comes to rest on the floor")
    {
        const float height = DropCrate(kStep, 5.0);
        // Resting with its centre half a metre above the floor.
        CHECK(height == doctest::Approx(0.5f).epsilon(0.05));
    }

    TEST_CASE("physics does not depend on the frame rate")
    {
        // Half a second of free fall, delivered as 30, 15 and 120 frames.
        const float at60 = DropCrate(kStep, 0.5);
        const float at30 = DropCrate(kStep * 2.0, 0.5);
        const float at240 = DropCrate(kStep / 4.0, 0.5);

        CHECK(at60 < 4.0f);
        CHECK(at60 > 3.0f);
        CHECK(at30 == doctest::Approx(at60).epsilon(0.0001));
        CHECK(at240 == doctest::Approx(at60).epsilon(0.0001));
    }

    TEST_CASE("a static body stays where its object was placed")
    {
        auto &app = GeniTest::RequireEngine();
        auto &engine = Geni::Engine::GetInstance();
        auto scene = GeniTest::ResetScene();
        app.sceneToUpdate = scene;

        auto wall = scene->CreateObject("wall");
        wall->SetPosition(glm::vec3(3.0f, 4.0f, 5.0f));
        wall->AddComponent(new Geni::PhysicsComponent(
            std::make_shared<Geni::RigidBody>(
                Geni::BodyType::Static, std::make_shared<Geni::BoxCollider>(glm::vec3(1.0f)), 0.0f, 0.5f
            )
        ));

        for (int i = 0; i < 30; ++i)
        {
            engine.Simulate(kStep);
        }
        CHECK(Near(wall->GetPosition(), glm::vec3(3.0f, 4.0f, 5.0f)));

        engine.GetRenderQueue().Clear();
        GeniTest::ResetScene();
    }
}

TEST_SUITE("gpu rendering")
{
    TEST_CASE("an updated scene is drawn; one that is not updated draws nothing")
    {
        auto &app = GeniTest::RequireEngine();
        auto &engine = Geni::Engine::GetInstance();
        auto scene = GeniTest::ResetScene();

        auto camera = scene->CreateObject("camera");
        camera->SetPosition(glm::vec3(0.0f, 0.0f, 5.0f));
        camera->AddComponent(new Geni::CameraComponent());
        scene->SetMainCamera(camera);

        auto light = scene->CreateObject("light");
        light->SetPosition(glm::vec3(2.0f, 4.0f, 3.0f));
        light->AddComponent(new Geni::LightComponent());

        auto material = std::make_shared<Geni::Material>();
        material->SetShaderProgram(engine.GetGraphicsAPI().GetDefaultShaderProgram());
        material->SetParam("uBaseColorFactor", glm::vec4(1.0f));
        material->SetParam("baseColorTexture", engine.GetTextureManager().GetWhiteTexture());
        REQUIRE(material->GetShaderProgram() != nullptr);

        auto box = scene->CreateObject("box");
        box->AddComponent(new Geni::MeshComponent(material, Geni::Mesh::CreateBox()));

        while (glGetError() != GL_NO_ERROR)
        {
        }

        // Not updated: the engine does not call Scene::Update itself.
        const auto frameBefore = engine.GetFrameStats().frameIndex;
        CHECK(engine.Tick());
        CHECK(engine.GetFrameStats().drawCalls == 0);
        CHECK(engine.GetFrameStats().frameIndex == frameBefore + 1);

        app.sceneToUpdate = scene;
        CHECK(engine.Tick());
        CHECK(engine.GetFrameStats().drawCalls == 1);
        CHECK(engine.GetRenderQueue().GetPendingCount() == 0);
        CHECK(glGetError() == GL_NO_ERROR);

        GeniTest::ResetScene();
    }

    TEST_CASE("textures load from the assets folder and are cached by path")
    {
        GeniTest::RequireEngine();
        auto &textures = Geni::Engine::GetInstance().GetTextureManager();

        auto first = textures.GetOrLoadTexture("textures/checker.png");
        REQUIRE(first != nullptr);
        CHECK(first->GetID() != 0);
        CHECK(textures.GetOrLoadTexture("textures/checker.png") == first);

        CHECK(textures.GetOrLoadTexture("textures/missing.png") == nullptr);
    }

    TEST_CASE("loading a missing material returns null")
    {
        GeniTest::RequireEngine();
        CHECK(Geni::Material::Load("materials/missing.mat") == nullptr);
    }
}
