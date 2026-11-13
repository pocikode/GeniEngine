// Weather (docs/design/0020): the lighting changes, transitions, precipitation around the
// camera, and the renderer's one-frame weather.

#include "TestEngine.h"

#include "core/Engine.h"
#include "render/RenderQueue.h"
#include "render/Renderer.h"
#include "render/Weather.h"
#include "rhi/Device.h"
#include "scene/BuiltinComponents.h"
#include "scene/GameObject.h"
#include "scene/Reflection.h"
#include "scene/Scene.h"
#include "scene/components/CameraComponent.h"
#include "scene/components/WeatherComponent.h"
#include "script/ScriptSystem.h"

#include <doctest/doctest.h>
#include <glm/geometric.hpp>

#include <cmath>
#include <string>

namespace
{

float Saturation(const glm::vec3 &c)
{
    const float high = std::max({c.r, c.g, c.b});
    const float low = std::min({c.r, c.g, c.b});
    return high > 0.0f ? (high - low) / high : 0.0f;
}

Geni::WeatherComponent *AddWeather(Geni::Scene &scene)
{
    Geni::RegisterBuiltinComponents();
    auto *weather = new Geni::WeatherComponent();
    scene.CreateObject("Weather")->AddComponent(weather);
    return weather;
}

} // namespace

TEST_SUITE("weather")
{
    TEST_CASE("clear weather changes nothing; clouds grey the sky and dim the sun; fog thickens")
    {
        const Geni::Atmosphere day;
        Geni::Atmosphere clear = day;
        CHECK(Geni::ApplyWeather(Geni::WeatherState{}, clear) == doctest::Approx(1.0f));
        CHECK(GeniTest::Near(clear.skyHorizon, day.skyHorizon));
        CHECK(GeniTest::Near(clear.fogColor, day.fogColor));
        CHECK(clear.fogDensity == doctest::Approx(day.fogDensity));
        CHECK(clear.darkness == doctest::Approx(0.0f));

        Geni::WeatherState storm;
        storm.cloudCover = 1.0f;
        Geni::Atmosphere overcast = day;
        CHECK(Geni::ApplyWeather(storm, overcast) == doctest::Approx(0.2f));
        CHECK(Saturation(overcast.skyHorizon) < 0.5f * Saturation(day.skyHorizon));
        CHECK(Saturation(overcast.skyZenith) < 0.5f * Saturation(day.skyZenith));
        CHECK(overcast.ambientIntensity > day.ambientIntensity);
        CHECK(overcast.darkness == doctest::Approx(0.5f));

        Geni::WeatherState foggy;
        foggy.fog = 1.0f;
        Geni::Atmosphere fog = day;
        Geni::ApplyWeather(foggy, fog);
        CHECK(fog.fogDensity == doctest::Approx(day.fogDensity * 12.0f));
    }

    TEST_CASE("the weather eases towards its target, and rain stops before snow starts")
    {
        Geni::Scene scene;
        auto *weather = AddWeather(scene);
        weather->SetTransitionSeconds(10.0f);
        weather->SetPrecipitation(0.0f);
        weather->Simulate(0.1f, glm::vec3(0.0f)); // starts at its target
        CHECK(weather->GetCurrent().precipitation == doctest::Approx(0.0f));

        weather->SetCloudCover(1.0f);
        weather->SetPrecipitation(1.0f);
        weather->Simulate(5.0f, glm::vec3(0.0f));
        CHECK(weather->GetCurrent().cloudCover == doctest::Approx(0.5f));
        CHECK(weather->GetCurrent().precipitation == doctest::Approx(0.5f));
        weather->Simulate(20.0f, glm::vec3(0.0f));
        CHECK(weather->GetCurrent().precipitation == doctest::Approx(1.0f));

        weather->SetSnow(true);
        weather->Simulate(5.0f, glm::vec3(0.0f));
        CHECK_FALSE(weather->GetCurrent().snow);
        CHECK(weather->GetCurrent().precipitation == doctest::Approx(0.5f));
        weather->Simulate(5.0f, glm::vec3(0.0f));
        CHECK(weather->GetCurrent().snow);
        weather->Simulate(10.0f, glm::vec3(0.0f));
        CHECK(weather->GetCurrent().precipitation == doctest::Approx(1.0f));

        weather->SetFog(0.25f);
        weather->Snap();
        CHECK(weather->GetCurrent().fog == doctest::Approx(0.25f));
    }

    TEST_CASE("drops fill a box around the view and wrap into it when the view moves")
    {
        Geni::Scene scene;
        auto *weather = AddWeather(scene);
        weather->SetMaxDrops(2000);
        weather->SetRadius(10.0f);
        weather->SetHeight(8.0f);
        weather->SetPrecipitation(0.5f);
        weather->SetWind(glm::vec3(3.0f, 0.0f, 0.0f));

        auto inBox = [&](const glm::vec3 &view) {
            for (const auto &quad : weather->GetQuads())
            {
                const glm::vec3 d = quad.center - view;
                if (std::abs(d.x) > 10.0f + 1e-3f || std::abs(d.z) > 10.0f + 1e-3f || std::abs(d.y) > 4.0f + 1e-3f)
                {
                    return false;
                }
            }
            return true;
        };

        glm::vec3 view(100.0f, 5.0f, -50.0f);
        for (int i = 0; i < 60; ++i)
        {
            weather->Simulate(1.0f / 60.0f, view);
        }
        CHECK(weather->GetQuads().size() == 1000);
        CHECK(inBox(view));

        // A fast move: every drop is still around the camera, none were lost.
        view += glm::vec3(37.0f, 2.0f, 15.0f);
        weather->Simulate(1.0f / 60.0f, view);
        CHECK(weather->GetQuads().size() == 1000);
        CHECK(inBox(view));

        // Rebasing moves the drops with the world.
        const glm::vec3 before = weather->GetQuads().front().center;
        weather->OnOriginShift(glm::vec3(10.0f, 0.0f, 0.0f));
        weather->Simulate(0.0f, view - glm::vec3(10.0f, 0.0f, 0.0f));
        CHECK(GeniTest::Near(weather->GetQuads().front().center, before - glm::vec3(10.0f, 0.0f, 0.0f), 1e-3f));
    }

    TEST_CASE("the targets are reflected and round-trip; Lua sets them and snaps")
    {
        Geni::RegisterBuiltinComponents();
        Geni::WeatherComponent weather;
        weather.SetCloudCover(0.7f);
        weather.SetSnow(true);
        weather.SetWind(glm::vec3(1.0f, 0.0f, 2.0f));
        weather.SetTransitionSeconds(3.0f);
        const auto properties = Geni::TypeRegistry::Get().Serialize("WeatherComponent", weather);
        Geni::WeatherComponent restored;
        Geni::SerializationContext context;
        REQUIRE(Geni::TypeRegistry::Get().Deserialize("WeatherComponent", restored, properties, context));
        CHECK(restored.GetCloudCover() == doctest::Approx(0.7f));
        CHECK(restored.GetSnow());
        CHECK(GeniTest::Near(restored.GetWind(), glm::vec3(1.0f, 0.0f, 2.0f)));
        CHECK(restored.GetTransitionSeconds() == doctest::Approx(3.0f));
        restored.SetCloudCover(4.0f);
        restored.SetPrecipitation(-1.0f);
        CHECK(restored.GetCloudCover() == doctest::Approx(1.0f));
        CHECK(restored.GetPrecipitation() == doctest::Approx(0.0f));

        auto &engine = Geni::Engine::GetInstance();
        auto *scene = new Geni::Scene();
        engine.SetScene(scene);
        Geni::ScriptConfig config;
        config.hotReload = false;
        REQUIRE(engine.GetScripts().Init(config, &engine.GetFileSystem()));
        auto *live = AddWeather(*scene);
        REQUIRE(engine.GetScripts().Run(
            "local w = scene.find('Weather'):get('WeatherComponent'); w.fog = 0.6; w.precipitation = 1; w:snap()"
        ));
        CHECK(live->GetCurrent().fog == doctest::Approx(0.6f));
        CHECK(live->GetCurrent().precipitation == doctest::Approx(1.0f));
        engine.SetScene(nullptr);
        engine.GetScripts().Shutdown();
    }
}

TEST_SUITE("gpu weather")
{
    TEST_CASE("the renderer applies the weather for one frame and draws the drops")
    {
        auto &app = GeniTest::RequireEngine();
        auto &engine = Geni::Engine::GetInstance();
        auto &renderer = engine.GetRenderer();
        auto *scene = GeniTest::ResetScene();
        app.sceneToUpdate = scene;

        auto *camera = scene->CreateObject("camera");
        camera->SetPosition(glm::vec3(0.0f, 2.0f, 0.0f));
        camera->AddComponent(new Geni::CameraComponent());
        scene->SetMainCamera(camera);
        auto *weather = AddWeather(*scene);
        weather->SetMaxDrops(3000);
        weather->SetCloudCover(1.0f);
        weather->SetPrecipitation(1.0f);

        const auto errorsBefore = Geni::rhi::GetValidationErrorCount();
        engine.GetRenderQueue().Clear();
        CHECK(engine.Tick());
        CHECK(renderer.GetFrameWeather().cloudCover == doctest::Approx(1.0f));
        // The drops are one command that is never culled, so every quad is written.
        CHECK(renderer.GetRenderStats().sprites == 3000);
        const Geni::Atmosphere stormy = renderer.GetFrameAtmosphere();
        CHECK(stormy.darkness == doctest::Approx(0.5f));

        // Without the component the next frame is clear again.
        weather->GetOwner()->MarkForDestroy();
        CHECK(engine.Tick());
        CHECK(engine.Tick());
        CHECK(renderer.GetFrameWeather().cloudCover == doctest::Approx(0.0f));
        CHECK(renderer.GetFrameAtmosphere().darkness == doctest::Approx(0.0f));
        CHECK(Geni::rhi::GetValidationErrorCount() == errorsBefore);

        app.sceneToUpdate = nullptr;
        GeniTest::ResetScene();
    }
}
