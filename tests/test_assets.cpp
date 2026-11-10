#include "TestEngine.h"
#include "assets/AssetManager.h"
#include "assets/AssetManifest.h"
#include "core/Engine.h"
#include "graphics/Texture.h"
#include "render/Material.h"
#include "render/Mesh.h"
#include "render/Shader.h"
#include "scene/Scene.h"
#include <doctest/doctest.h>
#include <filesystem>

TEST_SUITE("assets")
{
    TEST_CASE("asset IDs are stable, readable and distinct")
    {
        const auto a = Geni::AssetID::FromName("mesh:models/hierarchy.gltf:0:0");
        const auto b = Geni::AssetID::FromName("mesh:models/hierarchy.gltf:0:0");
        const auto c = Geni::AssetID::FromName("mesh:models/hierarchy.gltf:0:1");
        CHECK(a == b);
        CHECK(a != c);
        CHECK(a.IsValid());
        CHECK_FALSE(Geni::AssetID{}.IsValid());
        CHECK(Geni::AssetID::FromHex(a.ToHex()) == a);
        CHECK(a.ToHex().size() == 16);
        CHECK_FALSE(Geni::AssetID::FromHex("not a hash").IsValid());
    }

    TEST_CASE("the cooked manifest lists the test assets")
    {
        Geni::AssetManifest manifest;
        std::string error;
        const bool loaded =
            manifest.LoadFromFile(std::filesystem::path(GENI_TEST_ASSETS_DIR) / "cooked_manifest.json", &error);
        INFO(error);
        REQUIRE(loaded);
        CHECK(manifest.GetEntries().size() >= 5);
        CHECK(manifest.Find(Geni::AssetID::FromName("scene:models/hierarchy.gltf")) != nullptr);
        CHECK(manifest.Find(Geni::AssetID::FromName("texture:textures/checker.png")) != nullptr);
    }

    TEST_CASE("IDs of objects inside a glTF use their unique names")
    {
        // Reordering meshes or materials in the file must not change their IDs, so a
        // uniquely named mesh is keyed by "@name", not by index (tools/asset_cooker/Cooker.h).
        Geni::AssetManifest manifest;
        REQUIRE(manifest.LoadFromFile(std::filesystem::path(GENI_TEST_ASSETS_DIR) / "cooked_manifest.json"));
        const auto *mesh = manifest.FindBySource(Geni::AssetType::Mesh, "models/hierarchy.gltf#mesh0:0");
        REQUIRE(mesh != nullptr);
        CHECK(mesh->id == Geni::AssetID::FromName("mesh:models/hierarchy.gltf:@Triangle:0"));
        const auto *material = manifest.FindBySource(Geni::AssetType::Material, "models/hierarchy.gltf#material0");
        REQUIRE(material != nullptr);
        CHECK(material->id == Geni::AssetID::FromName("material:models/hierarchy.gltf:@Red"));
    }

    TEST_CASE("an image used as colour and as a normal map is cooked once per usage")
    {
        Geni::AssetManifest manifest;
        REQUIRE(manifest.LoadFromFile(std::filesystem::path(GENI_TEST_ASSETS_DIR) / "cooked_manifest.json"));
        const auto *color = manifest.Find(Geni::AssetID::FromName("texture:textures/checker.png"));
        const auto *normal = manifest.Find(Geni::AssetID::FromName("texture:textures/checker.png#normal"));
        REQUIRE(color != nullptr);
        REQUIRE(normal != nullptr);
        CHECK(color->bcPath.find("_bc7.ktx2") != std::string::npos);
        CHECK(normal->bcPath.find("_bc5.ktx2") != std::string::npos);
        CHECK(color->path != normal->path);
    }
}

TEST_SUITE("gpu assets")
{
    TEST_CASE("a cooked texture loads as KTX2 with a mip chain")
    {
        GeniTest::RequireEngine();
        auto &assets = Geni::Engine::GetInstance().GetAssetManager();
        auto texture = assets.LoadTexture(Geni::AssetID::FromName("texture:textures/checker.png"), false);
        REQUIRE(texture.IsReady());
        CHECK(texture->GetWidth() == 16);
        CHECK(texture->GetHeight() == 16);
        CHECK(texture->GetMipLevels() == 5);
        CHECK(texture->GetAssetID() == Geni::AssetID::FromName("texture:textures/checker.png"));

        const auto format = texture->GetFormat();
        // A colour texture: one of the sRGB variants, whichever the device supports.
        CHECK(
            (format == Geni::rhi::Format::BC7Srgb || format == Geni::rhi::Format::ASTC4x4Srgb ||
             format == Geni::rhi::Format::RGBA8Srgb)
        );
    }

    TEST_CASE("a cooked mesh keeps its layout, counts and bounds")
    {
        GeniTest::RequireEngine();
        auto &assets = Geni::Engine::GetInstance().GetAssetManager();
        auto mesh = assets.LoadMesh(assets.FindBySource(Geni::AssetType::Mesh, "models/hierarchy.gltf#mesh0:0"), false);
        REQUIRE(mesh.IsReady());
        CHECK(mesh->GetVertexCount() == 3);
        CHECK(mesh->GetIndexCount() == 3);
        CHECK(mesh->GetVertexLayout().stride > 0);
        CHECK(mesh->GetBounds().radius > 0.0f);
        CHECK_FALSE(mesh->IsSkinned());
    }

    TEST_CASE("a cooked material resolves its shader and texture")
    {
        GeniTest::RequireEngine();
        auto &assets = Geni::Engine::GetInstance().GetAssetManager();
        auto material =
            assets.LoadMaterial(assets.FindBySource(Geni::AssetType::Material, "models/hierarchy.gltf#material0"), false);
        REQUIRE(material.IsReady());
        REQUIRE(material->GetShader() != nullptr);
        CHECK(material->GetShader()->GetName().find("builtin/default") != std::string::npos);
    }

    TEST_CASE("LoadAsync returns a pending handle that becomes ready on the main thread")
    {
        GeniTest::RequireEngine();
        auto &engine = Geni::Engine::GetInstance();
        auto &assets = engine.GetAssetManager();

        const auto before = assets.GetStats().bytesLoaded;
        auto mesh =
            assets.LoadMesh(assets.FindBySource(Geni::AssetType::Mesh, "models/skinned_bar.gltf#mesh0:0:skin0"), true);
        REQUIRE(mesh.IsValid());
        CHECK(mesh.IsPending());
        CHECK_FALSE(mesh.IsReady());

        assets.WaitForAll();

        REQUIRE(mesh.IsReady());
        CHECK(mesh->IsSkinned());
        CHECK(mesh->GetJointNames().size() == 2);
        CHECK(assets.GetStats().bytesLoaded > before);
        CHECK_FALSE(assets.IsLoading());
    }

    TEST_CASE("a material loads its textures asynchronously and is ready once they are")
    {
        GeniTest::RequireEngine();
        auto &assets = Geni::Engine::GetInstance().GetAssetManager();
        const auto id = assets.FindBySource(Geni::AssetType::Material, "models/textured_quad.gltf#material0");
        REQUIRE(id.IsValid());

        auto material = assets.LoadMaterial(id, true);
        REQUIRE(material.IsValid());
        CHECK(material.IsPending());
        assets.WaitForAll();
        REQUIRE(material.IsReady());

        const auto base = material->GetTexture("baseColorTexture");
        const auto normal = material->GetTexture("normalTexture");
        REQUIRE(base != nullptr);
        REQUIRE(normal != nullptr);
        CHECK(base != normal); // the same image, cooked as colour and as a normal map
        const auto normalFormat = normal->GetFormat();
        CHECK(
            (normalFormat == Geni::rhi::Format::BC5Unorm || normalFormat == Geni::rhi::Format::ASTC4x4Unorm ||
             normalFormat == Geni::rhi::Format::RGBA8Unorm)
        );
    }

    TEST_CASE("a synchronous load waits for a load of the same asset already in flight")
    {
        GeniTest::RequireEngine();
        auto &assets = Geni::Engine::GetInstance().GetAssetManager();
        assets.WaitForAll();
        assets.CollectGarbage(); // forget earlier tests' copies so the load really starts
        const auto id = assets.FindBySource(Geni::AssetType::Mesh, "models/textured_quad.gltf#mesh0:0");
        REQUIRE(id.IsValid());

        auto pending = assets.LoadMesh(id, true);
        auto loaded = assets.LoadMesh(id, false);
        CHECK(loaded.IsReady());
        CHECK(loaded.Get() == pending.Get());
    }

    TEST_CASE("CollectGarbage releases assets nothing references")
    {
        GeniTest::RequireEngine();
        auto &assets = Geni::Engine::GetInstance().GetAssetManager();
        assets.WaitForAll();
        const auto id = assets.FindBySource(Geni::AssetType::Mesh, "models/hierarchy.gltf#mesh0:0");
        {
            auto mesh = assets.LoadMesh(id, false);
            REQUIRE(mesh.IsReady());
            assets.CollectGarbage();
            // Still referenced by `mesh`: kept.
            CHECK(assets.LoadMesh(id, false).Get() == mesh.Get());
        }
        const size_t before = assets.GetCachedAssetCount();
        CHECK(assets.CollectGarbage() >= 1);
        CHECK(assets.GetCachedAssetCount() < before);

        // Loading it again works and creates a new object.
        auto again = assets.LoadMesh(id, false);
        CHECK(again.IsReady());
    }

    TEST_CASE("handles keep assets alive and the cache shares them")
    {
        GeniTest::RequireEngine();
        auto &assets = Geni::Engine::GetInstance().GetAssetManager();
        auto first = assets.LoadTexture(Geni::AssetID::FromName("texture:textures/checker.png"), false);
        REQUIRE(first.IsReady());
        auto second = first;
        CHECK(first.Shared().get() == second.Shared().get());
        CHECK(first.Shared().use_count() >= 2); // cache + the temporary Shared()

        auto again = assets.LoadTexture(Geni::AssetID::FromName("texture:textures/checker.png"), false);
        CHECK(again.Shared().get() == first.Shared().get());
    }

    TEST_CASE("a missing asset fails without crashing")
    {
        GeniTest::RequireEngine();
        auto &assets = Geni::Engine::GetInstance().GetAssetManager();
        auto mesh = assets.LoadMesh(Geni::AssetID::FromName("mesh:missing"), false);
        CHECK_FALSE(mesh.IsReady());
        CHECK(mesh.HasFailed());
        CHECK_FALSE(mesh.GetError().empty());
    }
}
