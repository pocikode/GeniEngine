// What the editor needs from the running engine (ROADMAP M4, docs/design/0006): assets
// and shaders reloaded in place, the scene drawn into a rectangle of the backbuffer
// through a camera that is not in the scene, and a scene that is shown without simulating.

#include "TestEngine.h"
#include "assets/AssetManager.h"
#include "assets/CookedFormats.h"
#include "core/Engine.h"
#include "core/Log.h"
#include "graphics/Texture.h"
#include "physics/Collider.h"
#include "physics/RigidBody.h"
#include "render/Material.h"
#include "render/Mesh.h"
#include "render/Shader.h"
#include "rhi/Device.h"
#include "scene/GameObject.h"
#include "scene/Scene.h"
#include "scene/SceneSerializer.h"
#include "scene/components/CameraComponent.h"
#include "scene/components/LightComponent.h"
#include "scene/components/MeshComponent.h"
#include "scene/components/PhysicsComponent.h"
#include <chrono>
#include <cstdint>
#include <doctest/doctest.h>
#include <filesystem>
#include <glm/gtc/matrix_transform.hpp>
#include <memory>
#include <vector>

#include "default_frag.spv.h"
#include "default_vert.spv.h"

namespace
{

struct QuietLog
{
    QuietLog()
    {
        Geni::Log::SetConsoleEnabled(false);
    }
    ~QuietLog()
    {
        Geni::Log::SetConsoleEnabled(true);
    }
};

// Makes a file look edited whatever the file system's time resolution is.
void Touch(const std::filesystem::path &path)
{
    std::filesystem::last_write_time(path, std::filesystem::last_write_time(path) + std::chrono::seconds(2));
}

// The textured quad fixture in a fresh scene, with a camera looking at it.
Geni::MeshComponent *LoadQuad(Geni::Scene *scene)
{
    auto &assets = Geni::Engine::GetInstance().GetAssetManager();
    auto asset = assets.LoadScene(assets.FindBySource(Geni::AssetType::Scene, "models/textured_quad.gltf"), false);
    Geni::SerializationContext context{scene, &assets};
    if (!asset.IsReady() || !Geni::SceneSerializer::Load(*scene, asset->document, context, nullptr, false))
    {
        return nullptr;
    }
    assets.WaitForAll();

    auto camera = scene->CreateObject("camera");
    camera->SetPosition(glm::vec3(0.0f, 0.0f, 5.0f));
    camera->AddComponent(new Geni::CameraComponent());
    scene->SetMainCamera(camera);

    Geni::MeshComponent *found = nullptr;
    scene->ForEachComponent<Geni::MeshComponent>([&found](Geni::MeshComponent &mesh) { found = &mesh; });
    return found;
}

} // namespace

TEST_SUITE("gpu hot reload")
{
    TEST_CASE("a reloaded mesh replaces the one every component draws")
    {
        auto &app = GeniTest::RequireEngine();
        auto &engine = Geni::Engine::GetInstance();
        auto &assets = engine.GetAssetManager();
        auto scene = GeniTest::ResetScene();
        Geni::MeshComponent *component = LoadQuad(scene);
        REQUIRE(component != nullptr);
        REQUIRE(component->GetMesh() != nullptr);
        const auto errorsBefore = Geni::rhi::GetValidationErrorCount();

        // Keep the old object alive so the new one cannot reuse its address.
        const std::shared_ptr<Geni::Mesh> before = component->GetMeshHandle().Shared();
        const Geni::AssetID id = component->GetMeshID();
        REQUIRE(assets.Reload(id));
        Geni::Mesh *after = component->GetMesh();
        REQUIRE(after != nullptr);
        CHECK(after != before.get());
        CHECK(after->GetAssetID() == id);
        CHECK(after->GetVertexCount() == before->GetVertexCount());

        // The new mesh draws.
        app.sceneToUpdate = scene;
        CHECK(engine.Tick());
        CHECK(engine.GetFrameStats().drawCalls == 1);

        CHECK_FALSE(assets.Reload(Geni::AssetID::FromName("no such asset")));
        CHECK(Geni::rhi::GetValidationErrorCount() == errorsBefore);
        GeniTest::ResetScene();
    }

    TEST_CASE("only assets whose file changed are reloaded, and a broken file keeps the old asset")
    {
        GeniTest::RequireEngine();
        auto &engine = Geni::Engine::GetInstance();
        auto &assets = engine.GetAssetManager();
        auto scene = GeniTest::ResetScene();
        Geni::MeshComponent *component = LoadQuad(scene);
        REQUIRE(component != nullptr);
        const Geni::AssetID id = component->GetMeshID();
        const Geni::AssetManifestEntry *entry = assets.GetManifest().Find(id);
        REQUIRE(entry != nullptr);
        const auto file = engine.GetFileSystem().ResolveAssetPath(entry->path);

        CHECK(assets.ReloadChanged().empty()); // nothing on disk changed

        const std::shared_ptr<Geni::Mesh> before = component->GetMeshHandle().Shared();
        Touch(file);
        const std::vector<Geni::AssetID> reloaded = assets.ReloadChanged();
        REQUIRE(reloaded.size() == 1);
        CHECK(reloaded[0] == id);
        CHECK(component->GetMesh() != before.get());
        CHECK(assets.ReloadChanged().empty()); // and it is up to date again

        // A file that no longer decodes (half-written, wrong version): the asset in use stays.
        std::vector<uint8_t> good;
        REQUIRE(Geni::cooked::ReadFile(file, good));
        const std::shared_ptr<Geni::Mesh> current = component->GetMeshHandle().Shared();
        {
            QuietLog quiet;
            REQUIRE(Geni::cooked::WriteFile(file, std::vector<uint8_t>{'n', 'o', 'p', 'e'}));
            CHECK_FALSE(assets.Reload(id));
        }
        CHECK(component->GetMesh() == current.get());
        CHECK(component->GetMeshHandle().IsReady());
        REQUIRE(Geni::cooked::WriteFile(file, good));
        CHECK(assets.Reload(id));
        GeniTest::ResetScene();
    }

    TEST_CASE("a reloaded texture is rebound in the materials that use it")
    {
        auto &app = GeniTest::RequireEngine();
        auto &engine = Geni::Engine::GetInstance();
        auto &assets = engine.GetAssetManager();
        auto scene = GeniTest::ResetScene();
        Geni::MeshComponent *component = LoadQuad(scene);
        REQUIRE(component != nullptr);
        Geni::Material *material = component->GetMaterial();
        REQUIRE(material != nullptr);
        const std::shared_ptr<Geni::Texture> before = material->GetTexture("baseColorTexture");
        REQUIRE(before != nullptr);
        const auto errorsBefore = Geni::rhi::GetValidationErrorCount();

        // Draw once so the material has a bind group that points at the old texture.
        app.sceneToUpdate = scene;
        CHECK(engine.Tick());

        REQUIRE(assets.Reload(before->GetAssetID()));
        const std::shared_ptr<Geni::Texture> after = material->GetTexture("baseColorTexture");
        REQUIRE(after != nullptr);
        CHECK(after != before);
        CHECK(after->GetAssetID() == before->GetAssetID());
        CHECK(after->GetWidth() == before->GetWidth());

        CHECK(engine.Tick());
        CHECK(engine.GetFrameStats().drawCalls == 1);
        CHECK(Geni::rhi::GetValidationErrorCount() == errorsBefore);
        GeniTest::ResetScene();
    }

    TEST_CASE("a shader reloads in place; one that no longer loads keeps working")
    {
        auto &app = GeniTest::RequireEngine();
        auto &engine = Geni::Engine::GetInstance();
        const auto errorsBefore = Geni::rhi::GetValidationErrorCount();

        // The built-in shader's SPIR-V, written where Shader::Load looks.
        const auto folder = std::filesystem::path(GENI_TEST_ASSETS_DIR) / "hot_reload";
        const auto vertexFile = folder / "lit.slang.vert.spv";
        const auto fragmentFile = folder / "lit.slang.frag.spv";
        auto bytes = [](const uint32_t *words, size_t size) {
            const auto *data = reinterpret_cast<const uint8_t *>(words);
            return std::vector<uint8_t>(data, data + size);
        };
        const auto vertexBytes = bytes(geni_spirv_default_vert, sizeof(geni_spirv_default_vert));
        // WriteFile leaves an identical file alone; start from nothing so the times are fresh.
        std::filesystem::remove_all(folder);
        REQUIRE(Geni::cooked::WriteFile(vertexFile, vertexBytes));
        REQUIRE(Geni::cooked::WriteFile(fragmentFile, bytes(geni_spirv_default_frag, sizeof(geni_spirv_default_frag))));

        const std::shared_ptr<Geni::Shader> shader = Geni::Shader::Load("hot_reload/lit.slang", "hot_reload/lit.slang");
        REQUIRE(shader != nullptr);
        auto material = std::make_shared<Geni::Material>();
        material->SetShader(shader);
        material->SetParam("uBaseColorFactor", glm::vec4(1.0f));

        auto scene = GeniTest::ResetScene();
        auto camera = scene->CreateObject("camera");
        camera->SetPosition(glm::vec3(0.0f, 0.0f, 5.0f));
        camera->AddComponent(new Geni::CameraComponent());
        scene->SetMainCamera(camera);
        auto box = scene->CreateObject("box");
        box->AddComponent(new Geni::MeshComponent(material, Geni::Mesh::CreateBox()));
        app.sceneToUpdate = scene;
        CHECK(engine.Tick());
        CHECK(engine.GetFrameStats().drawCalls == 1);

        // Unchanged files: nothing to do.
        const uint64_t firstId = shader->GetId();
        CHECK(Geni::Shader::ReloadChanged() == 0);
        CHECK(shader->GetId() == firstId);

        // "Recompiled": same object, new ID, and the material draws with the new modules.
        Touch(vertexFile);
        CHECK(Geni::Shader::ReloadChanged() == 1);
        const uint64_t secondId = shader->GetId();
        CHECK(secondId != firstId);
        CHECK(material->GetShader() == shader.get());
        CHECK(shader->FindMaterialUniform("uBaseColorFactor") != nullptr);
        CHECK(engine.Tick());
        CHECK(engine.GetFrameStats().drawCalls == 1);

        // A compile that left garbage behind: reported once, and the shader stays usable.
        {
            QuietLog quiet;
            REQUIRE(Geni::cooked::WriteFile(vertexFile, std::vector<uint8_t>{1, 2, 3, 4, 5, 6, 7, 8}));
            Touch(vertexFile);
            CHECK(Geni::Shader::ReloadChanged() == 0);
            CHECK(Geni::Shader::ReloadChanged() == 0);
        }
        CHECK(shader->GetId() == secondId);
        CHECK(engine.Tick());
        CHECK(engine.GetFrameStats().drawCalls == 1);

        // Fixed again.
        REQUIRE(Geni::cooked::WriteFile(vertexFile, vertexBytes));
        Touch(vertexFile);
        CHECK(Geni::Shader::ReloadChanged() == 1);
        CHECK(engine.Tick());
        CHECK(Geni::rhi::GetValidationErrorCount() == errorsBefore);

        GeniTest::ResetScene();
        std::filesystem::remove_all(folder);
    }
}

TEST_SUITE("gpu editor support")
{
    TEST_CASE("the scene is drawn into a rectangle of the backbuffer")
    {
        auto &app = GeniTest::RequireEngine();
        auto &engine = Geni::Engine::GetInstance();
        auto &renderer = engine.GetRenderer();
        auto scene = GeniTest::ResetScene();
        REQUIRE(LoadQuad(scene) != nullptr);
        app.sceneToUpdate = scene;
        const auto errorsBefore = Geni::rhi::GetValidationErrorCount();

        uint32_t width = 0;
        uint32_t height = 0;
        engine.GetRenderDevice()->GetBackbufferSize(width, height);
        Geni::SceneViewport whole = renderer.GetSceneRect();
        CHECK(whole.x == 0);
        CHECK(whole.width == width);
        CHECK(whole.height == height);

        Geni::SceneViewport viewport;
        viewport.x = 40;
        viewport.y = 30;
        viewport.width = 200;
        viewport.height = 100;
        renderer.SetSceneViewport(viewport);
        Geni::SceneViewport rect = renderer.GetSceneRect();
        CHECK(rect.x == 40);
        CHECK(rect.y == 30);
        CHECK(rect.width == 200);
        CHECK(rect.height == 100);
        // Both frame slots get targets of the new size, and a different size again after.
        CHECK(engine.Tick());
        CHECK(engine.Tick());
        CHECK(engine.GetFrameStats().drawCalls == 1);

        // A rectangle that hangs over the edge is clipped to the backbuffer.
        viewport.x = width - 50;
        viewport.width = 500;
        renderer.SetSceneViewport(viewport);
        rect = renderer.GetSceneRect();
        CHECK(rect.x == width - 50);
        CHECK(rect.width == 50);
        CHECK(engine.Tick());
        CHECK(engine.Tick());

        renderer.SetSceneViewport(Geni::SceneViewport());
        CHECK(renderer.GetSceneRect().width == width);
        CHECK(engine.Tick());
        CHECK(Geni::rhi::GetValidationErrorCount() == errorsBefore);
        GeniTest::ResetScene();
    }

    TEST_CASE("a view override replaces the scene camera until it is cleared")
    {
        GeniTest::RequireEngine();
        auto &engine = Geni::Engine::GetInstance();
        auto scene = GeniTest::ResetScene();
        auto camera = scene->CreateObject("camera");
        camera->SetPosition(glm::vec3(0.0f, 0.0f, 5.0f));
        auto *component = new Geni::CameraComponent();
        component->SetFieldOfView(40.0f);
        camera->AddComponent(component);
        scene->SetMainCamera(camera);

        Geni::CameraData fromScene = engine.GetRenderCamera(2.0f);
        CHECK(GeniTest::Near(fromScene.position, glm::vec3(0, 0, 5)));
        CHECK(GeniTest::Near(fromScene.projectionMatrix, component->GetProjectionMatrix(2.0f)));

        Geni::ViewOverride view;
        view.viewMatrix = glm::lookAt(glm::vec3(10, 10, 10), glm::vec3(0), glm::vec3(0, 1, 0));
        view.position = glm::vec3(10, 10, 10);
        view.fieldOfViewDegrees = 75.0f;
        view.nearPlane = 0.5f;
        view.farPlane = 300.0f;
        engine.SetViewOverride(view);
        CHECK(engine.HasViewOverride());
        const Geni::CameraData overridden = engine.GetRenderCamera(2.0f);
        CHECK(GeniTest::Near(overridden.position, glm::vec3(10, 10, 10)));
        CHECK(GeniTest::Near(overridden.viewMatrix, view.viewMatrix));
        CHECK(GeniTest::Near(overridden.projectionMatrix, glm::perspective(glm::radians(75.0f), 2.0f, 0.5f, 300.0f)));
        CHECK(overridden.farPlane == doctest::Approx(300.0f));
        CHECK(engine.Tick()); // and a frame renders through it

        engine.ClearViewOverride();
        CHECK_FALSE(engine.HasViewOverride());
        CHECK(GeniTest::Near(engine.GetRenderCamera(2.0f).position, glm::vec3(0, 0, 5)));
        GeniTest::ResetScene();
    }

    TEST_CASE("with physics paused and the scene in edit mode nothing falls")
    {
        auto &app = GeniTest::RequireEngine();
        auto &engine = Geni::Engine::GetInstance();
        auto scene = GeniTest::ResetScene();
        auto crate = scene->CreateObject("crate");
        crate->SetPosition(glm::vec3(0.0f, 5.0f, 0.0f));
        crate->AddComponent(new Geni::PhysicsComponent(
            std::make_shared<Geni::RigidBody>(
                Geni::BodyType::Dynamic, std::make_shared<Geni::BoxCollider>(glm::vec3(1.0f)), 1.0f, 0.5f
            )
        ));
        engine.GetFixedTimestep().Reset();

        // What the editor does while editing: the fixed loop runs, physics does not.
        engine.SetPhysicsPaused(true);
        const int fixedBefore = app.fixedUpdates;
        for (int i = 0; i < 30; ++i)
        {
            engine.Simulate(1.0 / 60.0);
            scene->Update(1.0f / 60.0f, Geni::SceneUpdateMode::Edit);
        }
        CHECK(app.fixedUpdates == fixedBefore + 30);
        CHECK(crate->GetPosition().y == doctest::Approx(5.0f));

        // Play: the same scene simulates.
        engine.SetPhysicsPaused(false);
        for (int i = 0; i < 30; ++i)
        {
            engine.Simulate(1.0 / 60.0);
            scene->Update(1.0f / 60.0f);
        }
        CHECK(crate->GetPosition().y < 4.5f);
        engine.GetRenderQueue().Clear();
        GeniTest::ResetScene();
    }
}
