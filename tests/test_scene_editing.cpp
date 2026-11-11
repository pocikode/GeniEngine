// Engine support for editing a scene (ROADMAP M4, docs/design/0006): object IDs,
// structural edits, edit-mode update, subtrees, prefabs, picking and the file watcher.
// None of these need a window or a GPU.

#include "TestEngine.h"
#include "assets/AssetManager.h"
#include "io/FileSystem.h"
#include "io/FileWatcher.h"
#include "scene/BuiltinComponents.h"
#include "scene/GameObject.h"
#include "scene/Picking.h"
#include "scene/Reflection.h"
#include "scene/Scene.h"
#include "scene/SceneSerializer.h"
#include "scene/components/CameraComponent.h"
#include "scene/components/LightComponent.h"
#include <chrono>
#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <vector>

namespace
{

// Counts how it is driven, to tell the two update modes apart.
class ProbeComponent : public Geni::Component
{
    COMPONENT(ProbeComponent)

  public:
    void Update(float) override
    {
        ++updates;
    }
    void EditUpdate() override
    {
        ++editUpdates;
    }
    int updates = 0;
    int editUpdates = 0;
};

size_t CountObjects(const Geni::GameObject &object)
{
    size_t count = 1;
    for (const auto &child : object.GetChildren())
    {
        count += CountObjects(*child);
    }
    return count;
}

Geni::LightComponent *AddLight(Geni::GameObject *object, float intensity)
{
    auto *light = new Geni::LightComponent();
    light->SetIntensity(intensity);
    object->AddComponent(light);
    return light;
}

// A scratch assets folder with an AssetManager that reads from it, without an engine.
struct PrefabFixture
{
    std::filesystem::path root;
    Geni::FileSystem fileSystem;
    Geni::AssetManager assets;

    explicit PrefabFixture(const char *name)
    {
        root = std::filesystem::temp_directory_path() / "geni_tests" / name;
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root);
        fileSystem.SetAssetsFolder(root);
        fileSystem.SetCookedFolder(root);
        assets.Init(&fileSystem, nullptr, nullptr);
    }
    ~PrefabFixture()
    {
        assets.Shutdown();
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
    Geni::SerializationContext Context(Geni::Scene &scene)
    {
        return Geni::SerializationContext{&scene, &assets};
    }
};

Geni::CameraData LookDownMinusZ(float aspect = 1.0f)
{
    Geni::CameraData camera;
    camera.viewMatrix = glm::lookAt(glm::vec3(0, 0, 10), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
    camera.projectionMatrix = glm::perspective(glm::radians(60.0f), aspect, 0.1f, 100.0f);
    camera.position = glm::vec3(0, 0, 10);
    return camera;
}

} // namespace

TEST_SUITE("scene editing")
{
    TEST_CASE("every object has an ID that finds it and dies with it")
    {
        Geni::Scene scene;
        auto *a = scene.CreateObject("a");
        auto *b = scene.CreateObject("b", a);
        CHECK(a->GetID() != 0);
        CHECK(a->GetID() != b->GetID());
        CHECK(scene.FindObjectByID(a->GetID()) == a);
        CHECK(scene.FindObjectByID(b->GetID()) == b);
        CHECK(scene.FindObjectByID(0) == nullptr);

        const uint64_t idA = a->GetID();
        const uint64_t idB = b->GetID();
        scene.DestroyObject(a); // takes the child with it
        CHECK(scene.FindObjectByID(idA) == nullptr);
        CHECK(scene.FindObjectByID(idB) == nullptr);
        CHECK(scene.GetObjects().empty());

        // IDs are not reused while the scene lives, and a freed one can be given back.
        auto *c = scene.CreateObject("c");
        CHECK(c->GetID() != idA);
        CHECK(scene.SetObjectID(c, idA));
        CHECK(scene.FindObjectByID(idA) == c);
        auto *d = scene.CreateObject("d");
        CHECK_FALSE(scene.SetObjectID(d, idA));
        CHECK(d->GetID() != idA);
    }

    TEST_CASE("destroying the main camera clears it")
    {
        Geni::Scene scene;
        auto *camera = scene.CreateObject("camera");
        scene.SetMainCamera(camera);
        scene.DestroyObject(camera);
        CHECK(scene.GetMainCamera() == nullptr);
    }

    TEST_CASE("objects move to a parent and a position among its children")
    {
        Geni::Scene scene;
        auto *parent = scene.CreateObject("parent");
        auto *first = scene.CreateObject("first", parent);
        auto *second = scene.CreateObject("second", parent);
        auto *loose = scene.CreateObject("loose");

        REQUIRE(scene.MoveObject(loose, parent, 1));
        REQUIRE(parent->GetChildren().size() == 3);
        CHECK(parent->GetChildren()[0].get() == first);
        CHECK(parent->GetChildren()[1].get() == loose);
        CHECK(parent->GetChildren()[2].get() == second);
        CHECK(loose->GetSiblingIndex() == 1);
        CHECK(loose->GetParent() == parent);

        // Reordering within the same parent, and back out to the root at the front.
        REQUIRE(scene.MoveObject(first, parent, -1));
        CHECK(first->GetSiblingIndex() == 2);
        REQUIRE(scene.MoveObject(loose, nullptr, 0));
        CHECK(loose->GetParent() == nullptr);
        CHECK(scene.GetObjects()[0].get() == loose);

        // An object cannot become its own descendant.
        CHECK_FALSE(scene.MoveObject(parent, first, 0));
        CHECK(parent->GetParent() == nullptr);
    }

    TEST_CASE("components are removed and put back at an index")
    {
        Geni::Scene scene;
        auto *object = scene.CreateObject("object");
        auto *first = AddLight(object, 1.0f);
        auto *second = AddLight(object, 2.0f);
        CHECK(scene.CountComponents<Geni::LightComponent>() == 2);

        CHECK(object->RemoveComponent(first));
        CHECK(scene.CountComponents<Geni::LightComponent>() == 1);
        CHECK(object->GetComponents().size() == 1);
        CHECK_FALSE(object->RemoveComponent(first)); // already gone

        auto *restored = new Geni::LightComponent();
        object->AddComponent(restored, 0);
        REQUIRE(object->GetComponents().size() == 2);
        CHECK(object->GetComponents()[0].get() == restored);
        CHECK(object->GetComponents()[1].get() == second);
    }

    TEST_CASE("edit mode runs EditUpdate and nothing else")
    {
        Geni::Scene scene;
        auto *object = scene.CreateObject("object");
        auto *child = scene.CreateObject("child", object);
        auto *probe = new ProbeComponent();
        auto *childProbe = new ProbeComponent();
        object->AddComponent(probe);
        child->AddComponent(childProbe);

        scene.Update(0.016f, Geni::SceneUpdateMode::Edit);
        CHECK(probe->updates == 0);
        CHECK(probe->editUpdates == 1);
        CHECK(childProbe->editUpdates == 1);

        scene.Update(0.016f);
        CHECK(probe->updates == 1);
        CHECK(probe->editUpdates == 1);

        // Inactive objects are skipped in both modes, and deferred destruction still works.
        object->SetActive(false);
        scene.Update(0.016f, Geni::SceneUpdateMode::Edit);
        CHECK(probe->editUpdates == 1);
        object->SetActive(true);
        child->MarkForDestroy();
        scene.Update(0.016f, Geni::SceneUpdateMode::Edit);
        CHECK(object->GetChildren().empty());
    }

    TEST_CASE("a subtree is saved and instantiated under another parent")
    {
        Geni::RegisterBuiltinComponents();
        Geni::Scene scene;
        auto *root = scene.CreateObject("root");
        root->SetPosition(glm::vec3(1, 2, 3));
        auto *child = scene.CreateObject("child", root);
        AddLight(child, 4.0f);
        auto *other = scene.CreateObject("other");
        auto *camera = scene.CreateObject("camera");
        scene.SetMainCamera(camera);

        const nlohmann::json document = Geni::SceneSerializer::SaveSubtree({root});
        CHECK(document["nodes"].size() == 2);
        CHECK_FALSE(document["nodes"][0].contains("id"));

        Geni::SerializationContext context{&scene, nullptr};
        std::vector<Geni::GameObject *> roots;
        REQUIRE(Geni::SceneSerializer::InstantiateSubtree(scene, document, context, other, &roots));
        REQUIRE(roots.size() == 1);
        CHECK(roots[0] != root);
        CHECK(roots[0]->GetParent() == other);
        CHECK(roots[0]->GetName() == "root");
        CHECK(GeniTest::Near(roots[0]->GetPosition(), glm::vec3(1, 2, 3)));
        REQUIRE(roots[0]->GetChildren().size() == 1);
        auto *light = roots[0]->GetChildren()[0]->GetComponent<Geni::LightComponent>();
        REQUIRE(light != nullptr);
        CHECK(light->GetIntensity() == doctest::Approx(4.0f));
        // The rest of the scene is as it was.
        CHECK(scene.GetObjects().size() == 3);
        CHECK(scene.GetMainCamera() == camera);
    }

    TEST_CASE("a subtree saved with IDs comes back with the same IDs")
    {
        Geni::Scene scene;
        auto *root = scene.CreateObject("root");
        auto *child = scene.CreateObject("child", root);
        const uint64_t rootID = root->GetID();
        const uint64_t childID = child->GetID();

        const nlohmann::json document = Geni::SceneSerializer::SaveSubtree({root}, true);
        scene.DestroyObject(root);
        REQUIRE(scene.FindObjectByID(rootID) == nullptr);

        Geni::SerializationContext context{&scene, nullptr};
        REQUIRE(Geni::SceneSerializer::InstantiateSubtree(scene, document, context, nullptr, nullptr, nullptr, true));
        REQUIRE(scene.FindObjectByID(rootID) != nullptr);
        REQUIRE(scene.FindObjectByID(childID) != nullptr);
        CHECK(scene.FindObjectByID(childID)->GetParent() == scene.FindObjectByID(rootID));
        // New objects do not collide with the restored IDs.
        CHECK(scene.CreateObject("new")->GetID() > childID);
    }

    TEST_CASE("a malformed subtree leaves nothing behind")
    {
        Geni::Scene scene;
        scene.CreateObject("existing");
        nlohmann::json document = {
            {"format", "geni.scene"},
            {"version", 1},
            {"nodes", nlohmann::json::array({{{"name", "ok"}, {"parent", -1}}, {{"name", "bad"}, {"parent", -1}, {"position", "not a vector"}, {"scale", {1, "x", 1}}}})}
        };
        Geni::SerializationContext context{&scene, nullptr};
        std::string error;
        CHECK_FALSE(Geni::SceneSerializer::InstantiateSubtree(scene, document, context, nullptr, nullptr, &error));
        CHECK_FALSE(error.empty());
        REQUIRE(scene.GetObjects().size() == 1);
        CHECK(scene.GetObjects()[0]->GetName() == "existing");
    }
}

TEST_SUITE("prefabs")
{
    TEST_CASE("a prefab is saved, instantiated and not written back into the scene")
    {
        Geni::RegisterBuiltinComponents();
        PrefabFixture fixture("prefab_roundtrip");

        // The prefab: a lamp with a light on the root and a bulb child.
        Geni::Scene source;
        auto *lamp = source.CreateObject("Lamp");
        AddLight(lamp, 3.0f);
        auto *bulb = source.CreateObject("Bulb", lamp);
        bulb->SetPosition(glm::vec3(0, 2, 0));
        std::string error;
        REQUIRE_MESSAGE(Geni::SceneSerializer::SavePrefab(*lamp, fixture.root / "prefabs" / "lamp.gprefab", &error), error);
        const Geni::AssetID prefab = fixture.assets.RegisterSourceAsset(Geni::AssetType::Prefab, "prefabs/lamp.gprefab");
        CHECK(prefab == Geni::AssetID::FromName("prefab:prefabs/lamp.gprefab"));

        Geni::Scene scene;
        auto context = fixture.Context(scene);
        Geni::GameObject *instance = Geni::SceneSerializer::InstantiatePrefab(scene, prefab, context);
        REQUIRE(instance != nullptr);
        CHECK(instance->GetName() == "Lamp");
        CHECK(instance->IsPrefabInstance());
        CHECK_FALSE(instance->IsFromPrefab());
        REQUIRE(instance->GetComponent<Geni::LightComponent>() != nullptr);
        CHECK(instance->GetComponent<Geni::LightComponent>()->GetIntensity() == doctest::Approx(3.0f));
        REQUIRE(instance->GetChildren().size() == 1);
        CHECK(instance->GetChildren()[0]->GetName() == "Bulb");
        CHECK(instance->GetChildren()[0]->IsFromPrefab());

        // The instance has its own place; a child added in the scene belongs to the scene.
        instance->SetPosition(glm::vec3(5, 0, 0));
        instance->SetName("Lamp A");
        scene.CreateObject("Sticker", instance);

        const nlohmann::json saved = Geni::SceneSerializer::SaveToJson(scene);
        REQUIRE(saved["nodes"].size() == 2); // the instance and the sticker, not the bulb
        CHECK(saved["nodes"][0]["prefab"] == prefab.ToHex());
        CHECK(saved["nodes"][0]["components"].empty());
        CHECK(saved["nodes"][1]["name"] == "Sticker");

        // Loading the scene builds the instance from the prefab again.
        Geni::Scene loaded;
        auto loadedContext = fixture.Context(loaded);
        REQUIRE_MESSAGE(Geni::SceneSerializer::Load(loaded, saved, loadedContext, &error), error);
        REQUIRE(loaded.GetObjects().size() == 1);
        Geni::GameObject *again = loaded.GetObjects()[0].get();
        CHECK(again->GetName() == "Lamp A");
        CHECK(GeniTest::Near(again->GetPosition(), glm::vec3(5, 0, 0)));
        CHECK(again->GetComponent<Geni::LightComponent>() != nullptr);
        CHECK(CountObjects(*again) == 3);
        CHECK(loaded.FindObject("Bulb") != nullptr);
        CHECK(loaded.FindObject("Sticker") != nullptr);
    }

    TEST_CASE("editing the prefab file changes its instances")
    {
        Geni::RegisterBuiltinComponents();
        PrefabFixture fixture("prefab_refresh");
        const auto path = fixture.root / "lamp.gprefab";

        Geni::Scene source;
        auto *lamp = source.CreateObject("Lamp");
        auto *light = AddLight(lamp, 1.0f);
        source.CreateObject("Bulb", lamp);
        REQUIRE(Geni::SceneSerializer::SavePrefab(*lamp, path));
        const Geni::AssetID prefab = fixture.assets.RegisterSourceAsset(Geni::AssetType::Prefab, "lamp.gprefab");

        Geni::Scene scene;
        auto context = fixture.Context(scene);
        Geni::GameObject *instance = Geni::SceneSerializer::InstantiatePrefab(scene, prefab, context);
        REQUIRE(instance != nullptr);
        auto *kept = scene.CreateObject("Kept", instance);

        // The prefab gets a brighter light and a second child.
        light->SetIntensity(9.0f);
        source.CreateObject("Shade", lamp);
        REQUIRE(Geni::SceneSerializer::SavePrefab(*lamp, path));
        REQUIRE(fixture.assets.Reload(prefab));
        REQUIRE(Geni::SceneSerializer::RefreshPrefabInstance(*instance, context));

        REQUIRE(instance->GetComponents().size() == 1);
        CHECK(instance->GetComponent<Geni::LightComponent>()->GetIntensity() == doctest::Approx(9.0f));
        CHECK(instance->GetChildren().size() == 3); // Kept, Bulb, Shade
        CHECK(scene.FindObject("Shade") != nullptr);
        CHECK(scene.FindObject("Kept") == kept);
        CHECK(scene.CountComponents<Geni::LightComponent>() == 1);
    }

    TEST_CASE("an unpacked instance is ordinary objects")
    {
        Geni::RegisterBuiltinComponents();
        PrefabFixture fixture("prefab_unpack");
        Geni::Scene source;
        auto *lamp = source.CreateObject("Lamp");
        AddLight(lamp, 2.0f);
        source.CreateObject("Bulb", lamp);
        REQUIRE(Geni::SceneSerializer::SavePrefab(*lamp, fixture.root / "lamp.gprefab"));
        const Geni::AssetID prefab = fixture.assets.RegisterSourceAsset(Geni::AssetType::Prefab, "lamp.gprefab");

        Geni::Scene scene;
        auto context = fixture.Context(scene);
        Geni::GameObject *instance = Geni::SceneSerializer::InstantiatePrefab(scene, prefab, context);
        REQUIRE(instance != nullptr);
        Geni::SceneSerializer::UnpackPrefabInstance(*instance);
        CHECK_FALSE(instance->IsPrefabInstance());
        CHECK_FALSE(instance->GetChildren()[0]->IsFromPrefab());

        const nlohmann::json saved = Geni::SceneSerializer::SaveToJson(scene);
        REQUIRE(saved["nodes"].size() == 2);
        CHECK_FALSE(saved["nodes"][0].contains("prefab"));
        CHECK(saved["nodes"][0]["components"].size() == 1);
    }

    TEST_CASE("prefabs nest, and a prefab that contains itself stops")
    {
        Geni::RegisterBuiltinComponents();
        PrefabFixture fixture("prefab_nested");
        const Geni::AssetID lampID = fixture.assets.RegisterSourceAsset(Geni::AssetType::Prefab, "lamp.gprefab");
        const Geni::AssetID streetID = fixture.assets.RegisterSourceAsset(Geni::AssetType::Prefab, "street.gprefab");
        const Geni::AssetID loopID = fixture.assets.RegisterSourceAsset(Geni::AssetType::Prefab, "loop.gprefab");

        {
            Geni::Scene source;
            auto *lamp = source.CreateObject("Lamp");
            AddLight(lamp, 1.0f);
            REQUIRE(Geni::SceneSerializer::SavePrefab(*lamp, fixture.root / "lamp.gprefab"));
        }
        {
            // A street with two lamp instances.
            Geni::Scene source;
            auto context = fixture.Context(source);
            auto *street = source.CreateObject("Street");
            REQUIRE(Geni::SceneSerializer::InstantiatePrefab(source, lampID, context, street) != nullptr);
            REQUIRE(Geni::SceneSerializer::InstantiatePrefab(source, lampID, context, street) != nullptr);
            REQUIRE(Geni::SceneSerializer::SavePrefab(*street, fixture.root / "street.gprefab"));
        }

        Geni::Scene scene;
        auto context = fixture.Context(scene);
        Geni::GameObject *street = Geni::SceneSerializer::InstantiatePrefab(scene, streetID, context);
        REQUIRE(street != nullptr);
        REQUIRE(street->GetChildren().size() == 2);
        CHECK(street->GetChildren()[0]->IsPrefabInstance());
        CHECK(street->GetChildren()[0]->IsFromPrefab());
        CHECK(scene.CountComponents<Geni::LightComponent>() == 2);
        // The whole street is one line in the scene file.
        CHECK(Geni::SceneSerializer::SaveToJson(scene)["nodes"].size() == 1);

        // A prefab whose child is an instance of the same prefab.
        std::ofstream(fixture.root / "loop.gprefab")
            << R"({"format":"geni.prefab","version":1,"nodes":[{"name":"Loop","parent":-1,"components":[]},)"
            << R"({"name":"Inner","parent":0,"prefab":")" << loopID.ToHex() << R"(","components":[]}]})";
        Geni::GameObject *loop = Geni::SceneSerializer::InstantiatePrefab(scene, loopID, context);
        REQUIRE(loop != nullptr);              // it loads,
        CHECK(CountObjects(*loop) < 100);      // and it ends
    }

    TEST_CASE("a scene with a missing prefab still loads")
    {
        PrefabFixture fixture("prefab_missing");
        const nlohmann::json document = {
            {"format", "geni.scene"},
            {"version", 1},
            {"nodes", nlohmann::json::array({{{"name", "Ghost"}, {"parent", -1}, {"prefab", "00000000deadbeef"}}})}
        };
        Geni::Scene scene;
        auto context = fixture.Context(scene);
        REQUIRE(Geni::SceneSerializer::Load(scene, document, context));
        REQUIRE(scene.GetObjects().size() == 1);
        // The link survives a save, so fixing the file later brings the instance back.
        CHECK(scene.GetObjects()[0]->IsPrefabInstance());
        CHECK(Geni::SceneSerializer::SaveToJson(scene)["nodes"][0]["prefab"] == "00000000deadbeef");
    }
}

TEST_SUITE("picking")
{
    TEST_CASE("a ray through a pixel points where the camera looks")
    {
        const Geni::CameraData camera = LookDownMinusZ(2.0f);
        const glm::vec2 size(800.0f, 400.0f);
        const Geni::Ray center = Geni::ScreenPointToRay(camera, size * 0.5f, size);
        CHECK(GeniTest::Near(center.direction, glm::vec3(0, 0, -1)));
        CHECK(GeniTest::Near(center.origin, glm::vec3(0, 0, 9.9f), 1e-3f));

        // Right of centre goes to +X, above centre (smaller y) goes to +Y.
        CHECK(Geni::ScreenPointToRay(camera, glm::vec2(700, 200), size).direction.x > 0.1f);
        CHECK(Geni::ScreenPointToRay(camera, glm::vec2(400, 50), size).direction.y > 0.1f);

        // And back: a world point projects to the pixel its ray came from.
        const Geni::Ray ray = Geni::ScreenPointToRay(camera, glm::vec2(620, 130), size);
        glm::vec2 pixel;
        REQUIRE(Geni::WorldToScreen(camera, ray.At(7.0f), size, pixel));
        CHECK(pixel.x == doctest::Approx(620.0f).epsilon(0.001));
        CHECK(pixel.y == doctest::Approx(130.0f).epsilon(0.001));
        CHECK_FALSE(Geni::WorldToScreen(camera, glm::vec3(0, 0, 20), size, pixel)); // behind the camera
    }

    TEST_CASE("rays hit spheres and oriented boxes at the right distance")
    {
        const Geni::Ray ray{glm::vec3(0, 0, 10), glm::vec3(0, 0, -1)};
        float distance = 0.0f;
        REQUIRE(Geni::RayIntersectsSphere(ray, glm::vec3(0, 0, 0), 2.0f, distance));
        CHECK(distance == doctest::Approx(8.0f));
        CHECK_FALSE(Geni::RayIntersectsSphere(ray, glm::vec3(5, 0, 0), 2.0f, distance));
        CHECK_FALSE(Geni::RayIntersectsSphere(ray, glm::vec3(0, 0, 20), 2.0f, distance)); // behind

        const glm::vec3 low(-0.5f), high(0.5f);
        REQUIRE(Geni::RayIntersectsBox(ray, low, high, glm::mat4(1.0f), distance));
        CHECK(distance == doctest::Approx(9.5f));

        // Scaled: the distance stays in world units.
        const glm::mat4 scaled = glm::scale(glm::mat4(1.0f), glm::vec3(1, 1, 6));
        REQUIRE(Geni::RayIntersectsBox(ray, low, high, scaled, distance));
        CHECK(distance == doctest::Approx(7.0f));

        // A box moved aside is missed; turned 45 degrees its corner reaches further.
        const glm::mat4 moved = glm::translate(glm::mat4(1.0f), glm::vec3(0.6f, 0, 0));
        CHECK_FALSE(Geni::RayIntersectsBox(ray, low, high, moved, distance));
        const glm::mat4 turned = moved * glm::rotate(glm::mat4(1.0f), glm::radians(45.0f), glm::vec3(0, 0, 1));
        CHECK(Geni::RayIntersectsBox(ray, low, high, turned, distance));
    }

    TEST_CASE("picking returns the nearest light or camera marker")
    {
        Geni::Scene scene;
        auto *farLight = scene.CreateObject("far");
        farLight->SetPosition(glm::vec3(0, 0, -5));
        farLight->AddComponent(new Geni::LightComponent());
        auto *nearLight = scene.CreateObject("near");
        nearLight->SetPosition(glm::vec3(0, 0, 2));
        nearLight->AddComponent(new Geni::LightComponent());
        auto *empty = scene.CreateObject("empty"); // nothing to see, nothing to pick
        empty->SetPosition(glm::vec3(0, 0, 6));

        const Geni::Ray ray{glm::vec3(0, 0, 10), glm::vec3(0, 0, -1)};
        Geni::PickResult hit = Geni::PickObject(scene, ray);
        REQUIRE(hit);
        CHECK(hit.object == nearLight);
        CHECK(hit.distance == doctest::Approx(7.5f));

        nearLight->SetActive(false);
        CHECK(Geni::PickObject(scene, ray).object == farLight);
        Geni::PickOptions options;
        options.includeInactive = true;
        CHECK(Geni::PickObject(scene, ray, options).object == nearLight);

        const Geni::Ray miss{glm::vec3(50, 0, 10), glm::vec3(0, 0, -1)};
        CHECK_FALSE(Geni::PickObject(scene, miss));
    }
}

TEST_SUITE("file watcher")
{
    TEST_CASE("added, changed and removed files are reported once")
    {
        const auto root = std::filesystem::temp_directory_path() / "geni_tests" / "watcher";
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root / "shaders");
        std::filesystem::create_directories(root / "cooked");
        std::ofstream(root / "shaders" / "lit.slang") << "a";
        std::ofstream(root / "notes.txt") << "ignored";

        Geni::FileWatcher watcher;
        watcher.Watch(root, {".slang", ".png"});
        CHECK(watcher.Poll().empty()); // what was there at the start is the baseline

        std::ofstream(root / "new.png") << "p";
        std::ofstream(root / "notes.txt") << "still ignored";
        std::ofstream(root / "cooked" / "skipped.png") << "p"; // build output, not a source
        // A rewrite within the clock's resolution would look unchanged; move the time on.
        const auto file = root / "shaders" / "lit.slang";
        std::ofstream(file) << "b";
        std::filesystem::last_write_time(file, std::filesystem::last_write_time(file) + std::chrono::seconds(2));

        auto changes = watcher.Poll();
        REQUIRE(changes.size() == 2);
        CHECK(changes[0].path == "new.png");
        CHECK(changes[0].kind == Geni::FileWatcher::ChangeKind::Added);
        CHECK(changes[1].path == "shaders/lit.slang");
        CHECK(changes[1].kind == Geni::FileWatcher::ChangeKind::Modified);
        CHECK(watcher.Poll().empty());

        std::filesystem::remove(root / "new.png");
        changes = watcher.Poll();
        REQUIRE(changes.size() == 1);
        CHECK(changes[0].kind == Geni::FileWatcher::ChangeKind::Removed);
        std::filesystem::remove_all(root);
    }
}
