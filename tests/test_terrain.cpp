// Terrain (docs/design/0022): the height field format, the cooked chunks and their LODs,
// and TerrainComponent's queries and collider.

#include "TestEngine.h"

#include "assets/AssetManager.h"
#include "assets/CookedFormats.h"
#include "core/Engine.h"
#include "io/FileSystem.h"
#include "physics/PhysicsManager.h"
#include "render/RenderQueue.h"
#include "render/Renderer.h"
#include "rhi/Device.h"
#include "scene/BuiltinComponents.h"
#include "scene/GameObject.h"
#include "scene/Scene.h"
#include "scene/components/CameraComponent.h"
#include "scene/components/FoliageComponent.h"
#include "scene/components/TerrainComponent.h"
#include "world/Heightfield.h"

#include <doctest/doctest.h>
#include <glm/geometric.hpp>

#include <cmath>
#include <set>
#include <string>
#include <vector>

namespace
{

struct TerrainAssets
{
    Geni::FileSystem files;
    Geni::AssetManager assets;
    TerrainAssets()
    {
        files.SetAssetsFolder(GENI_TEST_ASSETS_DIR);
        assets.Init(&files, nullptr, nullptr);
    }
    ~TerrainAssets()
    {
        assets.Shutdown();
    }
};

} // namespace

TEST_SUITE("terrain")
{
    TEST_CASE("a height field round-trips and interpolates between samples")
    {
        Geni::HeightfieldData field;
        field.samples = 3;
        field.cellSize = 2.0f;
        field.heights = {0, 0, 0, 0, 4, 0, 0, 0, 0};
        std::vector<uint8_t> bytes;
        Geni::WriteHeightfield(field, bytes);
        Geni::HeightfieldData again;
        REQUIRE(Geni::ReadHeightfield(bytes, again));
        CHECK(again.samples == 3);
        CHECK(again.HeightAt(2.0f, 2.0f) == doctest::Approx(4.0f));
        CHECK(again.HeightAt(1.0f, 2.0f) == doctest::Approx(2.0f));
        CHECK(again.HeightAt(-10.0f, 2.0f) == doctest::Approx(0.0f)); // clamped to the edge
        CHECK(again.Extent() == doctest::Approx(4.0f));
        // On the slope's left side the normal leans towards -X.
        CHECK(again.NormalAt(1.0f, 2.0f).x < 0.0f);
        bytes.pop_back();
        CHECK_FALSE(Geni::ReadHeightfield(bytes, again));
    }

    TEST_CASE("a cooked terrain is a scene of chunks whose LODs keep every border vertex")
    {
        TerrainAssets t;
        const Geni::AssetID sceneID = t.assets.FindBySource(Geni::AssetType::Scene, "terrain/small.gterrain");
        REQUIRE(sceneID.IsValid());
        auto scene = t.assets.LoadScene(sceneID);
        REQUIRE(scene.IsReady());
        const auto &nodes = scene->document.at("nodes");
        REQUIRE(nodes.size() == 1 + 4); // root and 2 x 2 chunks
        CHECK(nodes[0].at("components")[0].at("type") == "TerrainComponent");

        // Chunk (0, 0) and its neighbour along +X share the column x = 32 m.
        const auto meshOf = [&](size_t node) {
            const std::string hex = nodes[node].at("components")[0].at("properties").at("mesh");
            const auto file = t.assets.FindCookedFile(Geni::AssetID::FromHex(hex), Geni::AssetType::Mesh);
            std::vector<uint8_t> bytes;
            Geni::cooked::CookedMeshData mesh;
            REQUIRE(Geni::cooked::ReadFile(file, bytes));
            REQUIRE(Geni::cooked::ReadMesh(bytes, mesh));
            return mesh;
        };
        const Geni::cooked::CookedMeshData mesh = meshOf(1);
        REQUIRE(mesh.lods.size() > 1);
        const size_t stride = mesh.layout.stride / sizeof(float);
        std::set<uint32_t> border;
        for (uint32_t v = 0; v < mesh.vertices.size() / stride; ++v)
        {
            if (std::abs(mesh.vertices[v * stride] - 32.0f) < 1e-3f)
            {
                border.insert(v);
            }
        }
        CHECK(border.size() == 17);
        for (const auto &lod : mesh.lods)
        {
            std::set<uint32_t> used(
                mesh.indices.begin() + lod.firstIndex, mesh.indices.begin() + lod.firstIndex + lod.indexCount
            );
            for (uint32_t v : border)
            {
                CHECK_MESSAGE(used.count(v) == 1, "LOD with " << lod.indexCount << " indices lost a border vertex");
            }
        }
        // The last LOD is much lighter than the first.
        CHECK(mesh.lods.back().indexCount * 3 < mesh.lods.front().indexCount);
    }

    TEST_CASE("the terrain component answers heights and normals, and its collider stops a ray")
    {
        Geni::RegisterBuiltinComponents();
        auto &physics = Geni::Engine::GetInstance().GetPhysicsManager();
        physics.Init();
        {
            TerrainAssets t;
            const Geni::AssetID fieldID =
                t.assets.FindBySource(Geni::AssetType::Heightfield, "terrain/small.gterrain#heights");
            REQUIRE(fieldID.IsValid());
            auto field = t.assets.LoadHeightfield(fieldID);
            REQUIRE(field.IsReady());
            CHECK(field->heights.samples == 33);
            CHECK(field->heights.Extent() == doctest::Approx(64.0f));

            Geni::Scene scene;
            Geni::GameObject *root = scene.CreateObject("Terrain");
            root->SetPosition(glm::vec3(100.0f, 5.0f, 0.0f));
            auto *terrain = new Geni::TerrainComponent();
            root->AddComponent(terrain);
            terrain->SetHeights(field);
            CHECK(terrain->IsReady());
            CHECK(terrain->HasCollider());

            // The ramp: 0 m at x = 0 rising to 8 m at the far edge (plus the central bump).
            float height = 0.0f;
            REQUIRE(terrain->GetHeight(glm::vec3(100.0f + 63.0f, 0.0f, 2.0f), height));
            CHECK(height == doctest::Approx(5.0f + field->heights.HeightAt(63.0f, 2.0f)));
            CHECK(height > 5.0f + 7.0f);
            glm::vec3 normal;
            REQUIRE(terrain->GetNormal(glm::vec3(110.0f, 0.0f, 2.0f), normal));
            CHECK(normal.y > 0.9f);
            CHECK(normal.x < 0.0f); // rising towards +X

            physics.FlushPendingBodies();
            Geni::PhysicsHit hit;
            const glm::dvec3 start(100.0 + 40.0, 100.0, 20.0);
            REQUIRE(physics.CastRay(start, glm::vec3(0.0f, -1.0f, 0.0f), 200.0f, hit));
            float expected = 0.0f;
            terrain->GetHeight(glm::vec3(140.0f, 0.0f, 20.0f), expected);
            CHECK(static_cast<float>(100.0 - hit.distance) == doctest::Approx(expected).epsilon(0.02));
            scene.Clear();
        }
        physics.Shutdown();
    }
}

TEST_SUITE("gpu terrain")
{
    TEST_CASE("foliage draws every part of every instance in range and skips far cells")
    {
        auto &app = GeniTest::RequireEngine();
        auto &engine = Geni::Engine::GetInstance();
        auto &assets = engine.GetAssetManager();
        auto *scene = GeniTest::ResetScene();
        app.sceneToUpdate = scene;

        // Any cooked mesh and material of the test assets will do as a plant.
        assets.FindBySource(Geni::AssetType::Scene, "models/lod_sphere.gltf"); // reads the manifest
        Geni::AssetID meshID;
        Geni::AssetID materialID;
        for (const auto &entry : assets.GetManifest().GetEntries())
        {
            if (!meshID.IsValid() && entry.type == Geni::AssetType::Mesh &&
                entry.source.find("lod_sphere") != std::string::npos)
            {
                meshID = entry.id;
            }
            if (!materialID.IsValid() && entry.type == Geni::AssetType::Material &&
                entry.source.find("lod_sphere") != std::string::npos)
            {
                materialID = entry.id;
            }
        }
        REQUIRE(meshID.IsValid());
        REQUIRE(materialID.IsValid());

        auto *camera = scene->CreateObject("camera");
        camera->SetPosition(glm::vec3(0.0f, 2.0f, 10.0f));
        camera->AddComponent(new Geni::CameraComponent());
        scene->SetMainCamera(camera);

        auto *foliage = new Geni::FoliageComponent();
        scene->CreateObject("Plants")->AddComponent(foliage);
        foliage->AddPart(assets.LoadMesh(meshID), assets.LoadMaterial(materialID), glm::mat4(1.0f));
        foliage->AddPart(
            assets.LoadMesh(meshID), assets.LoadMaterial(materialID),
            glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 2.0f, 0.0f))
        );
        foliage->SetDrawDistance(100.0f);
        foliage->SetInstances({
            {glm::vec3(-3.0f, 0.0f, 0.0f), 0.0f, 1.0f},
            {glm::vec3(0.0f, 0.0f, 0.0f), 1.0f, 1.0f},
            {glm::vec3(3.0f, 0.0f, 0.0f), 2.0f, 1.5f},
            {glm::vec3(900.0f, 0.0f, 0.0f), 0.0f, 1.0f}, // far beyond the draw distance
        });
        assets.WaitForAll();

        const auto errorsBefore = Geni::rhi::GetValidationErrorCount();
        engine.GetRenderQueue().Clear();
        CHECK(engine.Tick());
        CHECK(foliage->GetDrawnInstances() == 3);
        const Geni::RenderStats &stats = engine.GetRenderer().GetRenderStats();
        CHECK(stats.submitted >= 6); // two parts each
        CHECK(Geni::rhi::GetValidationErrorCount() == errorsBefore);

        app.sceneToUpdate = nullptr;
        GeniTest::ResetScene();
    }
}
