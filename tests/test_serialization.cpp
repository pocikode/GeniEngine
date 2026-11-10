#include "TestEngine.h"
#include "scene/BuiltinComponents.h"
#include "scene/GameObject.h"
#include "scene/Reflection.h"
#include "scene/Scene.h"
#include "scene/SceneSerializer.h"
#include "scene/components/CameraComponent.h"
#include "scene/components/LightComponent.h"
#include "scene/components/MeshComponent.h"
#include "scene/components/OrbitCameraComponent.h"
#include <algorithm>
#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

TEST_SUITE("reflection")
{
    TEST_CASE("built-in components expose named properties")
    {
        Geni::RegisterBuiltinComponents();
        for (const char *name : {"MeshComponent", "SkinnedMeshComponent", "CameraComponent", "LightComponent",
                                 "AnimationComponent", "PhysicsComponent", "OrbitCameraComponent",
                                 "PlayerControllerComponent"})
        {
            CHECK_MESSAGE(Geni::TypeRegistry::Get().Find(name) != nullptr, name);
        }

        const auto *light = Geni::TypeRegistry::Get().Find("LightComponent");
        REQUIRE(light != nullptr);
        CHECK(light->name == "LightComponent");

        std::vector<std::string> names;
        for (const auto &property : light->properties)
        {
            names.push_back(property.name);
        }
        CHECK(std::find(names.begin(), names.end(), "type") != names.end());
        CHECK(std::find(names.begin(), names.end(), "color") != names.end());
        CHECK(std::find(names.begin(), names.end(), "intensity") != names.end());

        auto *component = Geni::TypeRegistry::Get().Create("LightComponent");
        REQUIRE(component != nullptr);
        CHECK(component->GetTypeName() == std::string("LightComponent"));
        delete component;
    }

    TEST_CASE("reflection serializes and restores property values")
    {
        Geni::Scene scene;
        auto *object = scene.CreateObject("lamp");
        auto *light = new Geni::LightComponent();
        light->SetType(Geni::LightType::Spot);
        light->SetColor(glm::vec3(0.25f, 0.5f, 0.75f));
        light->SetIntensity(3.5f);
        light->SetRange(12.0f);
        object->AddComponent(light);

        Geni::SerializationContext context;
        const auto properties = Geni::TypeRegistry::Get().Serialize("LightComponent", *light);
        CHECK(properties["type"] == "spot");
        CHECK(properties["intensity"] == doctest::Approx(3.5f));

        auto *restored = new Geni::LightComponent();
        REQUIRE(Geni::TypeRegistry::Get().Deserialize("LightComponent", *restored, properties, context));
        CHECK(restored->GetType() == Geni::LightType::Spot);
        CHECK(GeniTest::Near(restored->GetColor(), glm::vec3(0.25f, 0.5f, 0.75f)));
        CHECK(restored->GetIntensity() == doctest::Approx(3.5f));
        CHECK(restored->GetRange() == doctest::Approx(12.0f));
        delete restored;
    }
}

TEST_SUITE("scene serialization")
{
    TEST_CASE("a scene saves and loads back with transforms and components")
    {
        Geni::Scene scene;
        auto *root = scene.CreateObject("root");
        root->SetPosition(glm::vec3(1.0f, 2.0f, 3.0f));
        root->SetRotation(glm::angleAxis(glm::radians(45.0f), glm::vec3(0.0f, 1.0f, 0.0f)));
        root->SetScale(glm::vec3(2.0f));

        auto *child = scene.CreateObject("child", root);
        child->SetPosition(glm::vec3(0.0f, 1.0f, 0.0f));
        auto *camera = new Geni::CameraComponent();
        camera->SetFieldOfView(75.0f);
        camera->SetFarPlane(500.0f);
        child->AddComponent(camera);

        auto *light = new Geni::LightComponent();
        light->SetType(Geni::LightType::Point);
        light->SetIntensity(4.0f);
        light->SetRange(20.0f);
        root->AddComponent(light);

        auto *mesh = new Geni::MeshComponent();
        mesh->SetMeshID(Geni::AssetID::FromName("mesh:test"));
        mesh->SetMaterialID(Geni::AssetID::FromName("material:test"));
        child->AddComponent(mesh);

        const auto document = Geni::SceneSerializer::SaveToJson(scene);
        CHECK(document["format"] == "geni.scene");
        CHECK(document["version"] == 1);
        REQUIRE(document["nodes"].size() == 2);
        CHECK(document["nodes"][0]["name"] == "root");
        CHECK(document["nodes"][1]["parent"] == 0);
        CHECK(document["mainCamera"] == -1);

        Geni::Scene loaded;
        Geni::SerializationContext context;
        REQUIRE(Geni::SceneSerializer::Load(loaded, document, context));

        auto *loadedRoot = loaded.FindObject("root");
        auto *loadedChild = loaded.FindObject("child");
        REQUIRE(loadedRoot != nullptr);
        REQUIRE(loadedChild != nullptr);
        CHECK(loadedChild->GetParent() == loadedRoot);
        CHECK(GeniTest::Near(loadedRoot->GetPosition(), glm::vec3(1.0f, 2.0f, 3.0f)));
        CHECK(
            GeniTest::SameRotation(
                loadedRoot->GetRotation(), glm::angleAxis(glm::radians(45.0f), glm::vec3(0.0f, 1.0f, 0.0f))
            )
        );
        CHECK(GeniTest::Near(loadedRoot->GetScale(), glm::vec3(2.0f)));

        auto *loadedCamera = loadedChild->GetComponent<Geni::CameraComponent>();
        REQUIRE(loadedCamera != nullptr);
        CHECK(loadedCamera->GetFieldOfView() == doctest::Approx(75.0f));
        CHECK(loadedCamera->GetFarPlane() == doctest::Approx(500.0f));

        auto *loadedLight = loadedRoot->GetComponent<Geni::LightComponent>();
        REQUIRE(loadedLight != nullptr);
        CHECK(loadedLight->GetType() == Geni::LightType::Point);
        CHECK(loadedLight->GetIntensity() == doctest::Approx(4.0f));
        CHECK(loadedLight->GetRange() == doctest::Approx(20.0f));

        auto *loadedMesh = loadedChild->GetComponent<Geni::MeshComponent>();
        REQUIRE(loadedMesh != nullptr);
        CHECK(loadedMesh->GetMeshID() == Geni::AssetID::FromName("mesh:test"));
        CHECK(loadedMesh->GetMaterialID() == Geni::AssetID::FromName("material:test"));
    }

    TEST_CASE("saving to a file writes readable JSON that loads again")
    {
        Geni::Scene scene;
        scene.CreateObject("root");
        const auto path = std::filesystem::temp_directory_path() / "geni_scene_roundtrip.gscene";

        std::string error;
        const bool saved = Geni::SceneSerializer::Save(scene, path, &error);
        INFO(error);
        REQUIRE(saved);

        std::ifstream file(path);
        std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        CHECK(text.find("\"format\": \"geni.scene\"") != std::string::npos);
        CHECK(text.find('\n') != std::string::npos);

        Geni::Scene loaded;
        Geni::SerializationContext context;
        REQUIRE(Geni::SceneSerializer::LoadFromFile(loaded, path, context, &error));
        CHECK(loaded.FindObject("root") != nullptr);
        std::filesystem::remove(path);
    }

    TEST_CASE("an unknown version is rejected")
    {
        Geni::Scene scene;
        Geni::SerializationContext context;
        nlohmann::json document;
        document["format"] = "geni.scene";
        document["version"] = 99;
        document["nodes"] = nlohmann::json::array();
        CHECK_FALSE(Geni::SceneSerializer::Load(scene, document, context));
    }

    TEST_CASE("the scene keeps a dense per-type component index")
    {
        Geni::Scene scene;
        auto *a = scene.CreateObject("a");
        auto *b = scene.CreateObject("b");
        a->AddComponent(new Geni::LightComponent());
        b->AddComponent(new Geni::LightComponent());
        b->AddComponent(new Geni::CameraComponent());

        CHECK(scene.CountComponents<Geni::LightComponent>() == 2);
        CHECK(scene.CountComponents<Geni::CameraComponent>() == 1);
        int visited = 0;
        scene.ForEachComponent<Geni::LightComponent>([&](Geni::LightComponent &light) {
            (void)light;
            ++visited;
        });
        CHECK(visited == 2);

        a->MarkForDestroy();
        scene.Update(0.0f);
        CHECK(scene.CountComponents<Geni::LightComponent>() == 1);
    }

    TEST_CASE("removing components keeps the per-type index consistent")
    {
        // Removal swaps with the last entry using the index each component stores; remove
        // from the front, middle and back and check every survivor is still visited once.
        Geni::Scene scene;
        std::vector<Geni::GameObject *> objects;
        for (int i = 0; i < 100; ++i)
        {
            auto *object = scene.CreateObject("light" + std::to_string(i));
            object->AddComponent(new Geni::LightComponent());
            objects.push_back(object);
        }
        for (int i : {0, 50, 99, 1, 98})
        {
            objects[static_cast<size_t>(i)]->MarkForDestroy();
        }
        scene.Update(0.0f);
        CHECK(scene.CountComponents<Geni::LightComponent>() == 95);
        std::vector<Geni::GameObject *> visited;
        scene.ForEachComponent<Geni::LightComponent>([&](Geni::LightComponent &light) {
            visited.push_back(light.GetOwner());
        });
        std::sort(visited.begin(), visited.end());
        CHECK(std::adjacent_find(visited.begin(), visited.end()) == visited.end());
        CHECK(visited.size() == 95);
        scene.Clear();
        CHECK(scene.CountComponents<Geni::LightComponent>() == 0);
    }

    TEST_CASE("a saved scene has one line per node")
    {
        Geni::Scene scene;
        auto *root = scene.CreateObject("root");
        scene.CreateObject("child", root)->AddComponent(new Geni::LightComponent());
        const std::string text = Geni::SceneSerializer::SaveToText(scene);
        size_t nodeLines = 0;
        size_t start = 0;
        while (start < text.size())
        {
            const size_t end = text.find('\n', start);
            const std::string line = text.substr(start, end - start);
            if (line.find("\"name\":") != std::string::npos)
            {
                ++nodeLines;
                CHECK(line.find("\"parent\":") != std::string::npos); // the whole node
            }
            start = end == std::string::npos ? text.size() : end + 1;
        }
        CHECK(nodeLines == 2);
        CHECK(nlohmann::json::parse(text)["nodes"].size() == 2);
    }

    TEST_CASE("a malformed scene fails without throwing and leaves nothing behind")
    {
        Geni::RegisterBuiltinComponents();
        Geni::SerializationContext context;
        std::string error;

        SUBCASE("a wrongly typed node field")
        {
            Geni::Scene scene;
            nlohmann::json document = nlohmann::json::parse(
                R"({"format":"geni.scene","version":1,"nodes":[{"name":"a","parent":"zero"}]})"
            );
            bool loaded = true;
            CHECK_NOTHROW(loaded = Geni::SceneSerializer::Load(scene, document, context, &error));
            CHECK_FALSE(loaded);
            CHECK_FALSE(error.empty());
            CHECK(scene.GetObjects().empty());
        }

        SUBCASE("a wrongly typed vector")
        {
            Geni::Scene scene;
            nlohmann::json document = nlohmann::json::parse(
                R"({"format":"geni.scene","version":1,"nodes":[{"name":"a","parent":-1,"position":["x",0,0]}]})"
            );
            CHECK_FALSE(Geni::SceneSerializer::Load(scene, document, context, &error));
            CHECK(scene.GetObjects().empty());
        }

        SUBCASE("appending to a live scene removes only what was added")
        {
            Geni::Scene scene;
            scene.CreateObject("existing");
            nlohmann::json document = nlohmann::json::parse(
                R"({"format":"geni.scene","version":1,"nodes":[{"name":"ok","parent":-1},{"name":"bad","parent":0,"scale":[1,"y",1]}]})"
            );
            CHECK_FALSE(Geni::SceneSerializer::Load(scene, document, context, &error, false));
            scene.Update(0.0f); // removal is deferred to the next update
            REQUIRE(scene.GetObjects().size() == 1);
            CHECK(scene.GetObjects()[0]->GetName() == "existing");
        }
    }

    TEST_CASE("SceneInstantiation builds a scene a few nodes per step")
    {
        Geni::RegisterBuiltinComponents();
        Geni::Scene source;
        Geni::GameObject *parent = nullptr;
        for (int i = 0; i < 25; ++i)
        {
            auto *object = source.CreateObject("n" + std::to_string(i), i % 5 == 0 ? nullptr : parent);
            object->AddComponent(new Geni::LightComponent());
            if (i % 5 == 0)
            {
                parent = object;
            }
        }
        const nlohmann::json document = nlohmann::json::parse(Geni::SceneSerializer::SaveToText(source));

        Geni::Scene scene;
        Geni::SerializationContext context{&scene, nullptr};
        Geni::SceneInstantiation instantiation(scene, document, context);
        int steps = 0;
        while (instantiation.Step(4) == Geni::SceneInstantiation::Status::InProgress)
        {
            ++steps;
            CHECK(instantiation.GetProgress() < 1.0f);
        }
        REQUIRE(instantiation.GetStatus() == Geni::SceneInstantiation::Status::Done);
        CHECK(steps >= 12); // 25 objects + 25 component lists, 4 per step
        CHECK(instantiation.GetProgress() == 1.0f);
        CHECK(scene.CountComponents<Geni::LightComponent>() == 25);
        CHECK(scene.GetObjects().size() == 5);
        CHECK(Geni::SceneSerializer::SaveToText(scene) == Geni::SceneSerializer::SaveToText(source));
    }
}
