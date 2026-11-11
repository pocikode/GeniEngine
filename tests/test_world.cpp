// M5: world streaming, origin rebasing, mesh LODs and physics on Jolt (docs/design/0007,
// docs/design/0008).
#include "TestEngine.h"
#include "assets/AssetManager.h"
#include "core/Engine.h"
#include "physics/Collider.h"
#include "physics/RigidBody.h"
#include "render/Material.h"
#include "render/Mesh.h"
#include "rhi/Device.h"
#include "scene/GameObject.h"
#include "scene/Scene.h"
#include "scene/components/CameraComponent.h"
#include "scene/components/MeshComponent.h"
#include "scene/components/OrbitCameraComponent.h"
#include "scene/components/PhysicsComponent.h"
#include "world/WorldFormat.h"
#include "world/WorldStreamer.h"
#include <doctest/doctest.h>
#include <glm/gtc/matrix_transform.hpp>

using GeniTest::Near;

TEST_SUITE("world format")
{
    TEST_CASE("a source world is parsed with its defaults and its grid is checked")
    {
        Geni::WorldSource world;
        std::string error;
        REQUIRE(
            Geni::ParseWorldSource(
                nlohmann::json::parse(
                    R"({"format":"geni.world","version":1,"cellSize":64,"origin":[-128,0,-64],"cellsX":4,"cellsZ":2})"
                ),
                world, &error
            )
        );
        CHECK(world.cellSize == 64.0f);
        CHECK(world.origin.x == -128.0);
        CHECK(world.cellsX == 4);
        CHECK(world.cellsZ == 2);
        CHECK(world.persistent == "persistent.gscene");
        CHECK(world.cellsFolder == "cells");
        CHECK(Geni::WorldCellFileName(3, 1) == "cell_3_1.gscene");

        // Written and read back.
        Geni::WorldSource again;
        REQUIRE(Geni::ParseWorldSource(nlohmann::json::parse(Geni::WorldSourceToJson(world).dump()), again));
        CHECK(again.cellSize == world.cellSize);
        CHECK(again.cellsZ == world.cellsZ);

        CHECK_FALSE(
            Geni::ParseWorldSource(
                nlohmann::json::parse(R"({"format":"geni.world","version":1,"cellsX":0})"), world, &error
            )
        );
        CHECK_FALSE(
            Geni::ParseWorldSource(
                nlohmann::json::parse(R"({"format":"geni.world","version":1,"cellSize":0})"), world, &error
            )
        );
        CHECK_FALSE(
            Geni::ParseWorldSource(nlohmann::json::parse(R"({"format":"geni.scene","version":1})"), world, &error)
        );
    }

    TEST_CASE("a cooked world round-trips and finds its cells")
    {
        Geni::WorldDescription world;
        world.name = "test";
        world.cellSize = 100.0f;
        world.origin = glm::dvec3(-200.0, 0.0, -200.0);
        world.cellsX = world.cellsZ = 4;
        for (int i = 0; i < 3; ++i)
        {
            Geni::WorldCellInfo cell;
            cell.x = 3 - i;
            cell.z = i;
            cell.scene = Geni::AssetID::FromName("scene:" + std::to_string(i));
            cell.nodeCount = 10u + static_cast<uint32_t>(i);
            cell.prefabs.push_back(Geni::AssetID::FromName("prefab"));
            world.cells.push_back(cell);
        }
        Geni::WorldDescription read;
        std::string error;
        REQUIRE_MESSAGE(
            Geni::ParseCookedWorld(nlohmann::json::parse(Geni::CookedWorldToJson(world).dump()), read, &error), error
        );
        REQUIRE(read.cells.size() == 3);
        const Geni::WorldCellInfo *cell = read.FindCell(2, 1);
        REQUIRE(cell != nullptr);
        CHECK(cell->nodeCount == 11);
        CHECK(cell->prefabs.size() == 1);
        CHECK(read.FindCell(0, 0) == nullptr);
        CHECK(read.CellCorner(1, 2).x == doctest::Approx(-100.0));
        CHECK(read.CellCorner(1, 2).z == doctest::Approx(0.0));

        // A cell outside the grid, or listed twice, is an error.
        auto json = Geni::CookedWorldToJson(world);
        json["cells"][0]["x"] = 9;
        CHECK_FALSE(Geni::ParseCookedWorld(nlohmann::json::parse(json.dump()), read, &error));
        json = Geni::CookedWorldToJson(world);
        json["cells"].push_back(json["cells"][0]);
        CHECK_FALSE(Geni::ParseCookedWorld(nlohmann::json::parse(json.dump()), read, &error));
    }

    TEST_CASE("per-platform budgets shrink from desktop to mobile to web")
    {
        const auto desktop = Geni::StreamingBudget::ForPlatform(Geni::StreamingPlatform::Desktop);
        const auto mobile = Geni::StreamingBudget::ForPlatform(Geni::StreamingPlatform::Mobile);
        const auto web = Geni::StreamingBudget::ForPlatform(Geni::StreamingPlatform::Web);
        CHECK(mobile.gpuMemoryBytes < desktop.gpuMemoryBytes);
        CHECK(web.gpuMemoryBytes < mobile.gpuMemoryBytes);
        CHECK(mobile.maxLoadedNodes < desktop.maxLoadedNodes);
        CHECK(mobile.loadRadius <= desktop.loadRadius);
        CHECK(web.hlodRadius <= mobile.hlodRadius);
        CHECK(std::string(Geni::StreamingPlatformName(Geni::StreamingBudget::CurrentPlatform())) == "desktop");
    }
}

TEST_SUITE("origin rebasing")
{
    TEST_CASE("shifting the origin moves roots, keeps world positions and tells components")
    {
        Geni::Scene scene;
        auto root = scene.CreateObject("root");
        root->SetPosition(glm::vec3(1500.0f, 2.0f, -700.0f));
        auto child = scene.CreateObject("child", root);
        child->SetPosition(glm::vec3(1.0f, 0.0f, 0.0f));
        auto orbit = new Geni::OrbitCameraComponent();
        orbit->SetTarget(glm::vec3(1500.0f, 0.0f, -700.0f));
        root->AddComponent(orbit);

        const glm::dvec3 before = scene.ToWorld(child->GetWorldPosition());
        scene.ShiftOrigin(glm::dvec3(1024.0, 0.0, -512.0));
        CHECK(scene.GetOrigin().x == 1024.0);
        CHECK(scene.GetOriginShiftCount() == 1);
        // Roots move; children keep their local transform.
        CHECK(Near(root->GetPosition(), glm::vec3(476.0f, 2.0f, -188.0f)));
        CHECK(Near(child->GetPosition(), glm::vec3(1.0f, 0.0f, 0.0f)));
        const glm::dvec3 after = scene.ToWorld(child->GetWorldPosition());
        CHECK(after.x == doctest::Approx(before.x));
        CHECK(after.z == doctest::Approx(before.z));
        CHECK(Near(scene.ToScene(after), child->GetWorldPosition()));

        // A new scene (and a cleared one) starts at zero.
        scene.Clear();
        CHECK(scene.GetOrigin().x == 0.0);
    }
}

TEST_SUITE("gpu world")
{
    TEST_CASE("the renderer draws far objects with a coarser LOD and close ones in full")
    {
        auto &app = GeniTest::RequireEngine();
        auto &engine = Geni::Engine::GetInstance();
        auto scene = GeniTest::ResetScene();
        app.sceneToUpdate = scene;
        const uint64_t errorsBefore = Geni::rhi::GetValidationErrorCount();

        // A mesh with two LODs over the same vertices: the full quad (two triangles) and one
        // triangle with an error of 0.1 m. The layout is the built-in box's (position,
        // colour, uv, normal), which the default shader reads.
        const Geni::VertexLayout layout = Geni::Mesh::CreateBox()->GetVertexLayout();
        const size_t stride = layout.stride / sizeof(float);
        std::vector<float> vertices(4 * stride, 0.0f);
        const float corners[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
        for (size_t v = 0; v < 4; ++v)
        {
            for (const auto &element : layout.elements)
            {
                float *out = vertices.data() + v * stride + element.offset / sizeof(float);
                if (element.index == Geni::VertexElement::PositionIndex)
                {
                    out[0] = corners[v][0];
                    out[1] = corners[v][1];
                }
                else if (element.index == Geni::VertexElement::NormalIndex)
                {
                    out[2] = 1.0f;
                }
            }
        }
        const std::vector<uint32_t> indices = {0, 1, 2, 0, 2, 3, 0, 1, 2};
        auto mesh = std::make_shared<Geni::Mesh>(layout, vertices, indices);
        REQUIRE(mesh->GetLods().size() == 1);
        CHECK(mesh->GetLods()[0].indexCount == 9);
        mesh->SetLods({{0, 6, 0.0f}, {6, 3, 0.1f}, {6, 30, 0.2f}}); // the last is out of range
        REQUIRE(mesh->GetLods().size() == 2);

        auto material = std::make_shared<Geni::Material>();
        material->SetShader(engine.GetRenderer().GetDefaultShader());
        auto camera = scene->CreateObject("camera");
        camera->AddComponent(new Geni::CameraComponent());
        scene->SetMainCamera(camera);
        auto add = [&](float z) {
            auto object = scene->CreateObject("quad");
            object->SetPosition(glm::vec3(0.0f, 0.0f, z));
            object->AddComponent(new Geni::MeshComponent(material, mesh));
        };
        add(-5.0f);   // 0.1 m is many pixels at 5 m: full detail
        add(-500.0f); // and a fraction of a pixel at 500 m

        engine.Simulate(1.0 / 60.0);
        engine.RenderFrame();
        const auto &stats = engine.GetRenderer().GetRenderStats();
        CHECK(stats.lodInstances[0] == 1);
        CHECK(stats.lodInstances[1] == 1);
        CHECK(Geni::rhi::GetValidationErrorCount() == errorsBefore);

        app.sceneToUpdate = nullptr;
        engine.GetRenderQueue().Clear();
        GeniTest::ResetScene();
    }

    TEST_CASE("a dynamic body falls in world coordinates and an origin shift does not move it")
    {
        auto &app = GeniTest::RequireEngine();
        auto &engine = Geni::Engine::GetInstance();
        auto scene = GeniTest::ResetScene();
        app.sceneToUpdate = scene;
        engine.GetFixedTimestep().Reset();
        auto &physics = engine.GetPhysicsManager();
        const uint32_t bodiesBefore = physics.GetBodyCount();

        // Far from the world origin: 5 km out, where the scene origin also is.
        scene->ShiftOrigin(glm::dvec3(5000.0, 0.0, 5000.0));
        auto floor = scene->CreateObject("floor");
        floor->SetPosition(glm::vec3(0.0f, -0.5f, 0.0f));
        floor->SetScale(glm::vec3(20.0f, 1.0f, 20.0f));
        // The collider is a unit box; the object's scale makes it 20 x 1 x 20.
        floor->AddComponent(new Geni::PhysicsComponent(
            std::make_shared<Geni::RigidBody>(
                Geni::BodyType::Static, std::make_shared<Geni::BoxCollider>(glm::vec3(1.0f)), 0.0f, 0.5f
            )
        ));
        auto crate = scene->CreateObject("crate");
        crate->SetPosition(glm::vec3(3.0f, 4.0f, 0.0f));
        auto *body = new Geni::PhysicsComponent(
            std::make_shared<Geni::RigidBody>(
                Geni::BodyType::Dynamic, std::make_shared<Geni::BoxCollider>(glm::vec3(1.0f)), 1.0f, 0.5f
            )
        );
        crate->AddComponent(body);
        // Added in a batch by the next step, not one by one.
        CHECK(physics.GetPendingAddCount() == 2);

        for (int i = 0; i < 30; ++i)
        {
            engine.Simulate(1.0 / 60.0);
        }
        CHECK(physics.GetPendingAddCount() == 0);
        CHECK(physics.GetBodyCount() == bodiesBefore + 2);
        const float fallen = crate->GetPosition().y;
        CHECK(fallen < 3.8f);
        CHECK(fallen > 1.0f);

        // Rebase: the crate's scene position changes, its world position does not.
        const glm::dvec3 worldBefore = scene->ToWorld(crate->GetWorldPosition());
        scene->ShiftOrigin(glm::dvec3(5256.0, 0.0, 5000.0));
        engine.Simulate(1.0 / 60.0);
        const glm::dvec3 worldAfter = scene->ToWorld(crate->GetWorldPosition());
        CHECK(worldAfter.x == doctest::Approx(worldBefore.x).epsilon(1e-6));
        CHECK(crate->GetPosition().x == doctest::Approx(3.0f - 256.0f).epsilon(1e-3));

        // Settles on the floor instead of falling through.
        for (int i = 0; i < 240; ++i)
        {
            engine.Simulate(1.0 / 60.0);
        }
        CHECK(crate->GetPosition().y == doctest::Approx(0.5f).epsilon(0.05));

        // Removing the objects removes their bodies at the next step.
        app.sceneToUpdate = nullptr;
        GeniTest::ResetScene();
        engine.Simulate(1.0 / 60.0);
        CHECK(physics.GetBodyCount() == bodiesBefore);
    }

    TEST_CASE("cells stream in and out around the focus, with HLOD, collision, budgets and rebasing")
    {
        GeniTest::RequireEngine();
        auto &engine = Geni::Engine::GetInstance();
        auto scene = GeniTest::ResetScene();
        auto &assets = engine.GetAssetManager();
        auto &physics = engine.GetPhysicsManager();
        const uint64_t errorsBefore = Geni::rhi::GetValidationErrorCount();

        const Geni::AssetID worldID = assets.FindBySource(Geni::AssetType::World, "worlds/tiny/tiny.gworld");
        REQUIRE(worldID.IsValid());
        Geni::StreamingBudget budget;
        budget.loadRadius = 10.0f; // the focus cell and the edges of its neighbours
        budget.hlodRadius = 40.0f;
        budget.hysteresis = 4.0f;
        budget.rebaseDistance = 50.0f;
        budget.nodesPerFrame = 3; // force several frames per cell
        budget.destroyNodesPerFrame = 2;

        Geni::WorldStreamer streamer;
        std::string error;
        REQUIRE_MESSAGE(streamer.Open(*scene, assets, worldID, budget, &error), error);
        // The persistent scene: the sun and the main camera.
        CHECK(scene->GetMainCamera() != nullptr);
        CHECK(scene->FindObject("Sun") != nullptr);

        // Centre of cell (0, 0): world origin -64, cells of 32 m.
        streamer.SetFocus(glm::dvec3(-48.0, 0.0, -48.0));
        const uint32_t bodiesBefore = physics.GetBodyCount();
        streamer.Update();
        CHECK(streamer.GetCellState(0, 0) == Geni::CellState::Loading);
        streamer.Flush();
        CHECK(streamer.GetCellState(0, 0) == Geni::CellState::Loaded);
        CHECK(streamer.GetCellState(3, 3) == Geni::CellState::Unloaded);
        // Neighbours further than the load radius show their HLOD; the loaded one does not.
        CHECK_FALSE(streamer.IsHlodShown(0, 0));
        CHECK(streamer.IsHlodShown(1, 0));       // 16 m away
        CHECK(streamer.IsHlodShown(1, 1));       // 23 m away
        CHECK_FALSE(streamer.IsHlodShown(2, 0)); // 48 m: beyond the HLOD radius

        Geni::GameObject *root = streamer.GetCellRoot(0, 0);
        REQUIRE(root != nullptr);
        CHECK(root->IsActive());
        CHECK(root->GetChildren().size() == 4);
        // The cell's corner is (-64, 0, -64) in the world.
        const glm::dvec3 corner = scene->ToWorld(root->GetWorldPosition());
        CHECK(corner.x == doctest::Approx(-64.0));
        CHECK(corner.z == doctest::Approx(-64.0));

        // Collision streams with the cell: ground and pillar of each loaded cell.
        engine.GetPhysicsManager().FlushPendingBodies();
        const uint32_t cellBodies = physics.GetBodyCount() - bodiesBefore;
        CHECK(cellBodies == 2 * streamer.GetStats().cellsLoaded);
        CHECK(
            streamer.GetStats().loadedNodes == 6 * (streamer.GetStats().cellsLoaded + streamer.GetStats().cellsLoading)
        );
        CHECK(streamer.GetStats().residentGpuBytes > 0);

        // Fly to cell (3, 3): far enough to rebase; (0, 0) unloads, (3, 3) loads.
        streamer.SetFocus(glm::dvec3(48.0, 0.0, 48.0));
        streamer.Flush();
        CHECK(streamer.GetStats().originShifts >= 1);
        CHECK(scene->GetOrigin().x == doctest::Approx(64.0)); // the nearest cell corner
        CHECK(streamer.GetCellState(0, 0) == Geni::CellState::Unloaded);
        CHECK(streamer.GetCellRoot(0, 0) == nullptr);
        CHECK(streamer.GetCellState(3, 3) == Geni::CellState::Loaded);
        Geni::GameObject *far = streamer.GetCellRoot(3, 3);
        REQUIRE(far != nullptr);
        const glm::dvec3 farCorner = scene->ToWorld(far->GetWorldPosition());
        CHECK(farCorner.x == doctest::Approx(32.0));
        engine.GetPhysicsManager().FlushPendingBodies();
        CHECK(physics.GetBodyCount() - bodiesBefore == 2 * streamer.GetStats().cellsLoaded);
        CHECK(streamer.GetStats().cellsUnloadedTotal >= 1);
        CHECK(streamer.GetStats().lateFrames == 0);

        // A budget that cannot hold even one cell loads nothing and says so.
        Geni::StreamingBudget tight = budget;
        tight.maxLoadedNodes = 4;
        streamer.SetBudget(tight);
        streamer.SetFocus(glm::dvec3(-48.0, 0.0, 48.0));
        streamer.Flush(200);
        CHECK(streamer.GetCellState(0, 3) == Geni::CellState::Unloaded);
        CHECK(streamer.GetStats().budgetLimited);
        CHECK(streamer.GetStats().peakLoadedNodes <= budget.maxLoadedNodes);

        streamer.Close();
        CHECK(scene->FindObject("Cell 3,3") == nullptr);
        engine.Simulate(1.0 / 60.0);
        CHECK(physics.GetBodyCount() == bodiesBefore);
        CHECK(Geni::rhi::GetValidationErrorCount() == errorsBefore);
        engine.GetRenderQueue().Clear();
        GeniTest::ResetScene();
    }

    TEST_CASE("the engine opens a world by path and streams it every frame")
    {
        auto &app = GeniTest::RequireEngine();
        auto &engine = Geni::Engine::GetInstance();
        GeniTest::ResetScene();
        std::string error;
        REQUIRE_MESSAGE(engine.OpenWorld(std::string("worlds/tiny/tiny.gworld"), &error), error);
        Geni::Scene *scene = engine.GetScene();
        app.sceneToUpdate = scene;
        CHECK(engine.GetWorldStreamer().IsOpen());
        // The camera is at the world's centre: the four middle cells are within reach.
        for (int i = 0; i < 400 && engine.GetWorldStreamer().GetCellState(1, 1) != Geni::CellState::Loaded; ++i)
        {
            engine.Tick();
        }
        CHECK(engine.GetWorldStreamer().GetCellState(1, 1) == Geni::CellState::Loaded);
        CHECK(engine.GetFrameStats().worldOpen);
        CHECK(engine.GetFrameStats().cellsLoaded > 0);
        CHECK(engine.GetFrameStats().residentGpuBytes > 0.0);

        CHECK_FALSE(engine.OpenWorld(std::string("worlds/missing.gworld"), &error));
        engine.CloseWorld();
        CHECK_FALSE(engine.GetWorldStreamer().IsOpen());
        app.sceneToUpdate = nullptr;
        engine.GetRenderQueue().Clear();
        GeniTest::ResetScene();
    }
}
