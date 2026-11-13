// Particles (docs/design/0019): billboard sprites and additive blending in the renderer,
// and the emitter component. The "gpu particles" suite draws them.

#include "TestEngine.h"
#include "TestImage.h"

#include "core/Engine.h"
#include "render/RenderQueue.h"
#include "render/Renderer.h"
#include "rhi/Device.h"
#include "scene/GameObject.h"
#include "scene/Scene.h"
#include "scene/components/CameraComponent.h"

#include <doctest/doctest.h>
#include <glm/gtc/quaternion.hpp>

#include <filesystem>
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

} // namespace

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
