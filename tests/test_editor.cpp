// Tests for geni_editor_core (docs/design/0006): the command stack, every undoable edit
// of EditorDocument, play and stop, the gizmo maths, the editor camera and the project
// layer. No window and no GPU: the document edits a plain Scene.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "core/CommandStack.h"
#include "core/EditorCamera.h"
#include "core/EditorCommands.h"
#include "core/EditorDocument.h"
#include "core/Gizmo.h"
#include "core/Project.h"
#include "core/AnimGraphEditing.h"
#include "core/MaterialEditing.h"
#include "core/TimelineEditing.h"

#include "assets/AssetManager.h"
#include "io/FileSystem.h"
#include "scene/BuiltinComponents.h"
#include "scene/GameObject.h"
#include "scene/Reflection.h"
#include "scene/Scene.h"
#include "scene/SceneSerializer.h"
#include "scene/components/CameraComponent.h"
#include "scene/components/LightComponent.h"
#include "scene/components/TimelineComponent.h"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <glm/gtc/quaternion.hpp>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

using namespace Geni;
using namespace Geni::Editor;

namespace
{

bool Near(const glm::vec3 &a, const glm::vec3 &b, float epsilon = 1e-3f)
{
    return glm::length(a - b) < epsilon;
}

size_t CountAll(const Scene &scene)
{
    size_t count = 0;
    std::vector<const GameObject *> stack;
    for (const auto &root : scene.GetObjects())
    {
        stack.push_back(root.get());
    }
    while (!stack.empty())
    {
        const GameObject *object = stack.back();
        stack.pop_back();
        ++count;
        for (const auto &child : object->GetChildren())
        {
            stack.push_back(child.get());
        }
    }
    return count;
}

// A command that adds to a number, to watch the stack work.
class AddCommand : public Command
{
  public:
    AddCommand(int &target, int amount) : m_target(target), m_amount(amount)
    {
    }
    const char *GetName() const override
    {
        return "Add";
    }
    void Do(EditContext &) override
    {
        m_target += m_amount;
    }
    void Undo(EditContext &) override
    {
        m_target -= m_amount;
    }

  private:
    int &m_target;
    int m_amount;
};

size_t CellFileCount(const EditorDocument &document)
{
    size_t count = 0;
    for (const auto &cell : document.GetRegionCells())
    {
        count += document.CellFileExists(cell.x, cell.z) ? 1 : 0;
    }
    return count;
}

// A scratch project folder with an asset manager reading from it.
struct ProjectFixture
{
    std::filesystem::path root;
    FileSystem fileSystem;
    AssetManager assets;

    explicit ProjectFixture(const char *name)
    {
        root = std::filesystem::temp_directory_path() / "geni_editor_tests" / name;
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root / "assets");
        fileSystem.SetAssetsFolder(root / "assets");
        fileSystem.SetCookedFolder(root / "cooked");
        assets.Init(&fileSystem, nullptr, nullptr);
    }
    ~ProjectFixture()
    {
        assets.Shutdown();
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
};

} // namespace

TEST_SUITE("editor commands")
{
    TEST_CASE("the stack undoes, redoes and drops the redo tail on a new edit")
    {
        CommandStack stack;
        EditContext context;
        int value = 0;
        stack.Execute(std::make_unique<AddCommand>(value, 1), context);
        stack.Execute(std::make_unique<AddCommand>(value, 10), context);
        CHECK(value == 11);
        CHECK(stack.CanUndo());
        CHECK_FALSE(stack.CanRedo());

        CHECK(stack.Undo(context));
        CHECK(value == 1);
        CHECK(stack.CanRedo());
        CHECK(stack.Redo(context));
        CHECK(value == 11);

        stack.Undo(context);
        stack.Execute(std::make_unique<AddCommand>(value, 100), context);
        CHECK(value == 101);
        CHECK_FALSE(stack.CanRedo());
        CHECK(stack.GetCount() == 2);

        stack.Undo(context);
        stack.Undo(context);
        CHECK(value == 0);
        CHECK_FALSE(stack.Undo(context));
    }

    TEST_CASE("the document is dirty exactly when it differs from what was saved")
    {
        CommandStack stack;
        EditContext context;
        int value = 0;
        CHECK_FALSE(stack.IsDirty());
        stack.Execute(std::make_unique<AddCommand>(value, 1), context);
        CHECK(stack.IsDirty());
        stack.MarkSaved();
        CHECK_FALSE(stack.IsDirty());

        stack.Undo(context);
        CHECK(stack.IsDirty()); // behind the saved state
        stack.Redo(context);
        CHECK_FALSE(stack.IsDirty());

        // Undo past the save, then edit: the saved state cannot be reached any more.
        stack.Undo(context);
        stack.Execute(std::make_unique<AddCommand>(value, 5), context);
        CHECK(stack.IsDirty());
        stack.Undo(context);
        CHECK(stack.IsDirty());
    }

    TEST_CASE("old commands fall off a full stack")
    {
        CommandStack stack(3);
        EditContext context;
        int value = 0;
        stack.MarkSaved();
        for (int i = 0; i < 5; ++i)
        {
            stack.Execute(std::make_unique<AddCommand>(value, 1), context);
        }
        CHECK(stack.GetCount() == 3);
        while (stack.Undo(context))
        {
        }
        CHECK(value == 2);       // two edits can no longer be undone
        CHECK(stack.IsDirty());  // so the saved state is out of reach
    }

    TEST_CASE("a composite undoes its parts in reverse")
    {
        EditContext context;
        int value = 1;
        // (1 + 4) then a second command that depends on the first having run.
        class DoubleCommand : public Command
        {
          public:
            explicit DoubleCommand(int &target) : m_target(target)
            {
            }
            const char *GetName() const override
            {
                return "Double";
            }
            void Do(EditContext &) override
            {
                m_target *= 2;
            }
            void Undo(EditContext &) override
            {
                m_target /= 2;
            }
            int &m_target;
        };
        CompositeCommand composite("both");
        composite.Add(std::make_unique<AddCommand>(value, 4));
        composite.Add(std::make_unique<DoubleCommand>(value));
        composite.Do(context);
        CHECK(value == 10);
        composite.Undo(context);
        CHECK(value == 1);
    }
}

TEST_SUITE("editor document")
{
    TEST_CASE("an animation graph edits stay consistent and valid")
    {
        AnimGraphDocument graph;
        REQUIRE(graph.Load(nlohmann::ordered_json::parse(R"({
            "parameters": { "speed": 0.0, "grounded": true, "jump": "trigger" },
            "initial": "move",
            "states": { "move": { "clip": "Walk" }, "jump": { "clip": "Jump", "loop": false } },
            "transitions": [ { "from": "move", "to": "jump", "when": [["jump"]], "duration": 0.08 } ],
            "custom": 42 })")));
        CHECK(graph.Validate().empty());
        CHECK(graph.GetStates() == std::vector<std::string>{"move", "jump"});

        // Renaming follows into transitions, the initial state and positions.
        graph.SetPosition("move", glm::vec2(10, 20));
        REQUIRE(graph.RenameState("move", "locomotion"));
        CHECK(graph.GetInitial() == "locomotion");
        CHECK(graph.GetTransitions()[0].from == "locomotion");
        CHECK(graph.GetPosition("locomotion") == glm::vec2(10, 20));
        CHECK_FALSE(graph.RenameState("jump", "locomotion"));

        const int back = graph.AddTransition("jump", "locomotion");
        REQUIRE(back == 1);
        AnimTransitionView view = graph.GetTransitions()[1];
        view.conditions = "grounded == true; speed > 0.5";
        view.exitTime = 0.35f;
        REQUIRE(graph.SetTransition(back, view));
        CHECK(graph.GetTransitions()[1].conditions == "grounded == true; speed > 0.5");
        CHECK(graph.Validate().empty());
        view.conditions = "speed >";
        std::string error;
        CHECK_FALSE(graph.SetTransition(back, view, &error));
        CHECK_FALSE(error.empty());
        CHECK(graph.AddTransition("locomotion", "locomotion") < 0);
        CHECK(graph.AddTransition("*", "jump") == 2);

        // A state without a clip is caught by the runtime's parser.
        REQUIRE(graph.AddState("crouch", ""));
        CHECK_FALSE(graph.Validate().empty());
        REQUIRE(graph.SetStateClip("crouch", "Crouch"));
        CHECK(graph.Validate().empty());

        // Removing a state removes its transitions; the last state cannot go.
        REQUIRE(graph.RemoveState("jump"));
        CHECK(graph.GetTransitions().empty());
        CHECK(graph.GetJson()["custom"] == 42); // fields the editor does not know survive
        REQUIRE(graph.RemoveState("crouch"));
        CHECK_FALSE(graph.RemoveState("locomotion"));
    }

    TEST_CASE("a material file loads with every standard parameter, edits and saves back")
    {
        MaterialSource source;
        REQUIRE(source.Load(nlohmann::json::parse(R"({
            "shader": "builtin:default",
            "params": { "float2": { "uMetallicRoughness": [0.5, 0.25] },
                        "textures": [ { "name": "baseColorTexture", "path": "textures/brick.png" } ] } })")));
        // Listed although the file leaves them out, with glTF defaults.
        REQUIRE(source.Find("uBaseColorFactor"));
        CHECK(source.Find("uBaseColorFactor")->value == glm::vec4(1.0f));
        CHECK(source.Find("emissiveTexture"));
        // The float2 lands in the standard float4 member.
        CHECK(source.Find("uMetallicRoughness")->kind == MaterialParamKind::Float4);
        CHECK(source.Find("uMetallicRoughness")->value.y == doctest::Approx(0.25f));
        CHECK(source.Find("baseColorTexture")->texture == "textures/brick.png");

        CHECK(source.Set("uBaseColorFactor", MaterialParamKind::Float4, glm::vec4(1, 0, 0, 1)));
        CHECK_FALSE(source.Set("baseColorTexture", MaterialParamKind::Float, glm::vec4(1)));
        CHECK(source.SetTexture("normalTexture", "textures/brick_n.png"));
        const nlohmann::json saved = source.ToJson();
        CHECK(saved["shader"] == "builtin:default");
        CHECK(saved["params"]["float4"]["uBaseColorFactor"][1] == 0.0);
        CHECK(saved["params"]["textures"].size() == 2);
        MaterialSource again;
        REQUIRE(again.Load(saved));
        CHECK(again.Find("normalTexture")->texture == "textures/brick_n.png");

        // A custom shader keeps only what the file has, and names its .slang for both stages.
        MaterialSource custom;
        REQUIRE(custom.Load(nlohmann::json::parse(
            R"({"shader": {"vertex": "shaders/water.slang", "fragment": "shaders/water.slang"},
                "params": {"float": {"uSpeed": 2}}})"
        )));
        CHECK_FALSE(custom.IsStandard());
        CHECK(custom.params.size() == 1);
        CHECK(custom.ToJson()["shader"]["fragment"] == "shaders/water.slang");
    }

    TEST_CASE("an editable copy of a cooked material keeps its values and texture paths")
    {
        cooked::CookedMaterialData cooked;
        cooked.shader = "builtin:default";
        cooked.float4s["uBaseColorFactor"] = glm::vec4(0.2f, 0.4f, 0.6f, 1.0f);
        cooked.float4s["uMetallicRoughness"] = glm::vec4(1.0f, 0.3f, 0.0f, 0.0f);
        cooked.textures["baseColorTexture"] = AssetID::FromName("texture:a");
        cooked.textures["normalTexture"] = AssetID::FromName("texture:missing");
        const MaterialSource source = MaterialSource::FromCooked(cooked, [](AssetID id) {
            return id == AssetID::FromName("texture:a") ? std::string("textures/a.png") : std::string();
        });
        CHECK(source.Find("uBaseColorFactor")->value.z == doctest::Approx(0.6f));
        CHECK(source.Find("uMetallicRoughness")->value.y == doctest::Approx(0.3f));
        CHECK(source.Find("baseColorTexture")->texture == "textures/a.png");
        CHECK(source.Find("normalTexture")->texture.empty()); // unknown image: left empty
    }

    TEST_CASE("timeline keys are set, moved and removed in order, and undo through the document")
    {
        TimelineData data;
        TimelineKey key;
        key.time = 2.0f;
        CHECK(SetTimelineKey(data, "Door", key) == 0);
        key.time = 1.0f;
        key.position = glm::vec3(1, 0, 0);
        SetTimelineKey(data, "Door", key);
        key.time = 1.0f + TimelineKeySnap * 0.5f; // the same key, replaced
        key.position = glm::vec3(5, 0, 0);
        SetTimelineKey(data, "Door", key);
        REQUIRE(data.transforms[0].keys.size() == 2);
        CHECK(data.transforms[0].keys[0].position.x == doctest::Approx(5.0f));
        CHECK(MoveTimelineKey(data, 0, 0, 3.0f) == 1); // now after the key at 2 s
        CHECK(data.transforms[0].keys[1].time == doctest::Approx(3.0f));
        CHECK(MoveTimelineKey(data, 0, 0, 3.0f) == 0); // onto it: replaces it
        CHECK(data.transforms[0].keys.size() == 1);
        CHECK(RemoveTimelineKey(data, 0, 0));
        CHECK_FALSE(RemoveTimelineKey(data, 0, 0));
        CHECK(AddTimelineTrack(data, "Lamp") == 1);
        CHECK(FindTimelineTrack(data, "Lamp") == 1);
        CHECK(RemoveTimelineTrack(data, 0));
        CHECK(FindTimelineTrack(data, "Lamp") == 0);

        RegisterBuiltinComponents();
        Scene scene;
        EditorDocument document(scene, nullptr);
        GameObject *cutscene = document.CreateEmpty("Cutscene");
        REQUIRE(document.AddComponent(*cutscene, "TimelineComponent"));
        auto *timeline = cutscene->GetComponent<TimelineComponent>();
        TimelineData keyed;
        key.time = 0.5f;
        SetTimelineKey(keyed, "Lamp", key);
        REQUIRE(document.SetProperty(*cutscene, 0, "tracks", nlohmann::json(TimelineToJson(keyed).dump())));
        CHECK(timeline->GetData().transforms.size() == 1);
        REQUIRE(document.Undo());
        CHECK(cutscene->GetComponent<TimelineComponent>()->GetData().transforms.empty());
    }

    TEST_CASE("creating, renaming and moving objects undo and redo")
    {
        RegisterBuiltinComponents();
        Scene scene;
        EditorDocument document(scene, nullptr);

        GameObject *object = document.CreateEmpty("Crate");
        REQUIRE(object != nullptr);
        const uint64_t id = object->GetID();
        CHECK(document.IsSelected(id));
        CHECK(document.IsDirty());

        NodeState state = NodeState::Capture(*object);
        state.name = "Box";
        state.position = glm::vec3(1, 2, 3);
        REQUIRE(document.SetNodeState(*object, state));
        CHECK_FALSE(document.SetNodeState(*object, state)); // no change, no undo step
        CHECK(object->GetName() == "Box");

        REQUIRE(document.Undo());
        CHECK(object->GetName() == "Crate");
        CHECK(Near(object->GetPosition(), glm::vec3(0)));
        REQUIRE(document.Undo()); // the creation
        CHECK(scene.FindObjectByID(id) == nullptr);
        CHECK(document.GetSelection().empty());
        CHECK_FALSE(document.IsDirty());

        REQUIRE(document.Redo());
        REQUIRE(document.Redo());
        GameObject *restored = scene.FindObjectByID(id); // same identity, new object
        REQUIRE(restored != nullptr);
        CHECK(restored->GetName() == "Box");
        CHECK(Near(restored->GetPosition(), glm::vec3(1, 2, 3)));
    }

    TEST_CASE("a finished drag is one undo step")
    {
        Scene scene;
        EditorDocument document(scene, nullptr);
        GameObject *a = document.CreateEmpty("a");
        GameObject *b = document.CreateEmpty("b");
        const size_t before = document.GetCommands().GetCount();

        // What a gizmo does: move the objects every frame, commit once at the end.
        const std::vector<std::pair<uint64_t, NodeState>> start = {
            {a->GetID(), NodeState::Capture(*a)}, {b->GetID(), NodeState::Capture(*b)}
        };
        for (int frame = 1; frame <= 10; ++frame)
        {
            a->SetPosition(glm::vec3(static_cast<float>(frame), 0, 0));
            b->SetPosition(glm::vec3(0, static_cast<float>(frame), 0));
        }
        REQUIRE(document.CommitNodeStates("Move", start));
        CHECK(document.GetCommands().GetCount() == before + 1);
        CHECK_FALSE(document.CommitNodeStates("Move", {{a->GetID(), NodeState::Capture(*a)}})); // nothing moved

        document.Undo();
        CHECK(Near(a->GetPosition(), glm::vec3(0)));
        CHECK(Near(b->GetPosition(), glm::vec3(0)));
        document.Redo();
        CHECK(Near(a->GetPosition(), glm::vec3(10, 0, 0)));
        CHECK(Near(b->GetPosition(), glm::vec3(0, 10, 0)));
    }

    TEST_CASE("deleting restores the subtree, its place and the main camera")
    {
        RegisterBuiltinComponents();
        Scene scene;
        EditorDocument document(scene, nullptr);
        document.CreateEmpty("first");
        GameObject *rig = document.CreateEmpty("rig");
        document.CreateEmpty("last");
        GameObject *camera = document.CreateCamera();
        REQUIRE(scene.GetMainCamera() == camera);
        REQUIRE(document.Reparent({camera}, rig));
        const uint64_t rigID = rig->GetID();
        const uint64_t cameraID = camera->GetID();
        REQUIRE(rig->GetSiblingIndex() == 1);

        document.Select(rigID);
        REQUIRE(document.DeleteSelection());
        CHECK(scene.FindObjectByID(rigID) == nullptr);
        CHECK(scene.FindObjectByID(cameraID) == nullptr);
        CHECK(scene.GetMainCamera() == nullptr);
        CHECK(document.GetSelection().empty());
        CHECK(scene.GetObjects().size() == 2);

        REQUIRE(document.Undo());
        GameObject *restoredRig = scene.FindObjectByID(rigID);
        GameObject *restoredCamera = scene.FindObjectByID(cameraID);
        REQUIRE(restoredRig != nullptr);
        REQUIRE(restoredCamera != nullptr);
        CHECK(restoredRig->GetSiblingIndex() == 1);
        CHECK(restoredCamera->GetParent() == restoredRig);
        CHECK(restoredCamera->GetComponent<CameraComponent>() != nullptr);
        CHECK(scene.GetMainCamera() == restoredCamera);

        REQUIRE(document.Redo());
        CHECK(scene.FindObjectByID(rigID) == nullptr);
        // Deleting a parent and its selected child deletes the subtree once.
        document.Undo();
        document.Select(rigID);
        document.Select(cameraID, true);
        CHECK(document.GetSelectedRoots().size() == 1);
        REQUIRE(document.DeleteSelection());
        REQUIRE(document.Undo());
        CHECK(CountAll(scene) == 4);
    }

    TEST_CASE("reparenting keeps the object where it is in the world")
    {
        Scene scene;
        EditorDocument document(scene, nullptr);
        GameObject *parent = document.CreateEmpty("parent");
        parent->SetPosition(glm::vec3(10, 0, 0));
        parent->SetRotation(glm::angleAxis(glm::radians(90.0f), glm::vec3(0, 1, 0)));
        parent->SetScale(glm::vec3(2.0f));
        GameObject *child = document.CreateEmpty("child");
        child->SetPosition(glm::vec3(3, 4, 5));

        REQUIRE(document.Reparent({child}, parent));
        CHECK(child->GetParent() == parent);
        CHECK(Near(child->GetWorldPosition(), glm::vec3(3, 4, 5)));
        CHECK_FALSE(Near(child->GetPosition(), glm::vec3(3, 4, 5)));

        REQUIRE(document.Undo());
        CHECK(child->GetParent() == nullptr);
        CHECK(Near(child->GetPosition(), glm::vec3(3, 4, 5)));
        REQUIRE(document.Redo());
        CHECK(child->GetParent() == parent);
        CHECK(Near(child->GetWorldPosition(), glm::vec3(3, 4, 5)));

        // Not into itself or its own subtree.
        CHECK_FALSE(document.Reparent({parent}, child));
        CHECK_FALSE(document.Reparent({parent}, parent));
    }

    TEST_CASE("components and their properties undo and redo")
    {
        RegisterBuiltinComponents();
        Scene scene;
        EditorDocument document(scene, nullptr);
        GameObject *object = document.CreateEmpty("lamp");

        REQUIRE(document.AddComponent(*object, "LightComponent"));
        CHECK_FALSE(document.AddComponent(*object, "NoSuchComponent"));
        auto *light = object->GetComponent<LightComponent>();
        REQUIRE(light != nullptr);

        REQUIRE(document.SetProperty(*object, 0, "intensity", 7.5f));
        REQUIRE(document.SetProperty(*object, 0, "type", "spot"));
        CHECK_FALSE(document.SetProperty(*object, 0, "type", "no such type"));
        CHECK_FALSE(document.SetProperty(*object, 0, "no such property", 1));
        CHECK_FALSE(document.SetProperty(*object, 0, "intensity", 7.5f)); // unchanged
        CHECK(light->GetIntensity() == doctest::Approx(7.5f));
        CHECK(light->GetType() == LightType::Spot);

        document.Undo();
        CHECK(light->GetType() == LightType::Point);
        document.Undo();
        CHECK(light->GetIntensity() == doctest::Approx(1.0f));
        document.Redo();
        document.Redo();

        // Removing remembers the values; undo puts an equal component back in its place.
        REQUIRE(document.AddComponent(*object, "CameraComponent"));
        REQUIRE(document.RemoveComponent(*object, 0));
        CHECK(object->GetComponent<LightComponent>() == nullptr);
        REQUIRE(document.Undo());
        REQUIRE(object->GetComponents().size() == 2);
        CHECK(std::string(object->GetComponents()[0]->GetTypeName()) == "LightComponent");
        light = object->GetComponent<LightComponent>();
        CHECK(light->GetIntensity() == doctest::Approx(7.5f));
        CHECK(light->GetType() == LightType::Spot);

        // An edit previewed on the live component becomes a command when it is committed.
        const nlohmann::json before = 7.5f;
        REQUIRE(PropertyCommand::Set(document.GetContext(), object->GetID(), 0, "intensity", 2.0f));
        REQUIRE(PropertyCommand::Set(document.GetContext(), object->GetID(), 0, "intensity", 3.0f));
        REQUIRE(document.CommitProperty(object->GetID(), 0, "intensity", before));
        document.Undo();
        CHECK(light->GetIntensity() == doctest::Approx(7.5f));
    }

    TEST_CASE("duplicating copies subtrees; a grid is one undo step")
    {
        RegisterBuiltinComponents();
        Scene scene;
        EditorDocument document(scene, nullptr);
        GameObject *building = document.CreateEmpty("building");
        building->SetPosition(glm::vec3(1, 0, 1));
        document.AddComponent(*building, "LightComponent");
        document.CreateEmpty("roof", building);
        document.Select(building->GetID());

        const auto copies = document.DuplicateSelection();
        REQUIRE(copies.size() == 1);
        CHECK(copies[0] != building);
        CHECK(copies[0]->GetChildren().size() == 1);
        CHECK(copies[0]->GetComponent<LightComponent>() != nullptr);
        CHECK(document.IsSelected(copies[0]->GetID()));
        CHECK(CountAll(scene) == 4);
        document.Undo();
        CHECK(CountAll(scene) == 2);

        document.Select(building->GetID());
        const auto grid = document.DuplicateGrid(4, 3, glm::vec3(2.0f, 0.0f, 5.0f));
        CHECK(grid.size() == 11); // 4 x 3 minus the original
        CHECK(scene.GetObjects().size() == 12);
        CHECK(Near(grid.back()->GetPosition(), glm::vec3(1 + 3 * 2.0f, 0, 1 + 2 * 5.0f)));
        CHECK(scene.CountComponents<LightComponent>() == 12);
        document.Undo();
        CHECK(scene.GetObjects().size() == 1);
        document.Redo();
        CHECK(scene.GetObjects().size() == 12);
    }

    TEST_CASE("play runs the scene and stop puts back what was edited")
    {
        RegisterBuiltinComponents();
        Scene scene;
        EditorDocument document(scene, nullptr);
        GameObject *object = document.CreateEmpty("mover");
        object->SetPosition(glm::vec3(1, 2, 3));
        GameObject *camera = document.CreateCamera();
        const uint64_t id = object->GetID();
        const uint64_t cameraID = camera->GetID();
        document.Select(id);
        const size_t history = document.GetCommands().GetCount();

        REQUIRE(document.Play());
        CHECK(document.IsPlaying());
        CHECK_FALSE(document.Play());
        // The scene was rebuilt from the snapshot: same identities, new objects.
        GameObject *playing = scene.FindObjectByID(id);
        REQUIRE(playing != nullptr);
        CHECK(scene.GetMainCamera() == scene.FindObjectByID(cameraID));

        // The game does what games do; the editor refuses edits meanwhile.
        playing->SetPosition(glm::vec3(50, 50, 50));
        scene.CreateObject("spawned at runtime");
        scene.Update(1.0f / 60.0f);
        CHECK(document.CreateEmpty("not now") == nullptr);
        CHECK_FALSE(document.DeleteSelection());
        CHECK_FALSE(document.Undo());
        std::string error;
        CHECK_FALSE(document.SaveAs(std::filesystem::temp_directory_path() / "geni_never_written.gscene", &error));

        REQUIRE(document.Stop());
        CHECK_FALSE(document.IsPlaying());
        GameObject *stopped = scene.FindObjectByID(id);
        REQUIRE(stopped != nullptr);
        CHECK(Near(stopped->GetPosition(), glm::vec3(1, 2, 3)));
        CHECK(scene.FindObject("spawned at runtime") == nullptr);
        CHECK(scene.GetObjects().size() == 2);
        CHECK(scene.GetMainCamera() == scene.FindObjectByID(cameraID));
        CHECK(document.IsSelected(id));
        // Playing is not an edit, and the history still works afterwards.
        CHECK(document.GetCommands().GetCount() == history);
        REQUIRE(document.Undo());
        CHECK(scene.FindObjectByID(cameraID) == nullptr);
    }

    TEST_CASE("a scene is saved, reopened and tracked as clean or dirty")
    {
        RegisterBuiltinComponents();
        ProjectFixture fixture("save_open");
        Scene scene;
        EditorDocument document(scene, &fixture.assets);
        std::string error;
        CHECK_FALSE(document.Save(&error)); // no file yet

        GameObject *sun = document.CreateLight(LightType::Directional);
        document.CreateCamera();
        const glm::quat rotation = sun->GetRotation();
        const auto path = fixture.root / "assets" / "scenes" / "test.gscene";
        REQUIRE_MESSAGE(document.SaveAs(path, &error), error);
        CHECK_FALSE(document.IsDirty());
        CHECK(document.GetPath() == path);

        document.CreateEmpty("extra");
        CHECK(document.IsDirty());
        REQUIRE(document.Save(&error));
        CHECK_FALSE(document.IsDirty());
        document.Undo();
        CHECK(document.IsDirty());

        Scene other;
        EditorDocument reopened(other, &fixture.assets);
        REQUIRE_MESSAGE(reopened.Open(path, &error), error);
        CHECK_FALSE(reopened.IsDirty());
        CHECK_FALSE(reopened.IsPrefabDocument());
        CHECK(other.GetObjects().size() == 3);
        GameObject *loadedSun = other.FindObject("Directional Light");
        REQUIRE(loadedSun != nullptr);
        CHECK(std::abs(glm::dot(loadedSun->GetRotation(), rotation)) == doctest::Approx(1.0f));
        REQUIRE(other.GetMainCamera() != nullptr);
        CHECK(other.GetMainCamera()->GetName() == "Camera");

        CHECK_FALSE(reopened.Open(fixture.root / "assets" / "missing.gscene", &error));
        std::ofstream(fixture.root / "assets" / "broken.gscene") << "{ not json";
        CHECK_FALSE(reopened.Open(fixture.root / "assets" / "broken.gscene", &error));
    }

    TEST_CASE("an object becomes a prefab instance, is unpacked, and both undo")
    {
        RegisterBuiltinComponents();
        ProjectFixture fixture("prefab_edit");
        Scene scene;
        EditorDocument document(scene, &fixture.assets);
        GameObject *lamp = document.CreateEmpty("Lamp");
        lamp->SetPosition(glm::vec3(4, 0, 0));
        document.AddComponent(*lamp, "LightComponent");
        document.CreateEmpty("Bulb", lamp);
        const uint64_t id = lamp->GetID();

        const AssetID prefab = fixture.assets.RegisterSourceAsset(AssetType::Prefab, "prefabs/lamp.gprefab");
        std::string error;
        REQUIRE_MESSAGE(
            document.CreatePrefab(*lamp, fixture.root / "assets" / "prefabs" / "lamp.gprefab", prefab, &error), error
        );
        GameObject *instance = scene.FindObjectByID(id);
        REQUIRE(instance != nullptr);
        CHECK(instance->IsPrefabInstance());
        CHECK(Near(instance->GetPosition(), glm::vec3(4, 0, 0)));
        CHECK(instance->GetComponent<LightComponent>() != nullptr);
        REQUIRE(instance->GetChildren().size() == 1);
        GameObject *bulb = instance->GetChildren()[0].get();
        CHECK(bulb->IsFromPrefab());

        // What came from the prefab is read-only in the scene.
        CHECK_FALSE(document.CanEdit(bulb));
        CHECK_FALSE(document.SetProperty(*instance, 0, "intensity", 9.0f));
        CHECK_FALSE(document.AddComponent(*instance, "CameraComponent"));
        document.Select(bulb->GetID());
        CHECK_FALSE(document.DeleteSelection());

        // A second instance, then the prefab file changes under both.
        GameObject *second = document.InstantiatePrefab(prefab);
        REQUIRE(second != nullptr);
        {
            Scene edit;
            EditorDocument prefabDocument(edit, &fixture.assets);
            REQUIRE(prefabDocument.Open(fixture.root / "assets" / "prefabs" / "lamp.gprefab", &error));
            CHECK(prefabDocument.IsPrefabDocument());
            prefabDocument.CreateEmpty("Shade", edit.GetObjects()[0].get());
            REQUIRE_MESSAGE(prefabDocument.Save(&error), error);
            // A prefab file holds one root.
            prefabDocument.CreateEmpty("second root");
            CHECK_FALSE(prefabDocument.Save(&error));
        }
        REQUIRE(fixture.assets.Reload(prefab));
        CHECK(document.RefreshPrefabInstances(prefab) == 2);
        CHECK(scene.FindObjectByID(id)->GetChildren().size() == 2);
        CHECK(second->GetChildren().size() == 2);

        // Unpack: plain objects that can be edited; undo makes it an instance again.
        instance = scene.FindObjectByID(id);
        REQUIRE(document.UnpackPrefab(*instance));
        CHECK_FALSE(instance->IsPrefabInstance());
        CHECK(document.CanEdit(instance->GetChildren()[0].get()));
        REQUIRE(document.Undo());
        instance = scene.FindObjectByID(id);
        REQUIRE(instance != nullptr);
        CHECK(instance->IsPrefabInstance());
        CHECK(instance->GetChildren().size() == 2);

        // Undo the second instance and the prefab creation: the original objects are back.
        REQUIRE(document.Undo());
        REQUIRE(document.Undo());
        GameObject *original = scene.FindObjectByID(id);
        REQUIRE(original != nullptr);
        CHECK_FALSE(original->IsPrefabInstance());
        REQUIRE(original->GetChildren().size() == 1);
        CHECK(original->GetChildren()[0]->GetName() == "Bulb");
        CHECK_FALSE(original->GetChildren()[0]->IsFromPrefab());
        CHECK(scene.GetObjects().size() == 1);
    }

    TEST_CASE("another scene's nodes are imported as one undo step")
    {
        RegisterBuiltinComponents();
        Scene model;
        auto *root = model.CreateObject("Model");
        auto *camera = model.CreateObject("Camera", root);
        camera->AddComponent(new CameraComponent());
        model.SetMainCamera(camera);
        const nlohmann::json modelDocument = SceneSerializer::SaveToJson(model);

        Scene scene;
        EditorDocument document(scene, nullptr);
        std::string error;
        REQUIRE_MESSAGE(document.ImportScene(modelDocument, &error), error);
        CHECK(CountAll(scene) == 2);
        REQUIRE(scene.GetMainCamera() != nullptr);
        CHECK(scene.GetMainCamera()->GetName() == "Camera");
        CHECK(document.GetSelection().size() == 1);

        // A second import adds to the scene and leaves its camera alone.
        GameObject *mainCamera = scene.GetMainCamera();
        REQUIRE(document.ImportScene(modelDocument, &error));
        CHECK(CountAll(scene) == 4);
        CHECK(scene.GetMainCamera() == mainCamera);
        document.Undo();
        document.Undo();
        CHECK(CountAll(scene) == 0);
        CHECK(scene.GetMainCamera() == nullptr);

        CHECK_FALSE(document.ImportScene(nlohmann::json{{"format", "something else"}}, &error));
    }
}

TEST_SUITE("editor world")
{
    // A 3 x 1 world of 100 m cells at x = -150: a persistent scene with a camera far from
    // the origin, cell 0 with two objects, cell 1 with one, cell 2 empty (no file).
    std::filesystem::path WriteWorld(const std::filesystem::path &assets)
    {
        const auto folder = assets / "worlds" / "strip";
        std::filesystem::create_directories(folder / "cells");
        std::ofstream(folder / "strip.gworld")
            << R"({"format":"geni.world","version":1,"cellSize":100,"origin":[-150,0,0],"cellsX":3,"cellsZ":1})";
        std::ofstream(
            folder / "persistent.gscene"
        ) << R"({"format":"geni.scene","version":1,"mainCamera":1,"nodes":[)"
          << R"({"name":"Sun","parent":-1,"components":[]},)"
          << R"({"name":"Camera","parent":-1,"position":[1000,20,5],"components":[{"type":"CameraComponent","properties":{}}]}]})";
        std::ofstream(folder / "cells" / "cell_0_0.gscene")
            << R"({"format":"geni.scene","version":1,"nodes":[)"
            << R"({"name":"House","parent":-1,"position":[10,0,50],"components":[]},)"
            << R"({"name":"Door","parent":0,"position":[0,1,1],"components":[]},)"
            << R"({"name":"Tree","parent":-1,"position":[90,0,50],"components":[]}]})";
        std::ofstream(
            folder / "cells" / "cell_1_0.gscene"
        ) << R"({"format":"geni.scene","version":1,"nodes":[{"name":"Shop","parent":-1,"position":[50,0,50],"components":[]}]})";
        return folder / "strip.gworld";
    }

    nlohmann::json ReadJson(const std::filesystem::path &path)
    {
        std::ifstream file(path);
        return nlohmann::json::parse(file);
    }

    TEST_CASE("a world opens with its persistent scene and loads a region of cells")
    {
        RegisterBuiltinComponents();
        ProjectFixture fixture("world_region");
        const auto worldFile = WriteWorld(fixture.root / "assets");
        Scene scene;
        EditorDocument document(scene, &fixture.assets);
        std::string error;
        REQUIRE_MESSAGE(document.OpenWorld(worldFile, &error), error);
        CHECK(document.IsWorldDocument());
        CHECK(document.GetRegionCells().empty());
        CHECK(scene.GetObjects().size() == 2);
        REQUIRE(scene.GetMainCamera() != nullptr);

        REQUIRE_MESSAGE(document.LoadRegion(1, 0, 0, 0, &error), error); // corners in any order
        REQUIRE(document.GetRegionCells().size() == 2);
        // The region's first corner is the scene origin: cell 0's corner is (-150, 0, 0).
        CHECK(scene.GetOrigin().x == doctest::Approx(-150.0));
        GameObject *cell0 = scene.FindObjectByID(document.GetRegionCells()[0].root);
        GameObject *cell1 = scene.FindObjectByID(document.GetRegionCells()[1].root);
        REQUIRE(cell0 != nullptr);
        REQUIRE(cell1 != nullptr);
        CHECK(cell0->GetChildren().size() == 2);
        CHECK(cell1->GetChildren().size() == 1);
        CHECK(Near(cell1->GetPosition(), glm::vec3(100.0f, 0.0f, 0.0f)));
        // The persistent camera keeps its world position.
        CHECK(scene.ToWorld(scene.GetMainCamera()->GetWorldPosition()).x == doctest::Approx(1000.0));
        CHECK(CellFileCount(document) == 2);

        // Cells are not objects you can edit: no delete, no move, no duplicate.
        CHECK_FALSE(document.CanEdit(cell0));
        document.Select(cell0->GetID());
        CHECK_FALSE(document.DeleteSelection());
        CHECK(document.DuplicateSelection().empty());
        CHECK(document.CanEdit(cell0->GetChildren().front().get()));

        // New objects go into the region's first cell.
        GameObject *lamp = document.CreateLight(LightType::Point);
        CHECK(lamp->GetParent() == cell0);
        CHECK(document.IsDirty());
        // With unsaved edits the region cannot change.
        CHECK_FALSE(document.LoadRegion(2, 0, 2, 0, &error));
        CHECK(error.find("save") != std::string::npos);
    }

    TEST_CASE("saving writes each cell and the persistent scene, moving objects into the cell they are in now")
    {
        RegisterBuiltinComponents();
        ProjectFixture fixture("world_save");
        const auto worldFile = WriteWorld(fixture.root / "assets");
        const auto cells = worldFile.parent_path() / "cells";
        Scene scene;
        EditorDocument document(scene, &fixture.assets);
        std::string error;
        REQUIRE(document.OpenWorld(worldFile, &error));
        REQUIRE(document.LoadRegion(0, 0, 2, 0, &error));

        // Move the tree (cell 0, x = 90) 30 m east: it is now in cell 1, at x = 20 there.
        GameObject *tree = scene.FindObject("Tree");
        REQUIRE(tree != nullptr);
        NodeState state = NodeState::Capture(*tree);
        state.position.x += 30.0f;
        REQUIRE(document.SetNodeState(*tree, state));
        // And the house 250 m east, into cell 2, which has no file yet.
        GameObject *house = scene.FindObject("House");
        state = NodeState::Capture(*house);
        state.position.x += 250.0f;
        REQUIRE(document.SetNodeState(*house, state));
        // Move the camera; the persistent scene is written in world coordinates.
        GameObject *camera = scene.GetMainCamera();
        state = NodeState::Capture(*camera);
        state.position.y = 30.0f;
        REQUIRE(document.SetNodeState(*camera, state));

        REQUIRE_MESSAGE(document.Save(&error), error);
        CHECK_FALSE(document.IsDirty());

        const auto cell0 = ReadJson(cells / "cell_0_0.gscene");
        CHECK(cell0["nodes"].empty());
        const auto cell1 = ReadJson(cells / "cell_1_0.gscene");
        REQUIRE(cell1["nodes"].size() == 2);
        CHECK(cell1["nodes"][1]["name"] == "Tree");
        CHECK(cell1["nodes"][1]["position"][0].get<double>() == doctest::Approx(20.0));
        // A new file for the house, its child with it, relative to cell 2's corner.
        REQUIRE(std::filesystem::exists(cells / "cell_2_0.gscene"));
        const auto cell2 = ReadJson(cells / "cell_2_0.gscene");
        REQUIRE(cell2["nodes"].size() == 2);
        CHECK(cell2["nodes"][0]["name"] == "House");
        CHECK(cell2["nodes"][0]["position"][0].get<double>() == doctest::Approx(60.0));
        CHECK(cell2["nodes"][1]["parent"] == 0);
        const auto persistent = ReadJson(worldFile.parent_path() / "persistent.gscene");
        CHECK(persistent["mainCamera"] == 1);
        CHECK(persistent["nodes"][1]["position"][0].get<double>() == doctest::Approx(1000.0));
        CHECK(persistent["nodes"][1]["position"][1].get<double>() == doctest::Approx(30.0));

        // The scene was rearranged to match: the tree is under cell 1 now.
        CHECK(tree->GetParent()->GetID() == document.GetRegionCells()[1].root);
        CHECK(scene.ToWorld(tree->GetWorldPosition()).x == doctest::Approx(-30.0));

        // Opening it again shows the same world.
        Scene other;
        EditorDocument reopened(other, &fixture.assets);
        REQUIRE(reopened.OpenWorld(worldFile, &error));
        REQUIRE(reopened.LoadRegion(0, 0, 2, 0, &error));
        CHECK(other.FindObjectByID(reopened.GetRegionCells()[2].root)->GetChildren().size() == 1);
        GameObject *treeAgain = other.FindObject("Tree");
        REQUIRE(treeAgain != nullptr);
        CHECK(other.ToWorld(treeAgain->GetWorldPosition()).x == doctest::Approx(-30.0));
        CHECK(other.ToWorld(other.GetMainCamera()->GetWorldPosition()).y == doctest::Approx(30.0));

        // A saved world cannot be saved somewhere else, and play keeps the region origin.
        CHECK_FALSE(reopened.SaveAs(fixture.root / "assets" / "elsewhere.gscene", &error));
        const glm::vec3 before = treeAgain->GetPosition();
        REQUIRE(reopened.Play());
        REQUIRE(reopened.Stop());
        CHECK(other.GetOrigin().x == doctest::Approx(-150.0));
        GameObject *treeAfterPlay = other.FindObject("Tree");
        REQUIRE(treeAfterPlay != nullptr);
        CHECK(Near(treeAfterPlay->GetPosition(), before));
        CHECK(reopened.IsCellRoot(treeAfterPlay->GetParent()->GetID()));
    }

    TEST_CASE("a cell that cannot be read is never overwritten")
    {
        RegisterBuiltinComponents();
        ProjectFixture fixture("world_broken");
        const auto worldFile = WriteWorld(fixture.root / "assets");
        const auto broken = worldFile.parent_path() / "cells" / "cell_1_0.gscene";
        std::ofstream(broken) << "{ not json";
        Scene scene;
        EditorDocument document(scene, &fixture.assets);
        std::string error;
        REQUIRE(document.OpenWorld(worldFile, &error));
        REQUIRE(document.LoadRegion(0, 0, 1, 0, &error));
        CHECK_FALSE(document.GetRegionCells()[1].loaded);
        document.CreateEmpty("new");
        REQUIRE(document.Save(&error));
        std::ifstream file(broken);
        const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        CHECK(text == "{ not json");
    }
}

TEST_SUITE("gizmo")
{
    // A camera on +Z looking at the origin; rays are built by hand.
    Ray Towards(const glm::vec3 &from, const glm::vec3 &target)
    {
        return Ray{from, glm::normalize(target - from)};
    }
    const glm::vec3 kEye(0.0f, 0.0f, 10.0f);
    const glm::vec3 kForward(0.0f, 0.0f, -1.0f);

    TEST_CASE("handles are hit where they are drawn")
    {
        Gizmo gizmo;
        GizmoFrame frame; // at the origin, world axes, size 1
        gizmo.mode = GizmoMode::Translate;
        CHECK(gizmo.HitTest(Towards(kEye, glm::vec3(0.7f, 0, 0)), frame) == GizmoHandle::X);
        CHECK(gizmo.HitTest(Towards(kEye, glm::vec3(0, 0.7f, 0)), frame) == GizmoHandle::Y);
        CHECK(gizmo.HitTest(Towards(kEye, glm::vec3(0.35f, 0.35f, 0)), frame) == GizmoHandle::XY);
        CHECK(gizmo.HitTest(Towards(kEye, glm::vec3(2.0f, 2.0f, 0)), frame) == GizmoHandle::None);
        CHECK(gizmo.HitTest(Towards(kEye, glm::vec3(1.5f, 0, 0)), frame) == GizmoHandle::None); // past the tip

        gizmo.mode = GizmoMode::Rotate;
        CHECK(gizmo.HitTest(Towards(kEye, glm::vec3(1.0f, 0, 0)), frame) == GizmoHandle::Z); // the ring facing the camera
        CHECK(gizmo.HitTest(Towards(kEye, glm::vec3(0.4f, 0.1f, 0)), frame) == GizmoHandle::None);

        gizmo.mode = GizmoMode::Scale;
        CHECK(gizmo.HitTest(Towards(kEye, glm::vec3(0)), frame) == GizmoHandle::Uniform);
        CHECK(gizmo.HitTest(Towards(kEye, glm::vec3(0.7f, 0, 0)), frame) == GizmoHandle::X);

        // A bigger gizmo is hit further out; a rotated one along its own axes.
        gizmo.mode = GizmoMode::Translate;
        frame.size = 4.0f;
        CHECK(gizmo.HitTest(Towards(kEye, glm::vec3(3.0f, 0, 0)), frame) == GizmoHandle::X);
        frame.rotation = glm::angleAxis(glm::radians(90.0f), glm::vec3(0, 0, 1)); // local X now points up
        CHECK(gizmo.HitTest(Towards(kEye, glm::vec3(0, 3.0f, 0)), frame) == GizmoHandle::X);
    }

    TEST_CASE("dragging an axis moves along it by what the cursor moved")
    {
        Gizmo gizmo;
        GizmoFrame frame;
        REQUIRE(gizmo.BeginDrag(GizmoHandle::X, Towards(kEye, glm::vec3(0.5f, 0, 0)), frame, kForward));
        CHECK(gizmo.IsDragging());
        GizmoDelta delta = gizmo.Drag(Towards(kEye, glm::vec3(3.5f, 0, 0)));
        CHECK(Near(delta.translation, glm::vec3(3.0f, 0, 0)));
        // A cursor that wanders off the axis still moves the object along X only, to the
        // point of the axis nearest the cursor's ray.
        delta = gizmo.Drag(Towards(kEye, glm::vec3(3.5f, 0.8f, 0)));
        CHECK(delta.translation.x == doctest::Approx(3.0f).epsilon(0.02));
        CHECK(delta.translation.y == 0.0f);
        CHECK(delta.translation.z == 0.0f);

        gizmo.snap.enabled = true;
        gizmo.snap.translate = 0.5f;
        delta = gizmo.Drag(Towards(kEye, glm::vec3(1.8f, 0, 0)));
        CHECK(Near(delta.translation, glm::vec3(1.5f, 0, 0)));
        gizmo.EndDrag();
        CHECK_FALSE(gizmo.IsDragging());
        CHECK(Near(gizmo.Drag(Towards(kEye, glm::vec3(5, 0, 0))).translation, glm::vec3(0)));

        // Looking straight down an axis gives nothing to drag along.
        CHECK_FALSE(gizmo.BeginDrag(GizmoHandle::Z, Ray{kEye, kForward}, frame, kForward));
    }

    TEST_CASE("plane, rotate and scale drags")
    {
        Gizmo gizmo;
        GizmoFrame frame;
        REQUIRE(gizmo.BeginDrag(GizmoHandle::XY, Towards(kEye, glm::vec3(0.3f, 0.3f, 0)), frame, kForward));
        CHECK(Near(gizmo.Drag(Towards(kEye, glm::vec3(2.3f, -0.7f, 0))).translation, glm::vec3(2.0f, -1.0f, 0)));
        gizmo.EndDrag();

        // A quarter turn around Z, counter-clockwise seen from +Z.
        gizmo.mode = GizmoMode::Rotate;
        REQUIRE(gizmo.BeginDrag(GizmoHandle::Z, Towards(kEye, glm::vec3(1, 0, 0)), frame, kForward));
        GizmoDelta delta = gizmo.Drag(Towards(kEye, glm::vec3(0, 1, 0)));
        CHECK(Near(delta.rotation * glm::vec3(1, 0, 0), glm::vec3(0, 1, 0)));
        gizmo.snap.enabled = true;
        gizmo.snap.rotateDegrees = 45.0f;
        delta = gizmo.Drag(Towards(kEye, glm::vec3(1, 0.9f, 0))); // about 42 degrees
        CHECK(glm::degrees(glm::angle(delta.rotation)) == doctest::Approx(45.0f).epsilon(0.001));
        gizmo.snap.enabled = false;
        gizmo.EndDrag();

        gizmo.mode = GizmoMode::Scale;
        REQUIRE(gizmo.BeginDrag(GizmoHandle::Y, Towards(kEye, glm::vec3(0, 0.5f, 0)), frame, kForward));
        delta = gizmo.Drag(Towards(kEye, glm::vec3(0, 1.5f, 0)));
        CHECK(Near(delta.scale, glm::vec3(1, 3, 1), 0.02f));
        // Dragged through the centre: the scale stops just above zero instead of flipping.
        CHECK(gizmo.Drag(Towards(kEye, glm::vec3(0, -1.0f, 0))).scale.y == doctest::Approx(0.01f));
        gizmo.EndDrag();

        REQUIRE(gizmo.BeginDrag(GizmoHandle::Uniform, Towards(kEye, glm::vec3(0.5f, 0, 0)), frame, kForward));
        delta = gizmo.Drag(Towards(kEye, glm::vec3(1.0f, 0, 0)));
        CHECK(Near(delta.scale, glm::vec3(2.0f), 0.02f));
    }

    TEST_CASE("a delta is applied in world space to children of transformed parents")
    {
        Scene scene;
        auto *parent = scene.CreateObject("parent");
        parent->SetPosition(glm::vec3(5, 0, 0));
        parent->SetRotation(glm::angleAxis(glm::radians(90.0f), glm::vec3(0, 1, 0)));
        parent->SetScale(glm::vec3(2.0f));
        auto *child = scene.CreateObject("child", parent);
        child->SetPosition(glm::vec3(1, 0, 0));
        auto *other = scene.CreateObject("other");
        other->SetPosition(glm::vec3(0, 0, 4));
        const glm::vec3 childStart = child->GetWorldPosition();

        GizmoTargets targets;
        targets.Capture({child, other});
        GizmoDelta move;
        move.translation = glm::vec3(0, 3, 0);
        targets.Apply(scene, GizmoMode::Translate, move, childStart);
        CHECK(Near(child->GetWorldPosition(), childStart + glm::vec3(0, 3, 0)));
        CHECK(Near(other->GetWorldPosition(), glm::vec3(0, 3, 4)));

        // Applied to the start state every time, not on top of the last frame.
        targets.Apply(scene, GizmoMode::Translate, move, childStart);
        CHECK(Near(child->GetWorldPosition(), childStart + glm::vec3(0, 3, 0)));

        // Rotation turns every object around the pivot.
        targets.Revert(scene);
        GizmoDelta turn;
        turn.rotation = glm::angleAxis(glm::radians(180.0f), glm::vec3(0, 1, 0));
        const glm::vec3 pivot(0.0f);
        targets.Apply(scene, GizmoMode::Rotate, turn, pivot);
        CHECK(Near(other->GetWorldPosition(), glm::vec3(0, 0, -4)));
        CHECK(Near(child->GetWorldPosition(), glm::vec3(-childStart.x, childStart.y, -childStart.z)));
        CHECK(Near(other->GetWorldRotation() * glm::vec3(1, 0, 0), glm::vec3(-1, 0, 0)));

        targets.Revert(scene);
        CHECK(Near(child->GetPosition(), glm::vec3(1, 0, 0)));
        GizmoDelta grow;
        grow.scale = glm::vec3(1, 2, 1);
        targets.Apply(scene, GizmoMode::Scale, grow, pivot);
        CHECK(Near(other->GetScale(), glm::vec3(1, 2, 1)));
        CHECK(targets.GetStartStates().size() == 2);
    }
}

TEST_SUITE("editor camera")
{
    TEST_CASE("the camera looks, flies, orbits, pans and dollies")
    {
        EditorCamera camera;
        camera.SetPosition(glm::vec3(0, 0, 10));
        camera.SetAngles(0.0f, 0.0f);
        CHECK(Near(camera.GetForward(), glm::vec3(0, 0, -1)));
        CHECK(Near(camera.GetRight(), glm::vec3(1, 0, 0)));

        // The view matrix puts a point in front of the camera on -Z.
        const glm::vec4 inView = camera.GetViewMatrix() * glm::vec4(0, 0, 0, 1);
        CHECK(Near(glm::vec3(inView), glm::vec3(0, 0, -10)));

        CameraInput fly;
        fly.look = true;
        fly.move = glm::vec3(0, 0, 1);
        camera.Update(fly, 0.5f);
        CHECK(Near(camera.GetPosition(), glm::vec3(0, 0, 10 - camera.moveSpeed * 0.5f)));

        // Mouse right turns right (towards +X); pitch stops short of straight up.
        CameraInput look;
        look.look = true;
        look.mouseDelta = glm::vec2(100, 0);
        camera.Update(look, 0.0f);
        CHECK(camera.GetForward().x > 0.1f);
        look.mouseDelta = glm::vec2(0, -100000);
        camera.Update(look, 0.0f);
        CHECK(camera.GetPitch() < 90.0f);
        CHECK(camera.GetForward().y < 1.0f);

        // Orbit keeps the pivot fixed; focus puts a sphere in view at a sensible distance.
        camera.SetAngles(0.0f, 0.0f);
        camera.Focus(glm::vec3(1, 2, 3), 2.0f);
        CHECK(Near(camera.GetPivot(), glm::vec3(1, 2, 3)));
        const float distance = glm::length(camera.GetPosition() - glm::vec3(1, 2, 3));
        CHECK(distance > 2.0f);
        CameraInput orbit;
        orbit.orbit = true;
        orbit.mouseDelta = glm::vec2(250, 40);
        camera.Update(orbit, 0.0f);
        CHECK(Near(camera.GetPivot(), glm::vec3(1, 2, 3)));
        CHECK(glm::length(camera.GetPosition() - glm::vec3(1, 2, 3)) == doctest::Approx(distance));

        CameraInput dolly;
        dolly.scroll = 1.0f;
        camera.Update(dolly, 0.0f);
        CHECK(glm::length(camera.GetPosition() - glm::vec3(1, 2, 3)) < distance);
        CHECK(Near(camera.GetPivot(), glm::vec3(1, 2, 3)));

        const glm::vec3 beforePan = camera.GetPosition();
        CameraInput pan;
        pan.pan = true;
        pan.mouseDelta = glm::vec2(10, 0);
        camera.Update(pan, 0.0f);
        CHECK(glm::dot(camera.GetPosition() - beforePan, camera.GetRight()) < 0.0f); // the scene follows the hand

        const ViewOverride view = camera.GetViewOverride();
        CHECK(Near(view.position, camera.GetPosition()));
        CHECK(view.fieldOfViewDegrees == doctest::Approx(camera.fieldOfViewDegrees));
    }
}

TEST_SUITE("editor project")
{
    TEST_CASE("asset paths are relative to the assets folder")
    {
        ProjectFixture fixture("paths");
        std::filesystem::create_directories(fixture.root / "assets" / "scenes");
        std::filesystem::create_directories(fixture.root / "assets" / "cooked");
        std::ofstream(fixture.root / "assets" / "scenes" / "b.gscene") << "{}";
        std::ofstream(fixture.root / "assets" / "a.gscene") << "{}";
        std::ofstream(fixture.root / "assets" / "cooked" / "c.gscene") << "{}";

        ProjectConfig config;
        config.assets = fixture.root / "assets";
        config.cooked = fixture.root / "cooked";
        Project project;
        project.Init(config, fixture.fileSystem, &fixture.assets, nullptr);

        CHECK(project.ToAssetPath(fixture.root / "assets" / "scenes" / "b.gscene") == "scenes/b.gscene");
        CHECK(project.ToAssetPath(fixture.root / "elsewhere.gscene").empty());
        CHECK(project.ToAbsolute("scenes/b.gscene") == fixture.root / "assets" / "scenes" / "b.gscene");
        const auto scenes = project.ListAssets(".gscene");
        REQUIRE(scenes.size() == 2); // the cooked folder is not a source
        CHECK(scenes[0] == "a.gscene");
        CHECK(scenes[1] == "scenes/b.gscene");

        // Without tools the project says so instead of pretending.
        std::string output;
        CHECK_FALSE(project.Cook(&output));
        CHECK_FALSE(output.empty());
        CHECK_FALSE(project.CompileShader("x.slang", &output));
    }

    TEST_CASE("a prefab edited on disk is reported so its instances can be rebuilt")
    {
        RegisterBuiltinComponents();
        ProjectFixture fixture("hot_reload");
        ProjectConfig config;
        config.assets = fixture.root / "assets";
        config.cooked = fixture.root / "cooked";
        Project project;
        project.Init(config, fixture.fileSystem, &fixture.assets, nullptr);
        const auto path = config.assets / "lamp.gprefab";

        Scene source;
        auto *lamp = source.CreateObject("Lamp");
        auto *light = new LightComponent();
        lamp->AddComponent(light);
        REQUIRE(SceneSerializer::SavePrefab(*lamp, path));
        const AssetID prefab = fixture.assets.RegisterSourceAsset(AssetType::Prefab, "lamp.gprefab");

        Scene scene;
        EditorDocument document(scene, &fixture.assets);
        GameObject *instance = document.InstantiatePrefab(prefab);
        REQUIRE(instance != nullptr);
        ReloadReport report;
        project.UpdateNow(report); // takes in the new file; nothing loaded has changed yet
        CHECK(report.prefabs.empty());
        CHECK_FALSE(project.UpdateNow(report)); // and now nothing is new

        // Someone edits the prefab file outside the editor.
        light->SetIntensity(6.0f);
        REQUIRE(SceneSerializer::SavePrefab(*lamp, path));
        std::filesystem::last_write_time(path, std::filesystem::last_write_time(path) + std::chrono::seconds(2));

        REQUIRE(project.UpdateNow(report));
        CHECK_FALSE(report.failed);
        REQUIRE(report.prefabs.size() == 1);
        CHECK(report.prefabs[0] == prefab);
        CHECK(document.RefreshPrefabInstances(prefab) == 1);
        CHECK(instance->GetComponent<LightComponent>()->GetIntensity() == doctest::Approx(6.0f));
    }

    TEST_CASE("a shader is compiled to SPIR-V, and a broken one leaves the old SPIR-V in place")
    {
        ProjectFixture fixture("shaders");
        ProjectConfig config;
        config.assets = fixture.root / "assets";
        config.cooked = fixture.root / "cooked";
        config.slangc = GENI_TEST_SLANGC;
        config.shaderModules = GENI_TEST_SHADER_MODULES;
        Project project;
        project.Init(config, fixture.fileSystem, &fixture.assets, nullptr);

        // A real shader that imports the engine's modules, and a module of the project's own.
        std::filesystem::create_directories(config.assets / "shaders");
        std::filesystem::copy_file(GENI_TEST_SHADER_SOURCE, config.assets / "shaders" / "lit.slang");
        std::ofstream(config.assets / "shaders" / "tint.slang") << "float3 Tint(float3 c) { return c * 0.5; }\n";

        const auto vertex = config.cooked / "shaders" / "lit.slang.vert.spv";
        const auto fragment = config.cooked / "shaders" / "lit.slang.frag.spv";
        std::string output;
        REQUIRE_MESSAGE(project.Prepare(&output), output); // compiles what has no SPIR-V yet
        REQUIRE(std::filesystem::exists(vertex));
        REQUIRE(std::filesystem::exists(fragment));
        CHECK(std::filesystem::file_size(vertex) > 100);
        // A module has no entry points, so it compiles to nothing.
        CHECK_FALSE(std::filesystem::exists(config.cooked / "shaders" / "tint.slang.vert.spv"));

        // A syntax error: the compiler's message comes back, the good SPIR-V stays.
        const auto size = std::filesystem::file_size(fragment);
        {
            std::ofstream broken(config.assets / "shaders" / "lit.slang", std::ios::app);
            broken << "\nthis is not slang;\n";
        }
        CHECK_FALSE(project.CompileShader("shaders/lit.slang", &output));
        CHECK(output.find("lit.slang") != std::string::npos);
        CHECK(std::filesystem::file_size(fragment) == size);
        CHECK_FALSE(std::filesystem::exists(fragment.string() + ".tmp"));

        // The watcher sees the edit and reports the failure instead of hiding it.
        std::filesystem::last_write_time(
            config.assets / "shaders" / "lit.slang",
            std::filesystem::last_write_time(config.assets / "shaders" / "lit.slang") + std::chrono::seconds(2)
        );
        ReloadReport report;
        REQUIRE(project.UpdateNow(report));
        CHECK(report.failed);
        REQUIRE_FALSE(report.messages.empty());
        CHECK(report.messages[0].find("did not compile") != std::string::npos);
    }

    TEST_CASE("a child process's exit code and output come back")
    {
        // CMake is certainly installed where these tests are built.
        std::string output;
        CHECK(RunProcess(GENI_TEST_CMAKE_COMMAND, {"-E", "echo", "hello from a child"}, &output) == 0);
        CHECK(output.find("hello from a child") != std::string::npos);
        CHECK(RunProcess(GENI_TEST_CMAKE_COMMAND, {"-E", "no_such_command"}, &output) != 0);
        CHECK(RunProcess("/no/such/program/anywhere", {}, &output) != 0);
    }
}
