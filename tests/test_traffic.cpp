// Traffic (docs/design/0025): the lane graph, its grid builder and conflicts, and the
// traffic system keeping cars apart.

#include "TestEngine.h"

#include "core/Engine.h"
#include "game/TrafficSystem.h"
#include "render/RenderQueue.h"
#include "rhi/Device.h"
#include "scene/GameObject.h"
#include "scene/Scene.h"
#include "scene/components/CameraComponent.h"
#include "world/RoadNetwork.h"

#include <doctest/doctest.h>
#include <glm/geometric.hpp>

#include <chrono>
#include <cstdio>

namespace
{

// The connector of crossing `id` from the entry facing `from` to the exit facing `to`
// (directions as unit vectors), or -1.
int64_t Connector(const Geni::RoadNetwork &network, int32_t id, const glm::dvec3 &from, const glm::dvec3 &to)
{
    for (uint32_t i = 0; i < network.GetLanes().size(); ++i)
    {
        const auto &lane = network.GetLanes()[i];
        if (lane.intersection != id)
        {
            continue;
        }
        if (glm::distance(network.DirectionAt(i, 0.0), from) < 0.05 &&
            glm::distance(network.DirectionAt(i, lane.length), to) < 0.05)
        {
            return i;
        }
    }
    return -1;
}

} // namespace

TEST_SUITE("traffic")
{
    TEST_CASE("a street grid has lanes between crossings and turns inside them, without dead ends")
    {
        Geni::RoadGridDesc desc;
        desc.columns = 3;
        desc.rows = 3;
        const Geni::RoadNetwork network = Geni::RoadNetwork::Grid(desc);
        CHECK(network.GetNodes().size() == 9 * 8);
        size_t streets = 0;
        for (uint32_t i = 0; i < network.GetLanes().size(); ++i)
        {
            const auto &lane = network.GetLanes()[i];
            if (lane.intersection < 0)
            {
                ++streets;
                CHECK(lane.length == doctest::Approx(32.0f - 8.0f));
                // Every street ends at an entry with somewhere to go.
                CHECK_FALSE(network.GetNodes()[lane.to].out.empty());
            }
            // Curves start and end on their nodes.
            CHECK(glm::distance(network.PointAt(i, 0.0), network.GetNodes()[lane.from].position) < 1e-6);
            CHECK(glm::distance(network.PointAt(i, lane.length), network.GetNodes()[lane.to].position) < 1e-6);
        }
        CHECK(streets == 24);

        // The centre crossing (id 4): +X straight crosses +Z straight; the two right turns
        // from opposite sides keep to their own corners; a left turn crosses the opposite
        // straight.
        const glm::dvec3 px(1, 0, 0), nx(-1, 0, 0), pz(0, 0, 1), nz(0, 0, -1);
        const int64_t straightX = Connector(network, 4, px, px);
        const int64_t straightZ = Connector(network, 4, pz, pz);
        const int64_t rightFromX = Connector(network, 4, px, pz); // right of +X is +Z
        const int64_t rightFromNegX = Connector(network, 4, nx, nz);
        const int64_t leftFromX = Connector(network, 4, px, nz);
        const int64_t straightNegX = Connector(network, 4, nx, nx);
        REQUIRE(straightX >= 0);
        REQUIRE(straightZ >= 0);
        REQUIRE(rightFromX >= 0);
        REQUIRE(rightFromNegX >= 0);
        REQUIRE(leftFromX >= 0);
        REQUIRE(straightNegX >= 0);
        CHECK(network.Conflicts(straightX, straightZ));
        CHECK_FALSE(network.Conflicts(rightFromX, rightFromNegX));
        CHECK(network.Conflicts(leftFromX, straightNegX));
        CHECK_FALSE(network.Conflicts(straightX, rightFromX)); // same entry: following, not crossing
        CHECK(Connector(network, 4, px, nx) < 0);              // no U-turns

        std::vector<uint32_t> near;
        network.LanesNear(glm::dvec3(32.0, 0.0, 32.0), 10.0, near);
        CHECK_FALSE(near.empty());
        for (uint32_t lane : near)
        {
            CHECK(network.GetLanes()[lane].intersection == 4);
        }
    }

    TEST_CASE("a road network round-trips through JSON; bad lanes are refused")
    {
        Geni::RoadNetwork network;
        const uint32_t a = network.AddNode(glm::dvec3(0, 0, 0));
        const uint32_t b = network.AddNode(glm::dvec3(10, 0, 0));
        const uint32_t c = network.AddNode(glm::dvec3(10, 0, 10));
        const glm::dvec3 corner(10, 0, 0);
        network.AddLane(a, b, 12.0f);
        network.AddLane(b, c, 6.0f, &corner, 0);
        Geni::RoadNetwork again;
        REQUIRE(again.LoadJson(network.ToJson()));
        REQUIRE(again.GetLanes().size() == 2);
        CHECK(again.GetLanes()[1].curved);
        CHECK(again.GetLanes()[1].intersection == 0);
        CHECK(again.GetLanes()[0].length == doctest::Approx(10.0f));
        CHECK_FALSE(again.LoadJson(
            nlohmann::json::parse(
                R"({"format":"geni.roads","version":1,"nodes":[[0,0,0]],"lanes":[{"from":0,"to":3}]})"
            )
        ));
    }

    TEST_CASE("the metro-sized grid builds quickly")
    {
        Geni::RoadGridDesc desc;
        desc.columns = 125;
        desc.rows = 125;
        const auto start = std::chrono::steady_clock::now();
        const Geni::RoadNetwork network = Geni::RoadNetwork::Grid(desc);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        MESSAGE("125 x 125 grid: " << network.GetLanes().size() << " lanes in " << ms << " ms");
        CHECK(network.GetLanes().size() > 200000);
        CHECK(ms < 5000.0);
    }
}

TEST_SUITE("gpu traffic")
{
    TEST_CASE("cars spawn around the focus, drive, and never run into each other")
    {
        auto &app = GeniTest::RequireEngine();
        auto &engine = Geni::Engine::GetInstance();
        auto *scene = GeniTest::ResetScene();
        app.sceneToUpdate = scene;
        auto *camera = scene->CreateObject("camera");
        camera->SetPosition(glm::vec3(64.0f, 30.0f, 64.0f));
        camera->AddComponent(new Geni::CameraComponent());
        scene->SetMainCamera(camera);

        Geni::RoadGridDesc desc;
        // Dense on purpose: 18 cars on a 4 x 4 grid queue and cross each other's paths.
        desc.columns = 4;
        desc.rows = 4;
        Geni::TrafficConfig config;
        config.models = {"models/hierarchy.gltf"};
        config.count = 18;
        config.radius = 120.0f;
        config.spawnInterval = 0.05f;
        auto &traffic = engine.GetTraffic();
        traffic.Start(Geni::RoadNetwork::Grid(desc), config);
        REQUIRE(traffic.IsRunning());

        const auto errorsBefore = Geni::rhi::GetValidationErrorCount();
        float closest = 1e9f;
        for (int frame = 0; frame < 900; ++frame)
        {
            engine.GetRenderQueue().Clear();
            REQUIRE(engine.Tick());
            const auto cars = traffic.GetCars(*scene);
            for (size_t i = 0; i < cars.size(); ++i)
            {
                for (size_t j = i + 1; j < cars.size(); ++j)
                {
                    closest =
                        std::min(closest, glm::distance(cars[i]->GetWorldPosition(), cars[j]->GetWorldPosition()));
                }
            }
        }
        const Geni::TrafficStats stats = traffic.GetStats();
        MESSAGE("cars " << stats.cars << ", driven " << stats.driven << " m, closest " << closest << " m");
        CHECK(stats.cars >= 10);
        CHECK(stats.driven > 200.0);
        CHECK(closest > 3.0f);
        CHECK(Geni::rhi::GetValidationErrorCount() == errorsBefore);

        traffic.Stop();
        app.sceneToUpdate = nullptr;
        GeniTest::ResetScene();
    }
}
