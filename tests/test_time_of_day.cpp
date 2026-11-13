#include "TestEngine.h"
#include "core/Engine.h"
#include "render/Renderer.h"
#include "render/TimeOfDay.h"
#include "scene/BuiltinComponents.h"
#include "scene/GameObject.h"
#include "scene/Reflection.h"
#include "scene/Scene.h"
#include "scene/components/LightComponent.h"
#include "scene/components/TimeOfDayComponent.h"
#include <doctest/doctest.h>
#include <glm/geometric.hpp>
#include <cmath>

TEST_SUITE("time of day")
{
    TEST_CASE("hours wrap into 0..24")
    {
        CHECK(Geni::WrapHours(0.0f) == doctest::Approx(0.0f));
        CHECK(Geni::WrapHours(25.5f) == doctest::Approx(1.5f));
        CHECK(Geni::WrapHours(-1.0f) == doctest::Approx(23.0f));
        CHECK(Geni::WrapHours(48.0f) == doctest::Approx(0.0f));
        CHECK(Geni::WrapHours(NAN) == doctest::Approx(0.0f));
        CHECK(Geni::WrapHours(-1e-7f) < 24.0f);
    }

    TEST_CASE("the sun rises in the east, peaks at noon and sets in the west")
    {
        Geni::TimeOfDaySettings settings;
        settings.maxElevation = 60.0f;

        const Geni::TimeOfDayState noon = Geni::EvaluateTimeOfDay(12.0f, settings);
        CHECK_FALSE(noon.moon);
        CHECK(noon.sunElevation == doctest::Approx(60.0f).epsilon(0.001));
        CHECK(noon.lightDirection.y < -0.8f);
        CHECK(noon.lightDirection.z > 0.0f); // the sun is towards -Z, so light travels to +Z
        CHECK(noon.lightIntensity == doctest::Approx(settings.sunIntensity));

        const Geni::TimeOfDayState sunrise = Geni::EvaluateTimeOfDay(6.0f, settings);
        CHECK(sunrise.sunElevation == doctest::Approx(0.0f).epsilon(0.001));
        CHECK(sunrise.lightDirection.x < -0.99f); // from the east

        const Geni::TimeOfDayState sunset = Geni::EvaluateTimeOfDay(18.0f, settings);
        CHECK(sunset.sunElevation == doctest::Approx(0.0f).epsilon(0.001));
        CHECK(sunset.lightDirection.x > 0.99f); // from the west

        // pathYaw turns the path about +Y: 90 degrees takes sunrise from +X to -Z.
        settings.pathYaw = 90.0f;
        const Geni::TimeOfDayState turned = Geni::EvaluateTimeOfDay(6.0f, settings);
        CHECK(turned.lightDirection.z > 0.99f);
    }

    TEST_CASE("midday matches the renderer's default sky")
    {
        const Geni::TimeOfDayState noon = Geni::EvaluateTimeOfDay(12.0f);
        const Geni::RendererConfig config;
        CHECK(GeniTest::Near(noon.atmosphere.skyZenith, config.skyZenith));
        CHECK(GeniTest::Near(noon.atmosphere.skyHorizon, config.skyHorizon));
        CHECK(GeniTest::Near(noon.atmosphere.fogColor, config.fogColor));
        CHECK(noon.atmosphere.ambientIntensity == doctest::Approx(config.ambientIntensity));
        CHECK(noon.atmosphere.fogDensity == doctest::Approx(config.fogDensity));
        CHECK(GeniTest::Near(noon.lightColor, glm::vec3(1.0f, 0.95f, 0.85f)));
    }

    TEST_CASE("the moon lights the night from above, dim and blue")
    {
        const Geni::TimeOfDayState midnight = Geni::EvaluateTimeOfDay(0.0f);
        CHECK(midnight.moon);
        CHECK(midnight.sunElevation < -50.0f);
        CHECK(midnight.lightDirection.y < -0.8f);
        CHECK(midnight.lightIntensity == doctest::Approx(Geni::TimeOfDaySettings{}.moonIntensity));
        CHECK(midnight.lightColor.b > midnight.lightColor.r);
        // The night sky is much darker than the day sky.
        CHECK(midnight.atmosphere.skyHorizon.r < 0.05f);
    }

    TEST_CASE("light and sky change smoothly through the whole day")
    {
        // One-minute steps: no jump in radiance or sky colour, including where the
        // light switches between the sun and the moon.
        Geni::TimeOfDayState previous = Geni::EvaluateTimeOfDay(0.0f);
        int switches = 0;
        for (int minute = 1; minute <= 24 * 60; ++minute)
        {
            const Geni::TimeOfDayState state = Geni::EvaluateTimeOfDay(minute / 60.0f);
            const glm::vec3 radiance = state.lightColor * state.lightIntensity;
            const glm::vec3 previousRadiance = previous.lightColor * previous.lightIntensity;
            CHECK_MESSAGE(glm::length(radiance - previousRadiance) < 0.1f, "minute " << minute);
            CHECK_MESSAGE(glm::length(state.atmosphere.skyHorizon - previous.atmosphere.skyHorizon) < 0.03f, minute);
            CHECK_MESSAGE(glm::length(state.atmosphere.fogColor - previous.atmosphere.fogColor) < 0.03f, minute);
            if (state.moon != previous.moon)
            {
                ++switches;
                // Both are dark at the switch, so the light's direction may flip.
                CHECK(state.lightIntensity < 0.01f);
                CHECK(previous.lightIntensity < 0.01f);
            }
            previous = state;
        }
        CHECK(switches == 2);
    }

    TEST_CASE("the component runs the clock and drives its light")
    {
        Geni::RegisterBuiltinComponents();
        Geni::Scene scene;
        Geni::GameObject *root = scene.CreateObject("World");
        root->SetRotation(glm::angleAxis(glm::radians(30.0f), glm::vec3(0.0f, 1.0f, 0.0f)));
        Geni::GameObject *sun = scene.CreateObject("Sun", root);
        auto *light = new Geni::LightComponent();
        light->SetType(Geni::LightType::Directional);
        sun->AddComponent(light);
        auto *clock = new Geni::TimeOfDayComponent();
        clock->SetTime(11.0f);
        clock->SetDayLengthMinutes(24.0f); // one game hour per real minute
        sun->AddComponent(clock);

        // Editing applies the hour without running the clock.
        scene.Update(1.0f, Geni::SceneUpdateMode::Edit);
        CHECK(clock->GetTime() == doctest::Approx(11.0f));

        for (int i = 0; i < 60; ++i)
        {
            scene.Update(1.0f);
        }
        CHECK(clock->GetTime() == doctest::Approx(12.0f).epsilon(0.001));

        // The light faces along the evaluated direction even under a rotated parent.
        const Geni::TimeOfDayState expected = Geni::EvaluateTimeOfDay(clock->GetTime(), clock->GetSettings());
        CHECK(GeniTest::Near(light->GetDirection(), expected.lightDirection, 1e-3f));
        CHECK(light->GetIntensity() == doctest::Approx(expected.lightIntensity));
        CHECK(GeniTest::Near(light->GetColor(), expected.lightColor));

        // The renderer gets the sky for the next frame.
        Geni::Renderer &renderer = Geni::Engine::GetInstance().GetRenderer();
        renderer.SetAtmosphereOverride(Geni::Atmosphere{});
        clock->SetTime(19.0f);
        clock->Apply();
        CHECK(clock->GetState().sunElevation < 0.0f);
        CHECK(GeniTest::Near(clock->GetState().atmosphere.skyHorizon,
                             Geni::EvaluateTimeOfDay(19.0f).atmosphere.skyHorizon));

        // A stopped clock stays put.
        clock->SetDayLengthMinutes(0.0f);
        scene.Update(10.0f);
        CHECK(clock->GetTime() == doctest::Approx(19.0f));
    }

    TEST_CASE("the component's settings are reflected and round-trip")
    {
        Geni::RegisterBuiltinComponents();
        Geni::TimeOfDayComponent clock;
        clock.SetTime(21.25f);
        clock.SetDayLengthMinutes(12.0f);
        clock.SetMaxElevation(45.0f);
        clock.SetPathYaw(-30.0f);
        clock.SetSunIntensity(3.0f);
        clock.SetMoonIntensity(0.2f);
        clock.SetFogDensity(0.004f);

        const auto properties = Geni::TypeRegistry::Get().Serialize("TimeOfDayComponent", clock);
        CHECK(properties["time"] == doctest::Approx(21.25f));

        Geni::TimeOfDayComponent restored;
        Geni::SerializationContext context;
        REQUIRE(Geni::TypeRegistry::Get().Deserialize("TimeOfDayComponent", restored, properties, context));
        CHECK(restored.GetTime() == doctest::Approx(21.25f));
        CHECK(restored.GetDayLengthMinutes() == doctest::Approx(12.0f));
        CHECK(restored.GetMaxElevation() == doctest::Approx(45.0f));
        CHECK(restored.GetPathYaw() == doctest::Approx(-30.0f));
        CHECK(restored.GetSunIntensity() == doctest::Approx(3.0f));
        CHECK(restored.GetMoonIntensity() == doctest::Approx(0.2f));
        CHECK(restored.GetFogDensity() == doctest::Approx(0.004f));

        // Out-of-range values are clamped rather than producing a broken sky.
        restored.SetTime(-2.0f);
        restored.SetMaxElevation(200.0f);
        restored.SetDayLengthMinutes(-5.0f);
        CHECK(restored.GetTime() == doctest::Approx(22.0f));
        CHECK(restored.GetMaxElevation() == doctest::Approx(90.0f));
        CHECK(restored.GetDayLengthMinutes() == doctest::Approx(0.0f));
    }
}
