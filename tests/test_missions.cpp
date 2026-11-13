// Missions and saved games (docs/design/0024).

#include "TestEngine.h"

#include "core/Engine.h"
#include "game/MissionSystem.h"
#include "game/SaveGame.h"
#include "scene/GameObject.h"
#include "scene/Scene.h"
#include "script/ScriptSystem.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <string>

namespace
{

const char *kMissions = R"({
  "format": "geni.mission", "version": 1,
  "missions": [
    { "id": "first", "title": "First", "objectives": [
        { "id": "car", "text": "Get in the car", "kind": "custom" },
        { "id": "drive", "text": "Drive to the square", "kind": "reach", "position": [1000, 0, 2000], "radius": 10 },
        { "id": "coins", "text": "Collect three coins", "kind": "count", "count": 3 } ] },
    { "id": "second", "title": "Second", "requires": ["first"], "timeLimit": 5,
      "objectives": [ { "id": "lamp", "text": "Find the lamp", "kind": "reach", "object": "Lamp", "radius": 2 } ] }
  ] })";

std::vector<std::string> EventNames(Geni::MissionSystem &missions)
{
    std::vector<std::string> names;
    for (const auto &event : missions.TakeEvents())
    {
        names.push_back(
            std::string(Geni::MissionEventName(event.type)) + ":" + event.mission +
            (event.objective.empty() ? "" : "/" + event.objective)
        );
    }
    return names;
}

std::filesystem::path TempSaves(const char *name)
{
    const auto folder = std::filesystem::temp_directory_path() / ("geni_saves_" + std::string(name));
    std::error_code ec;
    std::filesystem::remove_all(folder, ec);
    return folder;
}

} // namespace

TEST_SUITE("missions")
{
    TEST_CASE("objectives run in order; completing a mission unlocks the next")
    {
        Geni::MissionSystem missions;
        std::string error;
        REQUIRE_MESSAGE(missions.LoadJson(nlohmann::json::parse(kMissions), &error), error);
        CHECK(missions.GetState("first") == Geni::MissionState::Available);
        CHECK(missions.GetState("second") == Geni::MissionState::Locked);
        CHECK_FALSE(missions.Start("second"));
        CHECK(EventNames(missions) == std::vector<std::string>{"unlocked:first"});

        Geni::Scene scene;
        Geni::GameObject *player = scene.CreateObject("Player");
        missions.SetPlayer(player->GetID());
        REQUIRE(missions.Start("first"));
        CHECK(missions.GetCurrentObjective("first")->id == "car");
        // Out of order: the reach objective is not current yet, so being there does nothing.
        scene.SetOrigin(glm::dvec3(1000.0, 0.0, 2000.0));
        missions.Update(0.1f, &scene);
        CHECK(missions.GetCurrentObjective("first")->id == "car");
        CHECK_FALSE(missions.CompleteObjective("first", "drive"));
        CHECK(missions.CompleteObjective("first", "car"));

        // The player at scene (0, 0, 5) is world (1000, 0, 2005): inside the radius.
        player->SetPosition(glm::vec3(0.0f, 0.0f, 5.0f));
        missions.Update(0.1f, &scene);
        CHECK(missions.GetCurrentObjective("first")->id == "coins");
        CHECK(missions.AddProgress("first", "coins", 2));
        CHECK(missions.GetStatus("first")->progress[2] == 2);
        CHECK(missions.AddProgress("first", "", 5)); // clamped at the count
        CHECK(missions.GetState("first") == Geni::MissionState::Completed);
        CHECK(missions.GetState("second") == Geni::MissionState::Available);
        CHECK(
            EventNames(missions) == std::vector<std::string>{
                                        "started:first", "objective:first/car", "objective:first/drive",
                                        "objective:first/coins", "completed:first", "unlocked:second"
                                    }
        );
    }

    TEST_CASE("time limits fail a mission, which can be retried; named objects are reach targets")
    {
        Geni::MissionSystem missions;
        REQUIRE(missions.LoadJson(nlohmann::json::parse(kMissions)));
        missions.Start("first");
        missions.CompleteObjective("first");
        Geni::Scene scene;
        Geni::GameObject *player = scene.CreateObject("Player");
        Geni::GameObject *lamp = scene.CreateObject("Lamp");
        lamp->SetPosition(glm::vec3(50.0f, 0.0f, 0.0f));
        missions.SetPlayer(player->GetID());
        player->SetPosition(glm::vec3(1000.0f, 0.0f, 2005.0f));
        missions.Update(0.0f, &scene); // reach the square
        missions.AddProgress("first", "coins", 3);
        missions.TakeEvents();

        REQUIRE(missions.Start("second"));
        missions.Update(6.0f, &scene);
        CHECK(missions.GetState("second") == Geni::MissionState::Failed);
        CHECK(missions.GetStatus("second")->failReason == "time");
        CHECK(missions.Start("second"));
        player->SetPosition(glm::vec3(49.0f, 0.0f, 0.5f));
        missions.Update(0.5f, &scene);
        CHECK(missions.GetState("second") == Geni::MissionState::Completed);
    }

    TEST_CASE("mission state saves and restores")
    {
        Geni::MissionSystem missions;
        REQUIRE(missions.LoadJson(nlohmann::json::parse(kMissions)));
        missions.Start("first");
        missions.CompleteObjective("first");
        const nlohmann::json state = missions.SaveState();

        Geni::MissionSystem restored;
        REQUIRE(restored.LoadJson(nlohmann::json::parse(kMissions)));
        std::string error;
        REQUIRE_MESSAGE(restored.RestoreState(state, &error), error);
        CHECK(restored.GetState("first") == Geni::MissionState::Active);
        CHECK(restored.GetCurrentObjective("first")->id == "drive");
        CHECK(restored.TakeEvents().empty());
        CHECK_FALSE(restored.RestoreState(nlohmann::json::object()));
        CHECK_FALSE(missions.LoadJson(nlohmann::json::parse(R"({"format":"geni.mission","version":1,
            "missions":[{"id":"x","objectives":[{"id":"o","kind":"teleport"}]}]})")));
    }

    TEST_CASE("saves are written whole, read back, and only under safe names")
    {
        Geni::SaveGame saves;
        saves.Init("Test", TempSaves("files"));
        nlohmann::json document = {{"coins", 12}, {"name", "slot one"}};
        REQUIRE(saves.Write("slot1", document));
        CHECK(saves.Exists("slot1"));
        nlohmann::json read;
        REQUIRE(saves.Read("slot1", read));
        CHECK(read == document);
        CHECK_FALSE(std::filesystem::exists(saves.GetFolder() / "slot1.json.tmp"));
        document["coins"] = 13;
        REQUIRE(saves.Write("slot1", document)); // replaces
        REQUIRE(saves.Read("slot1", read));
        CHECK(read["coins"] == 13);
        CHECK_FALSE(saves.Write("../escape", document));
        CHECK_FALSE(saves.Write("a/b", document));
        CHECK_FALSE(saves.Read("missing", read));
        CHECK(saves.Delete("slot1"));
        CHECK_FALSE(saves.Exists("slot1"));
        CHECK_FALSE(Geni::SaveGame::UserDataFolder().empty());
    }

    TEST_CASE("Lua runs missions and writes saves")
    {
        auto &engine = Geni::Engine::GetInstance();
        auto *scene = new Geni::Scene();
        engine.SetScene(scene);
        engine.GetSaveGame().Init("Test", TempSaves("lua"));
        engine.GetMissions().Clear();
        REQUIRE(engine.GetMissions().LoadJson(nlohmann::json::parse(kMissions)));
        Geni::ScriptConfig config;
        config.hotReload = false;
        REQUIRE(engine.GetScripts().Init(config, &engine.GetFileSystem()));
        scene->CreateObject("Player");

        REQUIRE(engine.GetScripts().Run(R"(
            missions.set_player(scene.find("Player"))
            missions.events() -- "unlocked" from loading
            assert(missions.start("first"))
            assert(missions.current("first").id == "car")
            assert(missions.complete("first", "car"))
            local info = missions.info("first")
            assert(info.objectives[1].done and not info.objectives[2].done)
            assert(info.objectives[2].radius == 10)
            local events = missions.events()
            assert(#events == 2 and events[2].type == "objective")
            assert(save.write("progress", { missions = missions.state(), coins = 4 }))
            local back = save.read("progress")
            assert(back.coins == 4)
            missions.abandon("first")
            assert(missions.status("first") == "available")
            assert(missions.restore(back.missions))
            assert(missions.status("first") == "active")
            assert(save.read("nothing") == nil)
        )"));
        CHECK(engine.GetMissions().GetCurrentObjective("first")->id == "drive");

        engine.GetMissions().Clear();
        engine.SetScene(nullptr);
        engine.GetScripts().Shutdown();
    }
}
