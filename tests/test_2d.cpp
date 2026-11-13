// 2D (docs/design/0017): sprites, sprite animation, tilemaps with collision, physics in a
// plane and the orthographic camera. The "gpu 2d" suite draws them.

#include "TestEngine.h"
#include "TestImage.h"

#include "assets/AssetManager.h"
#include "core/Engine.h"
#include "graphics/Texture.h"
#include "io/FileSystem.h"
#include "physics/PhysicsManager.h"
#include "physics/RigidBody.h"
#include "render/RenderQueue.h"
#include "render/Renderer.h"
#include "rhi/Device.h"
#include "scene/GameObject.h"
#include "scene/Picking.h"
#include "scene/Reflection.h"
#include "scene/Scene.h"
#include "scene/SceneSerializer.h"
#include "scene/components/CameraComponent.h"
#include "scene/components/PhysicsComponent.h"
#include "scene/components/SpriteComponent.h"
#include "scene/components/TilemapComponent.h"

#include <doctest/doctest.h>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

using GeniTest::Image;
using GeniTest::Near;
using GeniTest::NearColor;
using GeniTest::ReadPpm;

namespace
{

struct PhysicsWorld
{
    PhysicsWorld()
    {
        Geni::Engine::GetInstance().GetPhysicsManager().Init();
    }
    ~PhysicsWorld()
    {
        Geni::Engine::GetInstance().GetPhysicsManager().Shutdown();
    }
    Geni::PhysicsManager &Get()
    {
        return Geni::Engine::GetInstance().GetPhysicsManager();
    }
};

bool Near2(const glm::vec2 &a, const glm::vec2 &b, float epsilon = 1e-5f)
{
    return Near(a.x, b.x, epsilon) && Near(a.y, b.y, epsilon);
}

} // namespace

TEST_SUITE("2d")
{
    TEST_CASE("a sprite's quad follows its size, pivot, atlas frame and flips")
    {
        Geni::Scene scene;
        auto *object = scene.CreateObject("sprite");
        auto *sprite = new Geni::SpriteComponent();
        object->AddComponent(sprite);

        sprite->SetSize(glm::vec2(2.0f, 1.0f));
        sprite->SetPivot(glm::vec2(0.5f, 0.0f));
        sprite->SetColumns(4);
        sprite->SetRows(2);
        sprite->SetFrame(6); // column 2, row 1
        sprite->BuildQuad();
        const Geni::SpriteQuad &quad = sprite->GetQuad();
        CHECK(Near2(quad.min, glm::vec2(-1.0f, 0.0f)));
        CHECK(Near2(quad.max, glm::vec2(1.0f, 1.0f)));
        CHECK(Near2(quad.uvTopLeft, glm::vec2(0.5f, 0.5f)));
        CHECK(Near2(quad.uvBottomRight, glm::vec2(0.75f, 1.0f)));

        sprite->SetFlipX(true);
        sprite->BuildQuad();
        CHECK(Near2(sprite->GetQuad().uvTopLeft, glm::vec2(0.75f, 0.5f)));
        CHECK(Near2(sprite->GetQuad().uvBottomRight, glm::vec2(0.5f, 1.0f)));

        // Frames wrap both ways.
        sprite->SetFrame(9);
        CHECK(sprite->GetFrame() == 1);
        sprite->SetFrame(-1);
        CHECK(sprite->GetFrame() == 7);

        // The tint is sRGB; the quad carries linear colour.
        sprite->SetColor(glm::vec4(0.5f, 1.0f, 0.0f, 0.25f));
        sprite->BuildQuad();
        CHECK(Near(sprite->GetQuad().color.r, 0.214f, 1e-3f));
        CHECK(Near(sprite->GetQuad().color.g, 1.0f));
        CHECK(Near(sprite->GetQuad().color.a, 0.25f));
    }

    TEST_CASE("a sprite shows its atlas region, cut into frames")
    {
        Geni::FileSystem files;
        files.SetAssetsFolder(GENI_TEST_ASSETS_DIR);
        Geni::AssetManager assets;
        assets.Init(&files, nullptr, nullptr);
        const Geni::AssetID atlasID = assets.FindBySource(Geni::AssetType::Atlas, "textures/test.gatlas");
        REQUIRE(atlasID.IsValid());
        auto atlas = assets.LoadAtlas(atlasID);
        REQUIRE(atlas.IsReady());
        // checker.png is 16 x 16 with 1 pixel of padding in a 64 x 64 atlas.
        CHECK(atlas->atlas.width == 64);
        REQUIRE(atlas->atlas.regions.count("checker") == 1);
        CHECK(atlas->atlas.regions.at("checker") == glm::ivec4(1, 1, 16, 16));

        Geni::SpriteComponent sprite;
        sprite.SetAtlasID(atlasID);
        sprite.SetRegion("checker");
        Geni::SerializationContext context;
        context.assets = &assets;
        sprite.ResolveAssets(context);
        CHECK(sprite.HasAtlasRegion());
        sprite.SetColumns(2);
        sprite.SetFrame(1);
        sprite.BuildQuad();
        CHECK(Near2(sprite.GetQuad().uvTopLeft, glm::vec2(9.0f, 1.0f) / 64.0f));
        CHECK(Near2(sprite.GetQuad().uvBottomRight, glm::vec2(17.0f, 17.0f) / 64.0f));

        sprite.SetRegion("missing");
        CHECK_FALSE(sprite.HasAtlasRegion());
        assets.Shutdown();
    }

    TEST_CASE("sprites submit one command each, with world bounds and their layer")
    {
        auto &queue = Geni::Engine::GetInstance().GetRenderQueue();
        queue.Clear();
        Geni::Scene scene;
        auto *a = scene.CreateObject("a");
        a->SetPosition(glm::vec3(3.0f, 4.0f, 0.0f));
        auto *sprite = new Geni::SpriteComponent();
        sprite->SetLayer(5);
        a->AddComponent(sprite);
        auto *hidden = scene.CreateObject("hidden");
        auto *hiddenSprite = new Geni::SpriteComponent();
        hiddenSprite->SetVisible(false);
        hidden->AddComponent(hiddenSprite);
        // A texture ID that has not loaded: nothing is drawn rather than a white box.
        auto *loading = scene.CreateObject("loading");
        auto *loadingSprite = new Geni::SpriteComponent();
        loadingSprite->SetTextureID(Geni::AssetID::FromName("texture:missing.png"));
        loading->AddComponent(loadingSprite);

        scene.Update(0.0f);
        REQUIRE(queue.GetPendingSprites().size() == 1);
        const Geni::SpriteCommand &command = queue.GetPendingSprites()[0];
        CHECK(command.layer == 5);
        CHECK(command.quadCount == 1);
        CHECK(command.texture == nullptr);
        CHECK(Near(command.modelMatrix[3].x, 3.0f));
        CHECK(Near(command.boundsRadius, std::sqrt(0.5f)));
        queue.Clear();
        CHECK(queue.GetPendingSprites().empty());
    }

    TEST_CASE("the sprite animator steps through a clip, loops, and holds the last frame of a one-shot")
    {
        Geni::Scene scene;
        auto *object = scene.CreateObject("hero");
        auto *sprite = new Geni::SpriteComponent();
        sprite->SetColumns(8);
        object->AddComponent(sprite);
        auto *animator = new Geni::SpriteAnimatorComponent();
        object->AddComponent(animator);

        animator->Play(2, 4, 10.0f, true); // frames 2..5, 0.1 s each
        CHECK(sprite->GetFrame() == 2);
        animator->Update(0.25f);
        CHECK(sprite->GetFrame() == 4);
        animator->Update(0.2f); // 0.45 s: wraps to the first frame
        CHECK(sprite->GetFrame() == 2);
        // Asking for the clip that is playing keeps its place.
        animator->Play(2, 4, 10.0f, true);
        animator->Update(0.1f);
        CHECK(sprite->GetFrame() == 3);

        animator->Play(6, 2, 10.0f, false);
        CHECK_FALSE(animator->IsFinished());
        animator->Update(1.0f);
        CHECK(sprite->GetFrame() == 7);
        CHECK(animator->IsFinished());
    }

    TEST_CASE("a tilemap reads and writes its tiles as text, resizes, and maps points to cells")
    {
        Geni::Scene scene;
        auto *object = scene.CreateObject("map");
        object->SetPosition(glm::vec3(10.0f, 5.0f, 0.0f));
        auto *map = new Geni::TilemapComponent();
        object->AddComponent(map);
        map->Resize(4, 3);
        map->SetTileSize(glm::vec2(0.5f, 0.5f));
        map->SetTilesText("0,1,2,3\n-1,4\n7,7,7,7,9");
        CHECK(map->GetTile(1, 0) == 1);
        CHECK(map->GetTile(0, 1) == Geni::TilemapComponent::EmptyTile);
        CHECK(map->GetTile(2, 1) == Geni::TilemapComponent::EmptyTile); // missing cells are empty
        CHECK(map->GetTile(3, 2) == 7);                                 // extra cells are ignored
        CHECK(map->GetTile(-1, 0) == Geni::TilemapComponent::EmptyTile);
        CHECK(map->GetTilesText() == "0,1,2,3\n-1,4,-1,-1\n7,7,7,7");

        map->Resize(2, 4);
        CHECK(map->GetTilesText() == "0,1\n-1,4\n7,7\n-1,-1");

        // The origin is the top-left corner; rows go down.
        CHECK(map->LocalToCell(glm::vec2(0.75f, -0.25f)) == glm::ivec2(1, 0));
        CHECK(map->LocalToCell(glm::vec2(0.25f, -1.25f)) == glm::ivec2(0, 2));
        CHECK(Near2(map->CellCenter(1, 2), glm::vec2(0.75f, -1.25f)));
        CHECK(map->WorldToCell(glm::vec3(10.75f, 4.75f, 0.0f)) == glm::ivec2(1, 0));
        CHECK(Near(map->CellToWorld(1, 2), glm::vec3(10.75f, 3.75f, 0.0f)));

        map->SetSolidTilesText("0-2, 7");
        CHECK(map->IsSolidTile(0));
        CHECK(map->IsSolidTile(2));
        CHECK_FALSE(map->IsSolidTile(3));
        CHECK(map->IsSolidTile(7));
        CHECK(map->IsSolid(0, 2));
        CHECK_FALSE(map->IsSolid(1, 1));
    }

    TEST_CASE("a tilemap is drawn in chunks with one quad per tile")
    {
        auto &queue = Geni::Engine::GetInstance().GetRenderQueue();
        queue.Clear();
        Geni::Scene scene;
        auto *object = scene.CreateObject("map");
        auto *map = new Geni::TilemapComponent();
        object->AddComponent(map);
        map->SetTilesetColumns(4);
        map->SetTilesetRows(4);
        map->Resize(40, 20); // 3 x 2 chunks of 16
        for (int column = 0; column < 40; ++column)
        {
            map->SetTile(column, 19, column % 16);
        }
        map->SetTile(0, 0, 3);

        scene.Update(0.0f);
        // The two top-right chunks are empty and are not submitted.
        CHECK(map->GetChunkCount() == 4);
        CHECK(map->GetQuadCount() == 41);
        CHECK(queue.GetPendingSprites().size() == 4);
        // Tile 5 of a 4 x 4 tileset: column 1, row 1.
        bool found = false;
        for (const auto &command : queue.GetPendingSprites())
        {
            for (uint32_t i = 0; i < command.quadCount; ++i)
            {
                const auto &quad = command.quads[i];
                if (Near2(quad.min, glm::vec2(5.0f, -20.0f)))
                {
                    found = true;
                    CHECK(Near2(quad.uvTopLeft, glm::vec2(0.25f, 0.25f)));
                    CHECK(Near2(quad.uvBottomRight, glm::vec2(0.5f, 0.5f)));
                }
            }
        }
        CHECK(found);
        queue.Clear();
    }

    TEST_CASE("solid tiles collide: runs merge into boxes and a 2D body lands on them without leaving the plane")
    {
        PhysicsWorld world;
        Geni::Scene scene;
        auto *ground = scene.CreateObject("ground");
        ground->SetPosition(glm::vec3(-5.0f, 0.0f, 0.0f));
        auto *map = new Geni::TilemapComponent();
        map->Resize(10, 2);
        map->SetTilesText("-1,-1,-1,-1,-1,-1,-1,-1,-1,-1\n1,1,1,1,0,0,9,1,1,1");
        map->SetSolidTilesText("0-1");
        map->SetCollision(true);
        ground->AddComponent(map);
        // Row 1: [0,6) is solid, 6 is not, [7,10) is solid.
        CHECK(map->GetColliderCount() == 2);

        auto *crate = scene.CreateObject("crate");
        crate->SetPosition(glm::vec3(-3.0f, 3.0f, 0.0f));
        auto *body = new Geni::PhysicsComponent();
        body->SetBodyType(Geni::BodyType::Dynamic);
        body->SetColliderType(Geni::ColliderType::Box);
        body->SetSize(glm::vec3(0.8f));
        body->SetMass(10.0f);
        body->SetPlane2D(true);
        crate->AddComponent(body);
        world.Get().FlushPendingBodies();

        // Pushed into the screen and spun: a 2D body ignores both.
        world.Get().AddBodyImpulse(body->GetRigidBody()->GetBodyId(), glm::vec3(5.0f, 0.0f, 40.0f));
        const float dt = 1.0f / 60.0f;
        for (int i = 0; i < 180; ++i)
        {
            world.Get().Step(dt);
            scene.Update(dt);
        }
        const glm::vec3 position = crate->GetPosition();
        // The solid row's top is at y = -1 (the map's origin is its top-left corner).
        CHECK(Near(position.y, -1.0f + 0.4f, 0.05f));
        CHECK(Near(position.z, 0.0f, 1e-3f));
        const glm::vec3 up = crate->GetRotation() * glm::vec3(0.0f, 0.0f, 1.0f);
        CHECK(Near(up, glm::vec3(0.0f, 0.0f, 1.0f), 1e-3f));

        // Emptying a solid cell splits the run.
        map->SetTile(2, 1, Geni::TilemapComponent::EmptyTile);
        scene.Update(0.0f);
        CHECK(map->GetColliderCount() == 3);
        map->SetCollision(false);
        scene.Update(0.0f);
        CHECK(map->GetColliderCount() == 0);
        Geni::Engine::GetInstance().GetRenderQueue().Clear();
    }

    TEST_CASE("sprites and tilemaps survive a save and load")
    {
        Geni::Scene scene;
        auto *object = scene.CreateObject("thing");
        auto *sprite = new Geni::SpriteComponent();
        sprite->SetTextureID(Geni::AssetID::FromName("texture:hero.png"));
        sprite->SetColumns(4);
        sprite->SetRows(2);
        sprite->SetFrame(5);
        sprite->SetFlipX(true);
        sprite->SetLayer(-2);
        sprite->SetFilter(Geni::SpriteFilter::Nearest);
        object->AddComponent(sprite);
        auto *animator = new Geni::SpriteAnimatorComponent();
        animator->SetFirstFrame(4);
        animator->SetClipFrameCount(3);
        animator->SetLoop(false);
        object->AddComponent(animator);
        auto *map = new Geni::TilemapComponent();
        map->SetTilesetColumns(8);
        map->Resize(3, 2);
        map->SetTilesText("1,2,3\n-1,5,6");
        map->SetSolidTilesText("1-3");
        object->AddComponent(map);

        const auto document = Geni::SceneSerializer::SaveToJson(scene);
        Geni::Scene loaded;
        Geni::SerializationContext context;
        context.scene = &loaded;
        std::string error;
        REQUIRE(Geni::SceneSerializer::Load(loaded, document, context, &error));
        auto *copy = loaded.FindObject("thing");
        REQUIRE(copy != nullptr);
        auto *loadedSprite = copy->GetComponent<Geni::SpriteComponent>();
        REQUIRE(loadedSprite != nullptr);
        CHECK(loadedSprite->GetTextureID() == Geni::AssetID::FromName("texture:hero.png"));
        CHECK(loadedSprite->GetFrame() == 5);
        CHECK(loadedSprite->GetFlipX());
        CHECK(loadedSprite->GetLayer() == -2);
        CHECK(loadedSprite->GetFilter() == Geni::SpriteFilter::Nearest);
        auto *loadedAnimator = copy->GetComponent<Geni::SpriteAnimatorComponent>();
        REQUIRE(loadedAnimator != nullptr);
        CHECK(loadedAnimator->GetFirstFrame() == 4);
        CHECK_FALSE(loadedAnimator->GetLoop());
        auto *loadedMap = copy->GetComponent<Geni::TilemapComponent>();
        REQUIRE(loadedMap != nullptr);
        CHECK(loadedMap->GetTilesText() == "1,2,3\n-1,5,6");
        CHECK(loadedMap->IsSolidTile(2));
        CHECK(loadedMap->GetTilesetColumns() == 8);
    }

    TEST_CASE("the editor picks sprites and tilemaps, preferring the higher layer at equal depth")
    {
        Geni::Scene scene;
        auto *map = scene.CreateObject("map");
        map->SetPosition(glm::vec3(0.0f, 10.0f, 0.0f));
        auto *tilemap = new Geni::TilemapComponent();
        tilemap->Resize(20, 10);
        map->AddComponent(tilemap);
        auto *crate = scene.CreateObject("crate");
        crate->SetPosition(glm::vec3(5.0f, 5.0f, 0.0f));
        auto *sprite = new Geni::SpriteComponent();
        sprite->SetLayer(3);
        crate->AddComponent(sprite);

        Geni::Ray ray;
        ray.origin = glm::vec3(5.2f, 5.1f, 10.0f);
        ray.direction = glm::vec3(0.0f, 0.0f, -1.0f);
        CHECK(Geni::PickObject(scene, ray).object == crate);
        ray.origin = glm::vec3(12.0f, 3.0f, 10.0f); // only the tilemap
        CHECK(Geni::PickObject(scene, ray).object == map);
        ray.origin = glm::vec3(25.0f, 3.0f, 10.0f); // beside both
        CHECK(Geni::PickObject(scene, ray).object == nullptr);
    }

    TEST_CASE("an orthographic camera shows orthoSize units of height at any distance")
    {
        Geni::CameraComponent camera;
        camera.SetProjection(Geni::CameraProjection::Orthographic);
        camera.SetOrthoSize(10.0f);
        const glm::mat4 projection = camera.GetProjectionMatrix(2.0f);
        const glm::vec4 corner = projection * glm::vec4(10.0f, 5.0f, -50.0f, 1.0f);
        CHECK(Near(corner.x / corner.w, 1.0f));
        CHECK(Near(corner.y / corner.w, 1.0f));
        const glm::vec4 nearer = projection * glm::vec4(10.0f, 5.0f, -2.0f, 1.0f);
        CHECK(Near(nearer.x / nearer.w, 1.0f));
    }
}

TEST_SUITE("gpu 2d")
{
    TEST_CASE("sprites batch by texture in layer order, are culled, and draw without validation errors")
    {
        auto &app = GeniTest::RequireEngine();
        auto &engine = Geni::Engine::GetInstance();
        auto *scene = GeniTest::ResetScene();
        app.sceneToUpdate = scene;

        auto *camera = scene->CreateObject("camera");
        camera->SetPosition(glm::vec3(0.0f, 0.0f, 10.0f));
        auto *cameraComponent = new Geni::CameraComponent();
        cameraComponent->SetProjection(Geni::CameraProjection::Orthographic);
        cameraComponent->SetOrthoSize(20.0f);
        camera->AddComponent(cameraComponent);
        scene->SetMainCamera(camera);

        const unsigned char red[4] = {255, 0, 0, 255};
        const unsigned char blue[4] = {0, 0, 255, 255};
        auto redTexture = std::make_shared<Geni::Texture>(1, 1, 4, red);
        auto blueTexture = std::make_shared<Geni::Texture>(1, 1, 4, blue);

        // 600 sprites: layer 0 red, layer 1 blue, layer 2 red, interleaved in creation order.
        for (int i = 0; i < 600; ++i)
        {
            auto *object = scene->CreateObject("s" + std::to_string(i));
            object->SetPosition(
                glm::vec3(static_cast<float>(i % 30) * 0.5f - 7.5f, static_cast<float>(i / 30) * 0.5f - 5.0f, 0.0f)
            );
            auto *sprite = new Geni::SpriteComponent();
            const int layer = i % 3;
            sprite->SetLayer(layer);
            sprite->SetTexture(layer == 1 ? blueTexture : redTexture);
            object->AddComponent(sprite);
        }
        // Far off screen: culled.
        auto *away = scene->CreateObject("away");
        away->SetPosition(glm::vec3(500.0f, 0.0f, 0.0f));
        away->AddComponent(new Geni::SpriteComponent());

        const auto errorsBefore = Geni::rhi::GetValidationErrorCount();
        engine.GetRenderQueue().Clear();
        CHECK(engine.Tick());
        const Geni::RenderStats &stats = engine.GetRenderer().GetRenderStats();
        CHECK(stats.sprites == 600);
        CHECK(stats.spritesCulled == 1);
        CHECK(stats.spriteBatches == 3);
        CHECK(Geni::rhi::GetValidationErrorCount() == errorsBefore);

        app.sceneToUpdate = nullptr;
        GeniTest::ResetScene();
    }

    TEST_CASE("with no tonemapping curve and no sky, sprite colours reach the screen as painted")
    {
        auto &app = GeniTest::RequireEngine();
        auto &engine = Geni::Engine::GetInstance();
        auto &renderer = engine.GetRenderer();
        auto *scene = GeniTest::ResetScene();
        app.sceneToUpdate = scene;
        const Geni::RendererConfig before = renderer.GetConfig();
        renderer.SetTonemapper(Geni::Tonemapper::None);
        renderer.SetExposure(1.0f);
        renderer.SetBackground(false, glm::vec3(20.0f, 40.0f, 60.0f) / 255.0f);

        auto *camera = scene->CreateObject("camera");
        camera->SetPosition(glm::vec3(0.0f, 0.0f, 10.0f));
        auto *cameraComponent = new Geni::CameraComponent();
        cameraComponent->SetProjection(Geni::CameraProjection::Orthographic);
        cameraComponent->SetOrthoSize(4.0f);
        camera->AddComponent(cameraComponent);
        scene->SetMainCamera(camera);

        // A white texture tinted (200, 100, 50); a half-transparent black square over the
        // left half of it on a higher layer.
        const unsigned char white[4] = {255, 255, 255, 255};
        auto texture = std::make_shared<Geni::Texture>(1, 1, 4, white);
        auto *object = scene->CreateObject("square");
        auto *sprite = new Geni::SpriteComponent();
        sprite->SetTexture(texture);
        sprite->SetSize(glm::vec2(2.0f));
        sprite->SetColor(glm::vec4(200.0f, 100.0f, 50.0f, 255.0f) / 255.0f);
        object->AddComponent(sprite);
        auto *shade = scene->CreateObject("shade");
        shade->SetPosition(glm::vec3(-0.5f, 0.0f, 0.0f));
        auto *shadeSprite = new Geni::SpriteComponent();
        shadeSprite->SetSize(glm::vec2(1.0f, 2.0f));
        shadeSprite->SetColor(glm::vec4(0.0f, 0.0f, 0.0f, 0.5f));
        shadeSprite->SetLayer(1);
        shade->AddComponent(shadeSprite);

        const auto path = std::filesystem::temp_directory_path() / "geni_test_2d_colors.ppm";
        std::error_code ec;
        std::filesystem::remove(path, ec);
        // The stats window would cover the middle of a small window.
        const bool overlayVisible = engine.GetDebugOverlay().IsVisible();
        engine.GetDebugOverlay().SetVisible(false);
        engine.RequestScreenshot(path.string());
        CHECK(engine.Tick());
        engine.GetDebugOverlay().SetVisible(overlayVisible);
        Image image;
        REQUIRE(ReadPpm(path, image));
        std::filesystem::remove(path, ec);

        const int cx = image.width / 2;
        const int cy = image.height / 2;
        const int quarter = image.height / 8; // a quarter of the 2-unit square
        CHECK(NearColor(image.At(cx + quarter, cy), glm::ivec3(200, 100, 50), 2));
        CHECK(NearColor(image.At(4, 4), glm::ivec3(20, 40, 60), 2));
        // Blending happens in linear light: half of (200, 100, 50) over black.
        const glm::ivec3 shaded = image.At(cx - quarter, cy);
        CHECK(shaded.x > 130);
        CHECK(shaded.x < 160);

        renderer.SetTonemapper(before.tonemapper);
        renderer.SetExposure(before.exposure);
        renderer.SetBackground(before.sky, before.clearColor);
        app.sceneToUpdate = nullptr;
        GeniTest::ResetScene();
    }
}
