// M6 gameplay systems: input actions (docs/design/0009), characters, vehicles and casts
// (0010), animation graphs (0011), audio (0012), Lua (0013), navigation (0014) and the
// game UI (0015). Everything except the "gpu" suites runs without a window.

#include "TestEngine.h"
#include "assets/AssetManager.h"

#include "animation/AnimationGraph.h"
#include "audio/AudioSystem.h"
#include "core/Engine.h"
#include "input/InputActions.h"
#include "input/InputManager.h"
#include "nav/NavigationSystem.h"
#include "physics/CharacterController.h"
#include "physics/PhysicsManager.h"
#include "physics/RigidBody.h"
#include "physics/Vehicle.h"
#include "scene/GameObject.h"
#include "scene/Scene.h"
#include "scene/components/AnimationComponent.h"
#include "scene/components/LightComponent.h"
#include "scene/components/NavAgentComponent.h"
#include "scene/components/PhysicsComponent.h"
#include "scene/components/ScriptComponent.h"
#include "script/ScriptSystem.h"
#include "ui/GameUI.h"

#include <GLFW/glfw3.h>
#include <doctest/doctest.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <glm/gtc/constants.hpp>
#include <memory>
#include <string>
#include <vector>

using GeniTest::Near;

namespace
{

std::filesystem::path TempFolder(const std::string &name)
{
    const auto folder = std::filesystem::temp_directory_path() / "geni_gameplay_tests" / name;
    std::error_code ec;
    std::filesystem::remove_all(folder, ec);
    std::filesystem::create_directories(folder);
    return folder;
}

void WriteText(const std::filesystem::path &path, const std::string &text)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << text;
}

// A mono 16-bit WAV of a sine tone.
void WriteTone(const std::filesystem::path &path, float seconds, float frequency, uint32_t rate = 22050)
{
    const uint32_t frames = static_cast<uint32_t>(seconds * static_cast<float>(rate));
    std::vector<int16_t> samples(frames);
    for (uint32_t i = 0; i < frames; ++i)
    {
        samples[i] = static_cast<int16_t>(
            12000.0f * std::sin(2.0f * glm::pi<float>() * frequency * static_cast<float>(i) / static_cast<float>(rate))
        );
    }
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    auto u32 = [&](uint32_t v) { out.write(reinterpret_cast<const char *>(&v), 4); };
    auto u16 = [&](uint16_t v) { out.write(reinterpret_cast<const char *>(&v), 2); };
    const uint32_t bytes = frames * 2;
    out.write("RIFF", 4);
    u32(36 + bytes);
    out.write("WAVEfmt ", 8);
    u32(16);
    u16(1);
    u16(1);
    u32(rate);
    u32(rate * 2);
    u16(2);
    u16(16);
    out.write("data", 4);
    u32(bytes);
    out.write(reinterpret_cast<const char *>(samples.data()), bytes);
}

float Peak(const std::vector<float> &samples)
{
    float peak = 0.0f;
    for (float s : samples)
    {
        peak = std::max(peak, std::fabs(s));
    }
    return peak;
}

// A static box body in the engine's physics world.
std::unique_ptr<Geni::RigidBody> StaticBox(const glm::vec3 &center, const glm::vec3 &size)
{
    auto body = std::make_unique<Geni::RigidBody>(
        Geni::BodyType::Static, std::make_shared<Geni::BoxCollider>(size), 0.0f, 0.8f
    );
    body->SetWorldPosition(glm::dvec3(center));
    Geni::Engine::GetInstance().GetPhysicsManager().AddRigidBody(body.get());
    return body;
}

// The engine's physics world, started for a test that runs without the engine.
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

std::shared_ptr<Geni::AnimationClip> RotationClip(const std::string &name, float degrees, float duration)
{
    auto clip = std::make_shared<Geni::AnimationClip>();
    clip->name = name;
    clip->duration = duration;
    Geni::TransformTrack track;
    track.targetName = "Bone";
    const glm::quat q = glm::angleAxis(glm::radians(degrees), glm::vec3(0.0f, 1.0f, 0.0f));
    track.rotations.push_back({0.0f, q});
    track.rotations.push_back({duration, q});
    clip->tracks.push_back(track);
    return clip;
}

float YawDegrees(const glm::quat &q)
{
    const glm::vec3 forward = q * glm::vec3(0.0f, 0.0f, -1.0f);
    return glm::degrees(std::atan2(-forward.x, -forward.z));
}

} // namespace

TEST_SUITE("input actions")
{
    TEST_CASE("buttons report held, pressed and released from any bound key or pad button")
    {
        Geni::InputManager input;
        input.SetGamepadPolling(false);
        Geni::InputActions actions;
        CHECK(actions.FindAction("jump"));

        actions.Update(input);
        CHECK_FALSE(actions.IsDown("jump"));

        input.SetKeyPressed(GLFW_KEY_SPACE, true);
        actions.Update(input);
        CHECK(actions.IsDown("jump"));
        CHECK(actions.WasPressed("jump"));
        input.EndFrame();
        actions.Update(input);
        CHECK(actions.IsDown("jump"));
        CHECK_FALSE(actions.WasPressed("jump"));

        input.SetKeyPressed(GLFW_KEY_SPACE, false);
        input.EndFrame();
        actions.Update(input);
        CHECK(actions.WasReleased("jump"));

        // The same action from a gamepad, which becomes the last device used.
        Geni::GamepadState pad;
        pad.connected = true;
        pad.buttons[static_cast<size_t>(Geni::GamepadButton::A)] = true;
        input.SetGamepadState(0, pad);
        actions.Update(input);
        CHECK(actions.WasPressed("jump"));
        CHECK(actions.GetLastDevice() == Geni::InputDevice::Gamepad);
    }

    TEST_CASE("axes sum their bindings with dead zones, triggers and inversion")
    {
        Geni::InputManager input;
        input.SetGamepadPolling(false);
        Geni::InputActions actions;

        input.SetKeyPressed(GLFW_KEY_A, true);
        actions.Update(input);
        CHECK(Near(actions.GetValue("move_x"), -1.0f));
        input.SetKeyPressed(GLFW_KEY_D, true);
        actions.Update(input);
        CHECK(Near(actions.GetValue("move_x"), 0.0f));
        input.SetKeyPressed(GLFW_KEY_A, false);
        input.SetKeyPressed(GLFW_KEY_D, false);

        Geni::GamepadState pad;
        pad.connected = true;
        pad.axes[static_cast<size_t>(Geni::GamepadAxis::LeftX)] = 0.1f;  // inside the dead zone
        pad.axes[static_cast<size_t>(Geni::GamepadAxis::LeftY)] = -1.0f; // pushed up
        pad.axes[static_cast<size_t>(Geni::GamepadAxis::RightTrigger)] = 1.0f;
        input.SetGamepadState(0, pad);
        actions.Update(input);
        CHECK(Near(actions.GetValue("move_x"), 0.0f));
        CHECK(Near(actions.GetValue("move_y"), 1.0f)); // the binding inverts the stick's Y
        CHECK(Near(actions.GetValue("throttle"), 1.0f));
        // Only the bound direction of a stick presses a menu button.
        CHECK(actions.IsDown("ui_up"));
        CHECK_FALSE(actions.IsDown("ui_down"));

        float x = 0.0f;
        float y = 0.0f;
        pad.axes[static_cast<size_t>(Geni::GamepadAxis::LeftX)] = 1.0f;
        input.SetGamepadState(0, pad);
        actions.Update(input);
        actions.GetVector("move_x", "move_y", x, y);
        CHECK(Near(std::sqrt(x * x + y * y), 1.0f, 1e-3f));
    }

    TEST_CASE("bindings load from JSON, save back, and groups switch actions off")
    {
        Geni::InputActions actions;
        std::string error;
        const nlohmann::json document = nlohmann::json::parse(R"({"actions": {
            "fire": {"type": "button", "group": "combat", "bindings": ["mouse:Left", "pad:RT"]},
            "lean": {"type": "axis", "bindings": [{"control": "key:Q", "scale": -1}, "key:E", "key:Nonsense"]}
        }})");
        CHECK_FALSE(actions.LoadJson(document, false, &error)); // the unknown key is reported
        REQUIRE(actions.FindAction("fire"));
        REQUIRE(actions.FindAction("lean"));
        CHECK(actions.FindAction("lean")->bindings.size() == 2);
        CHECK(actions.FindAction("jump")); // defaults kept without `replace`

        Geni::InputActions reloaded;
        reloaded.LoadJson(actions.SaveJson(), true);
        CHECK(reloaded.FindAction("lean")->bindings[0].scale == -1.0f);

        Geni::InputManager input;
        input.SetGamepadPolling(false);
        input.SetMouseButtonPressed(GLFW_MOUSE_BUTTON_LEFT, true);
        actions.SetGroupEnabled("combat", false);
        actions.Update(input);
        CHECK_FALSE(actions.IsDown("fire"));
        actions.SetGroupEnabled("combat", true);
        actions.Update(input);
        CHECK(actions.IsDown("fire"));
    }

    TEST_CASE("keys above 255 (Escape, Shift) are tracked")
    {
        Geni::InputManager input;
        input.SetKeyPressed(GLFW_KEY_ESCAPE, true);
        input.SetKeyPressed(GLFW_KEY_LEFT_SHIFT, true);
        CHECK(input.IsKeyPressed(GLFW_KEY_ESCAPE));
        CHECK(input.IsKeyPressed(GLFW_KEY_LEFT_SHIFT));
    }
}

TEST_SUITE("animation graph")
{
    const char *kGraph = R"({
        "parameters": {"speed": 0.0, "grounded": true, "jump": "trigger"},
        "initial": "move",
        "states": {
            "move": {"blend": {"parameter": "speed", "clips": [["Idle", 0.0], ["Run", 2.0]]}},
            "jump": {"clip": "Jump", "loop": false}
        },
        "transitions": [
            {"from": "move", "to": "jump", "when": [["jump"]], "duration": 0.2},
            {"from": "jump", "to": "move", "when": [["grounded", "==", true]], "exitTime": 0.5, "duration": 0.1}
        ],
        "events": [{"clip": "Run", "time": 0.5, "name": "footstep"}]
    })";

    struct Fixture
    {
        std::shared_ptr<Geni::AnimationClip> idle = RotationClip("Idle", 0.0f, 1.0f);
        std::shared_ptr<Geni::AnimationClip> run = RotationClip("Run", 90.0f, 1.0f);
        std::shared_ptr<Geni::AnimationClip> jump = RotationClip("Jump", -90.0f, 1.0f);
        Geni::AnimationGraphInstance instance;

        Fixture()
        {
            std::string error;
            auto graph = Geni::AnimationGraph::Parse(nlohmann::json::parse(kGraph), &error);
            REQUIRE_MESSAGE(graph, error);
            Geni::AnimPose rest;
            rest.Resize(1);
            instance.Bind(
                graph,
                [this](const std::string &name) -> const Geni::AnimationClip * {
                    return name == "Idle"   ? idle.get()
                           : name == "Run"  ? run.get()
                           : name == "Jump" ? jump.get()
                                            : nullptr;
                },
                {"Bone"}, rest
            );
        }

        float SampleYaw()
        {
            Geni::AnimPose pose;
            instance.Sample(pose);
            return YawDegrees(pose.rotations[0]);
        }
    };

    TEST_CASE("bad graphs are rejected with a reason")
    {
        std::string error;
        CHECK_FALSE(Geni::AnimationGraph::Parse(nlohmann::json::parse(R"({"states": {}})"), &error));
        CHECK_FALSE(error.empty());
        CHECK_FALSE(
            Geni::AnimationGraph::Parse(
                nlohmann::json::parse(R"({"states": {"a": {"clip": "x"}}, "transitions": [{"from": "a", "to": "b"}]})"),
                &error
            )
        );
        CHECK(error.find("unknown state") != std::string::npos);
    }

    TEST_CASE("a 1D blend space mixes its neighbouring clips by the parameter")
    {
        Fixture f;
        CHECK(f.instance.GetStateName() == "move");
        f.instance.Advance(0.0f);
        CHECK(Near(f.SampleYaw(), 0.0f, 0.5f));
        f.instance.SetFloat("speed", 1.0f);
        f.instance.Advance(0.016f);
        CHECK(Near(f.SampleYaw(), 45.0f, 0.5f));
        f.instance.SetFloat("speed", 5.0f);
        f.instance.Advance(0.016f);
        CHECK(Near(f.SampleYaw(), 90.0f, 0.5f));
    }

    TEST_CASE("a trigger starts a cross-fade that ends in the new state")
    {
        Fixture f;
        f.instance.SetFloat("speed", 2.0f);
        f.instance.Advance(0.016f);
        f.instance.SetTrigger("jump");
        f.instance.Advance(0.1f); // halfway through the 0.2 s fade from 90 to -90 degrees
        CHECK(f.instance.GetStateName() == "jump");
        CHECK(f.instance.IsInTransition());
        CHECK(Near(f.SampleYaw(), 0.0f, 2.0f));
        f.instance.Advance(0.15f);
        CHECK_FALSE(f.instance.IsInTransition());
        CHECK(Near(f.SampleYaw(), -90.0f, 0.5f));
        // A trigger nothing consumed does not linger.
        f.instance.SetTrigger("jump");
        f.instance.Advance(0.0f);
        CHECK(f.instance.GetStateName() == "jump");
    }

    TEST_CASE("exit time holds a transition back until the state has played that far")
    {
        Fixture f;
        f.instance.SetTrigger("jump");
        f.instance.Advance(0.3f);
        CHECK(f.instance.GetStateName() == "jump");
        f.instance.Advance(0.1f); // 0.4 into a 1 s clip: before exitTime 0.5
        CHECK(f.instance.GetStateName() == "jump");
        f.instance.Advance(0.2f);
        f.instance.Advance(0.0f);
        CHECK(f.instance.GetStateName() == "move");
    }

    TEST_CASE("events fire once when their clip time is crossed")
    {
        Fixture f;
        f.instance.SetFloat("speed", 2.0f);
        f.instance.Advance(0.4f);
        CHECK(f.instance.TakeEvents().empty());
        f.instance.Advance(0.2f);
        const auto events = f.instance.TakeEvents();
        REQUIRE(events.size() == 1);
        CHECK(events[0] == "footstep");
        CHECK(f.instance.TakeEvents().empty());
        f.instance.Advance(1.0f); // a full loop crosses it again
        CHECK(f.instance.TakeEvents().size() == 1);
    }

    TEST_CASE("poses blend rotations along the shortest arc")
    {
        Geni::AnimPose a;
        Geni::AnimPose b;
        a.Resize(1);
        b.Resize(1);
        a.rotations[0] = glm::angleAxis(glm::radians(170.0f), glm::vec3(0, 1, 0));
        b.rotations[0] = -glm::angleAxis(glm::radians(-170.0f), glm::vec3(0, 1, 0)); // same as 190, flipped sign
        Geni::BlendPoses(a, b, 0.5f);
        CHECK(Near(std::fabs(YawDegrees(a.rotations[0])), 180.0f, 0.5f));
    }
}

TEST_SUITE("characters and vehicles")
{
    TEST_CASE("a character walks, climbs a kerb, stops at a wall and jumps")
    {
        PhysicsWorld world;
        auto floor = StaticBox({0.0f, -0.5f, 0.0f}, {60.0f, 1.0f, 60.0f});
        auto kerb = StaticBox({0.0f, 0.15f, -8.0f}, {6.0f, 0.3f, 4.0f}); // a 30 cm step, z -10..-6
        auto wall = StaticBox({0.0f, 2.0f, -14.0f}, {6.0f, 4.0f, 1.0f}); // front face at z = -13.5
        world.Get().FlushPendingBodies();

        Geni::CharacterController character(world.Get(), Geni::CharacterSettings(), glm::dvec3(0.0, 0.05, 0.0));
        const float dt = 1.0f / 60.0f;
        for (int i = 0; i < 30; ++i)
        {
            character.Step(dt, glm::vec3(0.0f), 0.0f);
            world.Get().Step(dt);
        }
        CHECK(character.IsGrounded());
        CHECK(std::fabs(character.GetPosition().y) < 0.05);

        // Walk 3 m/s towards -Z for 2 s: onto the kerb.
        for (int i = 0; i < 120; ++i)
        {
            character.Step(dt, glm::vec3(0.0f, 0.0f, -3.0f), 0.0f);
            world.Get().Step(dt);
        }
        CHECK(character.GetPosition().z < -5.5);
        CHECK(Near(static_cast<float>(character.GetPosition().y), 0.3f, 0.06f));

        // Keep walking: the wall stops it.
        for (int i = 0; i < 180; ++i)
        {
            character.Step(dt, glm::vec3(0.0f, 0.0f, -3.0f), 0.0f);
            world.Get().Step(dt);
        }
        CHECK(character.GetPosition().z > -13.5);
        CHECK(character.GetPosition().z < -12.5);

        // Jump: it leaves the ground and comes back down.
        double peak = character.GetPosition().y;
        character.Step(dt, glm::vec3(0.0f), 5.0f);
        world.Get().Step(dt);
        for (int i = 0; i < 90; ++i)
        {
            character.Step(dt, glm::vec3(0.0f), 0.0f);
            world.Get().Step(dt);
            peak = std::max(peak, character.GetPosition().y);
        }
        CHECK(peak > 1.1); // v^2 / 2g = 1.27 m at 5 m/s
        CHECK(character.IsGrounded());
    }

    TEST_CASE("a car settles on its wheels, accelerates, steers and brakes")
    {
        PhysicsWorld world;
        auto floor = StaticBox({0.0f, -0.5f, 0.0f}, {600.0f, 1.0f, 600.0f});
        world.Get().FlushPendingBodies();
        Geni::Vehicle car(world.Get(), Geni::VehicleSettings(), glm::dvec3(0.0, 1.2, 0.0), glm::quat(1, 0, 0, 0));
        const float dt = 1.0f / 60.0f;
        for (int i = 0; i < 90; ++i)
        {
            car.SetInput(0.0f, 0.0f, 0.0f, 0.0f);
            world.Get().Step(dt);
        }
        for (int w = 0; w < 4; ++w)
        {
            CHECK(car.IsWheelOnGround(w));
        }
        CHECK(car.GetPosition().y > 0.3);
        CHECK(car.GetPosition().y < 1.2);

        for (int i = 0; i < 240; ++i)
        {
            car.SetInput(1.0f, 0.0f, 0.0f, 0.0f);
            world.Get().Step(dt);
        }
        const float speed = car.GetForwardSpeed();
        MESSAGE("after 4 s: " << speed * 3.6f << " km/h in gear " << car.GetGear() << " at " << car.GetRpm() << " rpm");
        CHECK(speed > 10.0f);               // over 36 km/h after 4 s
        CHECK(car.GetGear() >= 2);          // the automatic gearbox shifts (it does not while wheels spin)
        CHECK(car.GetPosition().z < -15.0); // forward is -Z
        CHECK(std::fabs(car.GetPosition().x) < 1.0);
        CHECK(car.GetRpm() > 900.0f);

        const float yawBefore = YawDegrees(car.GetRotation());
        for (int i = 0; i < 60; ++i)
        {
            car.SetInput(0.5f, 1.0f, 0.0f, 0.0f); // right
            world.Get().Step(dt);
        }
        const float yawAfter = YawDegrees(car.GetRotation());
        CHECK(yawAfter < yawBefore - 10.0f); // turning right is a negative yaw

        for (int i = 0; i < 240; ++i)
        {
            car.SetInput(0.0f, 0.0f, 1.0f, 0.0f);
            world.Get().Step(dt);
        }
        CHECK(std::fabs(car.GetForwardSpeed()) < 1.0f);
    }

    TEST_CASE("rays and spheres hit the closest body on the requested layers")
    {
        PhysicsWorld world;
        auto floor = StaticBox({0.0f, -0.5f, 0.0f}, {20.0f, 1.0f, 20.0f});
        auto crate = std::make_unique<Geni::RigidBody>(
            Geni::BodyType::Dynamic, std::make_shared<Geni::BoxCollider>(glm::vec3(1.0f)), 1.0f, 0.5f
        );
        crate->SetWorldPosition(glm::dvec3(0.0, 3.0, 0.0));
        crate->SetUserData(77);
        world.Get().AddRigidBody(crate.get());
        world.Get().FlushPendingBodies();

        Geni::PhysicsHit hit;
        REQUIRE(world.Get().CastRay(glm::dvec3(0.0, 10.0, 0.0), glm::vec3(0, -1, 0), 50.0f, hit));
        CHECK(Near(hit.distance, 6.5f, 0.05f)); // the crate's top
        CHECK(hit.userData == 77);
        CHECK(Near(hit.normal, glm::vec3(0, 1, 0), 1e-3f));

        REQUIRE(
            world.Get().CastRay(glm::dvec3(0.0, 10.0, 0.0), glm::vec3(0, -1, 0), 50.0f, hit, Geni::PhysicsLayerStatic)
        );
        CHECK(Near(hit.distance, 10.0f, 0.05f)); // straight through to the floor
        CHECK(world.Get().CastRay(
            glm::dvec3(0.0, 10.0, 0.0), glm::vec3(0, -1, 0), 50.0f, hit, Geni::PhysicsLayerAll, crate->GetBodyId()
        ));
        CHECK(Near(hit.distance, 10.0f, 0.05f));

        REQUIRE(world.Get().CastSphere(glm::dvec3(-10.0, 3.0, 0.0), 0.5f, glm::vec3(1, 0, 0), 20.0f, hit));
        CHECK(Near(hit.distance, 9.0f, 0.05f)); // centre stops 0.5 m before the crate's side
        CHECK_FALSE(world.Get().CastRay(glm::dvec3(0.0, 10.0, 0.0), glm::vec3(0, 1, 0), 50.0f, hit));
    }

    TEST_CASE("a kinematic body follows its targets and stops when they stop")
    {
        PhysicsWorld world;
        auto platform = std::make_unique<Geni::RigidBody>(
            Geni::BodyType::Kinematic, std::make_shared<Geni::BoxCollider>(glm::vec3(2.0f, 0.2f, 2.0f)), 0.0f, 0.5f
        );
        world.Get().AddRigidBody(platform.get());
        world.Get().FlushPendingBodies();
        const float dt = 1.0f / 60.0f;
        world.Get().SetKinematicTarget(platform->GetBodyId(), glm::dvec3(1.0, 0.0, 0.0), glm::quat(1, 0, 0, 0));
        world.Get().Step(dt);
        CHECK(Near(static_cast<float>(platform->GetWorldPosition().x), 1.0f, 1e-3f));
        CHECK(world.Get().GetBodyLinearVelocity(platform->GetBodyId()).x > 50.0f);
        world.Get().Step(dt); // no new target: it stays
        CHECK(Near(static_cast<float>(platform->GetWorldPosition().x), 1.0f, 1e-3f));
    }
}

TEST_SUITE("navigation")
{
    // A floor with a wall across the middle, as scene objects with static colliders.
    Geni::Scene *WallScene()
    {
        auto *scene = new Geni::Scene();
        auto addBox = [&](const char *name, glm::vec3 position, glm::vec3 scale) {
            Geni::GameObject *object = scene->CreateObject(name);
            object->SetPosition(position);
            object->SetScale(scale);
            auto *physics = new Geni::PhysicsComponent();
            physics->SetBodyType(Geni::BodyType::Static);
            object->AddComponent(physics);
        };
        addBox("Floor", {0.0f, -0.5f, 0.0f}, {40.0f, 1.0f, 40.0f});
        addBox("Wall", {0.0f, 2.0f, -3.0f}, {1.0f, 4.0f, 26.0f}); // z -16..10, a gap at z 10..20
        return scene;
    }

    TEST_CASE("a mesh is built from colliders and a path goes around the wall")
    {
        std::unique_ptr<Geni::Scene> scene(WallScene());
        Geni::NavigationSystem navigation;
        Geni::NavMeshSettings settings;
        settings.tileSize = 32.0f;
        REQUIRE(navigation.Init(settings));
        REQUIRE(navigation.BuildForScene(*scene));
        CHECK(navigation.GetStats().tiles >= 4);

        glm::dvec3 nearest;
        REQUIRE(navigation.FindNearestPoint(glm::dvec3(-5.0, 1.0, 0.0), nearest));
        CHECK(std::fabs(nearest.y) < 0.3);
        // From inside the wall the nearest walkable point is beside it, an agent radius out.
        REQUIRE(navigation.FindNearestPoint(glm::dvec3(0.0, 0.0, 0.0), nearest, 2.0f));
        CHECK(std::fabs(nearest.x) > 0.85);

        std::vector<glm::dvec3> corners;
        REQUIRE(navigation.FindPath(glm::dvec3(-5.0, 0.0, 0.0), glm::dvec3(5.0, 0.0, 0.0), corners));
        REQUIRE(corners.size() >= 3);
        double length = 0.0;
        double furthestZ = 0.0;
        for (size_t i = 1; i < corners.size(); ++i)
        {
            length += glm::length(corners[i] - corners[i - 1]);
            furthestZ = std::max(furthestZ, corners[i].z);
        }
        CHECK(length > 20.0);   // around the end of the wall, not through it
        CHECK(furthestZ > 9.5); // through the gap at z > 10

        glm::dvec3 random;
        REQUIRE(navigation.RandomPointNear(glm::dvec3(-8.0, 0.0, -8.0), 3.0f, random));
        // Detour keeps the search near the circle but not exactly inside it.
        CHECK(std::fabs(random.y) < 0.3);
        CHECK(glm::length(random - glm::dvec3(-8.0, 0.0, -8.0)) < 12.0);
    }

    TEST_CASE("crowd agents walk to their targets and tiles can be dropped and rebuilt")
    {
        std::unique_ptr<Geni::Scene> scene(WallScene());
        Geni::NavigationSystem navigation;
        Geni::NavMeshSettings settings;
        settings.tileSize = 32.0f;
        REQUIRE(navigation.Init(settings));
        REQUIRE(navigation.BuildForScene(*scene));

        const int agent = navigation.AddAgent(glm::dvec3(-5.0, 0.0, 15.0), Geni::NavAgentParams());
        REQUIRE(agent >= 0);
        CHECK(navigation.AddAgent(glm::dvec3(100.0, 0.0, 100.0), Geni::NavAgentParams()) < 0); // off the mesh
        REQUIRE(navigation.SetAgentTarget(agent, glm::dvec3(5.0, 0.0, 15.0)));
        for (int i = 0; i < 600 && !navigation.GetAgent(agent).arrived; ++i)
        {
            navigation.Update(1.0f / 30.0f);
        }
        const Geni::NavAgentState state = navigation.GetAgent(agent);
        CHECK(state.arrived);
        CHECK(glm::length(state.position - glm::dvec3(5.0, 0.0, 15.0)) < 1.0);

        int tx = 0;
        int tz = 0;
        navigation.TileAt(glm::dvec3(5.0, 0.0, 15.0), tx, tz);
        CHECK(navigation.HasTile(tx, tz));
        navigation.RemoveTile(tx, tz);
        CHECK_FALSE(navigation.HasTile(tx, tz));
        Geni::NavGeometry geometry;
        for (const auto &object : scene->GetObjects())
        {
            geometry.AddColliders(*object);
        }
        navigation.BuildTileAsync(tx, tz, geometry); // no job system: builds now
        navigation.FinishBuilds();
        CHECK(navigation.HasTile(tx, tz));
    }
}

TEST_SUITE("audio")
{
    TEST_CASE("the engine plays a sound's cooked ADPCM variant, as long as the source")
    {
        Geni::FileSystem files;
        files.SetAssetsFolder(GENI_TEST_ASSETS_DIR);
        Geni::AssetManager assets;
        assets.Init(&files, nullptr, nullptr);
        const Geni::AssetID desktop = assets.FindBySource(Geni::AssetType::Sound, "sounds/tone_long.wav#desktop");
        const Geni::AssetID mobile = assets.FindBySource(Geni::AssetType::Sound, "sounds/tone_long.wav#mobile");
        REQUIRE(desktop.IsValid());
        REQUIRE(mobile.IsValid());
        const auto cookedFile = assets.FindCookedFile(desktop, Geni::AssetType::Sound);
        REQUIRE_FALSE(cookedFile.empty());
        CHECK(cookedFile.extension() == ".gsnd");
        std::error_code ec;
        CHECK(std::filesystem::file_size(cookedFile, ec) < 132344 / 3); // ADPCM, not PCM

        Geni::AudioSystem audio;
        Geni::AudioConfig config;
        config.device = false;
        config.sampleRate = 22050;
        config.channels = 1;
        config.variant = "desktop";
        REQUIRE(audio.Init(config, &files));
        std::string resolved;
        audio.SetCookedResolver([&](const std::string &path) {
            const Geni::AssetID id = assets.FindBySource(Geni::AssetType::Sound, path + "#" + audio.GetVariant());
            const auto file = assets.FindCookedFile(id, Geni::AssetType::Sound);
            resolved = file.string();
            return file;
        });
        const Geni::SoundId id = audio.Play("sounds/tone_long.wav");
        REQUIRE(id != Geni::InvalidSound);
        CHECK(resolved.find(".gsnd") != std::string::npos);

        // 3 s at 22050 Hz: still playing after 2.9 s, finished by 3.1 s, and loud throughout.
        std::vector<float> buffer(2205);
        float quietest = 1.0f;
        for (int block = 0; block < 29; ++block)
        {
            REQUIRE(audio.ReadFrames(buffer.data(), buffer.size()) == buffer.size());
            quietest = std::min(quietest, Peak(buffer));
        }
        CHECK(quietest > 0.3f);
        CHECK(audio.IsPlaying(id));
        for (int block = 0; block < 3; ++block)
        {
            audio.ReadFrames(buffer.data(), buffer.size());
        }
        audio.Update(0.0f);
        CHECK_FALSE(audio.IsPlaying(id));
        audio.Shutdown();
        assets.Shutdown();
    }

    TEST_CASE("without a device the mixer plays one-shot, 3D and streamed sounds")
    {
        const auto folder = TempFolder("audio");
        WriteTone(folder / "sounds" / "tone.wav", 0.25f, 440.0f);
        Geni::FileSystem files;
        files.SetAssetsFolder(folder);
        Geni::AudioSystem audio;
        Geni::AudioConfig config;
        config.device = false;
        REQUIRE(audio.Init(config, &files));
        CHECK_FALSE(audio.HasDevice());

        std::vector<float> buffer(2048 * audio.GetChannels());
        const Geni::SoundId id = audio.Play("sounds/tone.wav");
        REQUIRE(id != Geni::InvalidSound);
        CHECK(audio.IsPlaying(id));
        REQUIRE(audio.ReadFrames(buffer.data(), 2048) == 2048);
        const float loud = Peak(buffer);
        CHECK(loud > 0.1f);
        audio.Stop(id);
        audio.Update(0.0f);
        CHECK(audio.GetActiveSoundCount() == 0);

        CHECK(audio.Play("sounds/missing.wav") == Geni::InvalidSound);

        // A 3D sound far from the listener is quieter than one next to it.
        audio.SetListener(glm::vec3(0.0f), glm::vec3(0, 0, -1), glm::vec3(0, 1, 0));
        Geni::SoundParams far;
        far.spatial = true;
        far.position = glm::vec3(0.0f, 0.0f, -60.0f);
        const Geni::SoundId distant = audio.Play("sounds/tone.wav", far);
        // The spatializer eases its gain in over a few hundred frames; measure after that.
        audio.ReadFrames(buffer.data(), 1024);
        audio.ReadFrames(buffer.data(), 2048);
        const float quiet = Peak(buffer);
        CHECK(quiet < loud * 0.25f);
        CHECK(quiet > 0.0f);
        audio.Stop(distant);
        audio.Update(0.0f);

        // A one-shot finishes by itself as the mixer advances.
        audio.Play("sounds/tone.wav");
        for (int i = 0; i < 10; ++i)
        {
            audio.Update(0.05f);
        }
        CHECK(audio.GetActiveSoundCount() == 0);

        // Music streams and loops until stopped with a fade; bus volumes scale it.
        audio.PlayMusic("sounds/tone.wav", 0.0f);
        CHECK(audio.GetMusicPath() == "sounds/tone.wav");
        for (int i = 0; i < 10; ++i)
        {
            audio.Update(0.05f);
        }
        CHECK(audio.GetActiveSoundCount() == 1);
        audio.SetBusVolume(Geni::AudioBus::Music, 0.0f);
        audio.ReadFrames(buffer.data(), 2048);
        CHECK(Peak(buffer) < 1e-4f);
        audio.SetBusVolume(Geni::AudioBus::Music, 1.0f);
        audio.StopMusic(0.1f);
        for (int i = 0; i < 10; ++i)
        {
            audio.Update(0.05f);
        }
        CHECK(audio.GetActiveSoundCount() == 0);
        audio.Shutdown();
    }
}

TEST_SUITE("lua scripting")
{
    struct LuaFixture
    {
        std::filesystem::path folder = TempFolder("lua");
        Geni::Engine &engine = Geni::Engine::GetInstance();
        Geni::Scene *scene = new Geni::Scene();

        LuaFixture()
        {
            engine.GetFileSystem().SetAssetsFolder(folder);
            Geni::ScriptConfig config;
            config.hotReload = false;
            REQUIRE(engine.GetScripts().Init(config, &engine.GetFileSystem()));
            engine.SetScene(scene);
        }
        ~LuaFixture()
        {
            engine.SetScene(nullptr);
            engine.GetScripts().Shutdown();
            engine.GetFileSystem().SetAssetsFolder(std::filesystem::path());
        }
        std::string Eval(const std::string &code)
        {
            std::string result;
            std::string error;
            CHECK_MESSAGE(engine.GetScripts().Evaluate(code, result, &error), error);
            return result;
        }
    };

    TEST_CASE("the state has vec3, safe libraries and require from scripts/")
    {
        LuaFixture f;
        CHECK(f.Eval("1 + 2") == "3");
        CHECK(f.Eval("(vec3(1, 2, 2) + vec3(0, 0, 1)):length() == math.sqrt(14)") == "true");
        CHECK(f.Eval("tostring(vec3(1, 0, 0):cross(vec3(0, 1, 0)))") == "(0.000, 0.000, 1.000)");
        CHECK(f.Eval("io == nil and os.execute == nil and loadfile == nil and os.clock ~= nil") == "true");
        WriteText(f.folder / "scripts" / "util" / "maths.lua", "return { double = function(x) return 2 * x end }");
        CHECK(f.Eval("require('util.maths').double(21)") == "42");
        std::string result;
        CHECK_FALSE(f.engine.GetScripts().Evaluate("require('nope')", result));
    }

    TEST_CASE("objects and reflected component properties are reachable from Lua")
    {
        LuaFixture f;
        Geni::GameObject *lamp = f.scene->CreateObject("Lamp");
        CHECK(f.Eval("scene.find('Lamp'):name()") == "Lamp");
        CHECK(f.Eval("scene.find('Nothing') == nil") == "true");
        REQUIRE(f.engine.GetScripts().Run(
            "local l = scene.find('Lamp'):add('LightComponent', {intensity = 3.5}); l.range = 12; "
            "scene.find('Lamp'):set_position(vec3(1, 2, 3))"
        ));
        auto *light = lamp->GetComponent<Geni::LightComponent>();
        REQUIRE(light);
        CHECK(Near(light->GetIntensity(), 3.5f));
        CHECK(Near(light->GetRange(), 12.0f));
        CHECK(Near(lamp->GetPosition(), glm::vec3(1, 2, 3)));
        CHECK(f.Eval("scene.find('Lamp'):get('LightComponent').range") == "12.0");
        CHECK(f.Eval("scene.find('Lamp'):get('LightComponent').type") == "point");
        // A handle to a destroyed object reports it instead of crashing.
        REQUIRE(f.engine.GetScripts().Run("held = scene.find('Lamp')"));
        f.scene->DestroyObject(lamp);
        CHECK(f.Eval("held:valid()") == "false");
        std::string result;
        CHECK_FALSE(f.engine.GetScripts().Evaluate("held:name()", result));
    }

    TEST_CASE("a script component runs start, update and destroy, survives errors and hot reloads")
    {
        LuaFixture f;
        WriteText(f.folder / "scripts" / "counter.lua", R"(
local Counter = {}
function Counter:start() self.count = 0; self.object:set_name("started") end
function Counter:update(dt)
  self.count = self.count + (self.step or 1)
  self.object:set_position(vec3(self.count, 0, 0))
end
function Counter:destroy() destroyed = (destroyed or 0) + 1 end
return Counter
)");
        Geni::GameObject *object = f.scene->CreateObject("Counter");
        auto *script = new Geni::ScriptComponent();
        script->SetScript("scripts/counter.lua");
        object->AddComponent(script);
        for (int i = 0; i < 3; ++i)
        {
            f.scene->Update(0.016f);
        }
        CHECK(object->GetName() == "started");
        CHECK(Near(object->GetPosition().x, 3.0f));

        // Edit the file: the instance keeps its count, the new code runs.
        WriteText(f.folder / "scripts" / "counter.lua", R"(
local Counter = {}
function Counter:update(dt) self.count = self.count + 10; self.object:set_position(vec3(self.count, 0, 0)) end
return Counter
)");
        std::filesystem::last_write_time(
            f.folder / "scripts" / "counter.lua",
            std::filesystem::last_write_time(f.folder / "scripts" / "counter.lua") + std::chrono::seconds(5)
        );
        CHECK(f.engine.GetScripts().ReloadChanged() == 1);
        f.scene->Update(0.016f);
        CHECK(Near(object->GetPosition().x, 13.0f));

        // A runtime error stops this component, not the game.
        const uint64_t errors = f.engine.GetScripts().GetErrorCount();
        WriteText(
            f.folder / "scripts" / "broken.lua", "local B = {}\nfunction B:update() error('boom') end\nreturn B\n"
        );
        Geni::GameObject *broken = f.scene->CreateObject("Broken");
        auto *brokenScript = new Geni::ScriptComponent();
        brokenScript->SetScript("scripts/broken.lua");
        broken->AddComponent(brokenScript);
        f.scene->Update(0.016f);
        f.scene->Update(0.016f);
        CHECK(brokenScript->HasFailed());
        CHECK(f.engine.GetScripts().GetErrorCount() == errors + 1);

        // Params set fields before start.
        WriteText(
            f.folder / "scripts" / "params.lua",
            "local P = {}\nfunction P:start() self.object:set_name(self.label .. self.n) end\nreturn P\n"
        );
        Geni::GameObject *withParams = f.scene->CreateObject("P");
        auto *paramsScript = new Geni::ScriptComponent();
        paramsScript->SetScript("scripts/params.lua");
        paramsScript->SetParams(R"({"label": "car", "n": 7})");
        withParams->AddComponent(paramsScript);
        f.scene->Update(0.016f);
        CHECK(withParams->GetName() == "car7");

        // The reloaded class has no destroy method; the params script's has none either.
        f.scene->DestroyObject(object);
        CHECK(f.Eval("destroyed == nil") == "true");
        WriteText(f.folder / "scripts" / "bye.lua", "local B = {}\nfunction B:destroy() destroyed = 1 end\nreturn B\n");
        Geni::GameObject *bye = f.scene->CreateObject("Bye");
        auto *byeScript = new Geni::ScriptComponent();
        byeScript->SetScript("scripts/bye.lua");
        bye->AddComponent(byeScript);
        f.scene->Update(0.016f);
        f.scene->DestroyObject(bye);
        CHECK(f.Eval("destroyed") == "1");
    }
}

TEST_SUITE("game ui")
{
    TEST_CASE("without Dear ImGui every call is harmless")
    {
        Geni::GameUI ui;
        CHECK_FALSE(ui.IsAvailable());
        ui.Text("hello", Geni::UiAnchor::Center, glm::vec2(0.0f));
        ui.BeginMenu("menu", "Title");
        CHECK_FALSE(ui.Button("Play"));
        ui.EndMenu();
    }
}

TEST_SUITE("gpu game ui")
{
    TEST_CASE("menus move focus with ui actions, skip labels and activate with accept")
    {
        GeniTest::RequireEngine();
        auto &engine = Geni::Engine::GetInstance();
        Geni::GameUI &ui = engine.GetGameUI();
        REQUIRE(ui.IsAvailable());
        Geni::InputManager input;
        input.SetGamepadPolling(false);
        Geni::InputActions actions;

        auto frame = [&](int key, bool &play, bool &quit, float &volume) {
            if (key >= 0)
            {
                input.SetKeyPressed(key, true);
            }
            actions.Update(input);
            ui.BeginFrame(actions, false); // keyboard only: the desktop pointer must not interfere
            ui.BeginMenu("test", "Paused");
            play = ui.Button("Resume");
            ui.Label("Settings");
            ui.Slider("Volume", volume, 0.0f, 1.0f, 0.25f);
            quit = ui.Button("Quit");
            ui.EndMenu();
            input.EndFrame();
            if (key >= 0)
            {
                input.SetKeyPressed(key, false);
            }
            input.EndFrame();
            actions.Update(input);
        };
        bool play = false;
        bool quit = false;
        float volume = 0.5f;
        frame(-1, play, quit, volume);
        CHECK(ui.GetFocus("test") == 0);
        frame(GLFW_KEY_DOWN, play, quit, volume);
        CHECK(ui.GetFocus("test") == 2); // the label is skipped
        frame(GLFW_KEY_RIGHT, play, quit, volume);
        CHECK(Near(volume, 0.75f));
        frame(GLFW_KEY_DOWN, play, quit, volume);
        CHECK(ui.GetFocus("test") == 3);
        frame(GLFW_KEY_ENTER, play, quit, volume);
        CHECK(quit);
        CHECK_FALSE(play);
        frame(GLFW_KEY_DOWN, play, quit, volume); // wraps to the top
        CHECK(ui.GetFocus("test") == 0);
        CHECK(ui.GetCanvasSize().y == 1080.0f);
        CHECK(ui.MeasureText("Resume", 32.0f) > 40.0f);
    }
}
