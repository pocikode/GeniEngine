// Timelines (docs/design/0026): data, sampling and the component's playback.

#include "TestEngine.h"

#include "core/Engine.h"
#include "scene/BuiltinComponents.h"
#include "scene/GameObject.h"
#include "scene/Scene.h"
#include "scene/Timeline.h"
#include "scene/components/CameraComponent.h"
#include "scene/components/TimelineComponent.h"
#include "script/ScriptSystem.h"

#include <doctest/doctest.h>

namespace
{

Geni::TimelineData Sample()
{
    Geni::TimelineData data;
    Geni::TimelineTrack track;
    track.target = "Door";
    for (int i = 0; i < 3; ++i)
    {
        Geni::TimelineKey key;
        key.time = static_cast<float>(i);
        key.position = glm::vec3(static_cast<float>(i) * 2.0f, 0.0f, 0.0f);
        key.rotation = glm::angleAxis(glm::radians(45.0f * static_cast<float>(i)), glm::vec3(0, 1, 0));
        track.keys.push_back(key);
    }
    data.transforms.push_back(track);
    data.events = {{0.0f, "start"}, {1.5f, "middle"}, {2.0f, "end"}};
    data.cuts = {{1.0f, "Close"}};
    return data;
}

} // namespace

TEST_SUITE("timeline")
{
    TEST_CASE("keys round-trip through JSON and sample smoothly through every key")
    {
        const Geni::TimelineData data = Sample();
        Geni::TimelineData again;
        REQUIRE(Geni::ParseTimeline(Geni::TimelineToJson(data), again));
        REQUIRE(again.transforms.size() == 1);
        CHECK(again.transforms[0].keys.size() == 3);
        CHECK(again.events.size() == 3);
        CHECK(again.LastKeyTime() == doctest::Approx(2.0f));

        glm::vec3 position;
        glm::quat rotation;
        glm::vec3 scale;
        REQUIRE(Geni::SampleTrack(again.transforms[0], 1.0f, position, rotation, scale));
        CHECK(GeniTest::Near(position, glm::vec3(2.0f, 0.0f, 0.0f)));
        Geni::SampleTrack(again.transforms[0], 0.5f, position, rotation, scale);
        // Half-way between the first two keys: the curve eases out of the first key (its
        // missing neighbour repeats it), so it is a little behind the straight line.
        CHECK(position.x == doctest::Approx(0.875f));
        CHECK(GeniTest::SameRotation(rotation, glm::angleAxis(glm::radians(22.5f), glm::vec3(0, 1, 0))));
        Geni::SampleTrack(again.transforms[0], 9.0f, position, rotation, scale); // holds the end
        CHECK(GeniTest::Near(position, glm::vec3(4.0f, 0.0f, 0.0f)));

        Geni::TimelineData bad;
        CHECK_FALSE(Geni::ParseTimeline(nlohmann::json::parse(R"({"transforms":[{"keys":[]}]})"), bad));
    }

    TEST_CASE("playing moves the targets, cuts cameras and fires each event once")
    {
        Geni::RegisterBuiltinComponents();
        Geni::Scene scene;
        Geni::GameObject *root = scene.CreateObject("Cutscene");
        Geni::GameObject *door = scene.CreateObject("Door");
        Geni::GameObject *close = scene.CreateObject("Close");
        close->AddComponent(new Geni::CameraComponent());
        auto *timeline = new Geni::TimelineComponent();
        root->AddComponent(timeline);
        timeline->SetData(Sample());

        // Editing previews the pose only.
        timeline->SetTime(2.0f);
        timeline->EditUpdate();
        CHECK(GeniTest::Near(door->GetPosition(), glm::vec3(4.0f, 0.0f, 0.0f)));
        CHECK(timeline->TakeEvents().empty());
        CHECK(scene.GetMainCamera() == nullptr);

        timeline->Stop();
        timeline->Play();
        std::vector<std::string> fired;
        for (int i = 0; i < 30; ++i)
        {
            scene.Update(0.1f);
            for (const auto &event : timeline->TakeEvents())
            {
                fired.push_back(event);
            }
        }
        CHECK(fired == std::vector<std::string>{"start", "middle", "end"});
        CHECK_FALSE(timeline->IsPlaying());
        CHECK(scene.GetMainCamera() == close);
        CHECK(GeniTest::Near(door->GetPosition(), glm::vec3(4.0f, 0.0f, 0.0f)));

        // Looping fires the events again on the next pass.
        timeline->SetLoop(true);
        timeline->Stop();
        timeline->Play();
        fired.clear();
        for (int i = 0; i < 35; ++i)
        {
            timeline->Advance(0.1f);
            for (const auto &event : timeline->TakeEvents())
            {
                fired.push_back(event);
            }
        }
        CHECK(std::count(fired.begin(), fired.end(), "start") == 2);
        CHECK(timeline->IsPlaying());
    }

    TEST_CASE("Lua plays, seeks and reads events")
    {
        auto &engine = Geni::Engine::GetInstance();
        auto *scene = new Geni::Scene();
        engine.SetScene(scene);
        Geni::ScriptConfig config;
        config.hotReload = false;
        REQUIRE(engine.GetScripts().Init(config, &engine.GetFileSystem()));
        Geni::GameObject *root = scene->CreateObject("Cutscene");
        scene->CreateObject("Door");
        auto *timeline = new Geni::TimelineComponent();
        root->AddComponent(timeline);
        timeline->SetData(Sample());
        REQUIRE(engine.GetScripts().Run(R"(
            local t = scene.find("Cutscene"):get("TimelineComponent")
            t:seek(1.0)
            assert(math.abs(scene.find("Door"):position().x - 2.0) < 1e-3)
            t:play()
            assert(t:playing())
            t.loop = true
        )"));
        CHECK(timeline->GetLoop());
        CHECK(timeline->IsPlaying());
        engine.SetScene(nullptr);
        engine.GetScripts().Shutdown();
    }
}
