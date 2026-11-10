#include "TestEngine.h"
#include "scene/Component.h"
#include "scene/GameObject.h"
#include "scene/Scene.h"
#include "scene/components/LightComponent.h"
#include <doctest/doctest.h>
#include <string>
#include <vector>

namespace
{

// Records its lifecycle into a shared log so tests can check order and counts.
class ProbeComponent : public Geni::Component
{
    COMPONENT(ProbeComponent)

  public:
    explicit ProbeComponent(std::vector<std::string> *log = nullptr) : m_log(log)
    {
    }

    ~ProbeComponent() override
    {
        Record("destroy");
    }

    void Init() override
    {
        ownerWasSetInInit = GetOwner() != nullptr;
        Record("init");
    }

    void Update(float deltaTime) override
    {
        ++updates;
        lastDelta = deltaTime;
        Record("update");
    }

    int updates = 0;
    float lastDelta = 0.0f;
    bool ownerWasSetInInit = false;

  private:
    void Record(const char *event)
    {
        if (m_log)
        {
            m_log->push_back((GetOwner() ? GetOwner()->GetName() : std::string("?")) + ":" + event);
        }
    }

    std::vector<std::string> *m_log;
};

class OtherComponent : public Geni::Component
{
    COMPONENT(OtherComponent)

  public:
    void Update(float) override
    {
    }
};

class DerivedProbe : public ProbeComponent
{
    COMPONENT(DerivedProbe)
};

class CustomObject : public Geni::GameObject
{
  public:
    int customValue = 7;
};

} // namespace

TEST_SUITE("scene tree")
{
    TEST_CASE("created objects know their name, scene and parent")
    {
        Geni::Scene scene;
        auto root = scene.CreateObject("root");
        auto child = scene.CreateObject("child", root);

        CHECK(root->GetName() == "root");
        CHECK(root->GetScene() == &scene);
        CHECK(root->GetParent() == nullptr);
        CHECK(child->GetParent() == root);
        CHECK(child->GetScene() == &scene);
        REQUIRE(root->GetChildren().size() == 1);
        CHECK(root->GetChildren()[0].get() == child);
    }

    TEST_CASE("the templated CreateObject builds subclasses")
    {
        Geni::Scene scene;
        CustomObject *object = scene.CreateObject<CustomObject>("custom");
        CHECK(object->customValue == 7);
        CHECK(object->GetName() == "custom");
        CHECK(object->GetScene() == &scene);
    }

    TEST_CASE("reparenting moves an object between owners")
    {
        Geni::Scene scene;
        auto a = scene.CreateObject("a");
        auto b = scene.CreateObject("b");
        auto child = scene.CreateObject("child", a);

        SUBCASE("from one parent to another")
        {
            CHECK(child->SetParent(b));
            CHECK(child->GetParent() == b);
            CHECK(a->GetChildren().empty());
            REQUIRE(b->GetChildren().size() == 1);
            CHECK(b->GetChildren()[0].get() == child);
        }

        SUBCASE("from a parent to the scene root")
        {
            CHECK(child->SetParent(nullptr));
            CHECK(child->GetParent() == nullptr);
            CHECK(a->GetChildren().empty());
            // Still owned by the scene: updating reaches it.
            auto probe = new ProbeComponent();
            child->AddComponent(probe);
            scene.Update(0.016f);
            CHECK(probe->updates == 1);
        }

        SUBCASE("from the scene root to a parent")
        {
            CHECK(b->SetParent(a));
            CHECK(b->GetParent() == a);
            CHECK(a->GetChildren().size() == 2);
        }

        SUBCASE("to the parent it already has")
        {
            CHECK(child->SetParent(a));
            CHECK(child->GetParent() == a);
            CHECK(a->GetChildren().size() == 1);
        }
    }

    TEST_CASE("reparenting that would create a cycle is rejected")
    {
        Geni::Scene scene;
        auto root = scene.CreateObject("root");
        auto child = scene.CreateObject("child", root);
        auto grandchild = scene.CreateObject("grandchild", child);

        CHECK_FALSE(root->SetParent(grandchild));
        CHECK_FALSE(child->SetParent(grandchild));
        CHECK_FALSE(child->SetParent(child));

        CHECK(root->GetParent() == nullptr);
        CHECK(child->GetParent() == root);
        CHECK(grandchild->GetParent() == child);
    }

    TEST_CASE("FindChildByName searches the whole subtree, including the object itself")
    {
        Geni::Scene scene;
        auto root = scene.CreateObject("root");
        auto arm = scene.CreateObject("arm", root);
        auto hand = scene.CreateObject("hand", arm);
        scene.CreateObject("leg", root);

        CHECK(root->FindChildByName("root") == root);
        CHECK(root->FindChildByName("hand") == hand);
        CHECK(arm->FindChildByName("hand") == hand);
        CHECK(arm->FindChildByName("leg") == nullptr);
        CHECK(root->FindChildByName("missing") == nullptr);
    }

    TEST_CASE("Update visits components parent first, then children")
    {
        // Declared before the scene: components write to it from their destructors.
        std::vector<std::string> log;
        Geni::Scene scene;
        auto root = scene.CreateObject("root");
        auto child = scene.CreateObject("child", root);
        child->AddComponent(new ProbeComponent(&log));
        root->AddComponent(new ProbeComponent(&log));
        log.clear();

        scene.Update(0.25f);

        REQUIRE(log.size() == 2);
        CHECK(log[0] == "root:update");
        CHECK(log[1] == "child:update");
        CHECK(root->GetComponent<ProbeComponent>()->lastDelta == doctest::Approx(0.25f));
    }

    TEST_CASE("an inactive object skips itself and its subtree")
    {
        Geni::Scene scene;
        auto root = scene.CreateObject("root");
        auto child = scene.CreateObject("child", root);
        auto rootProbe = new ProbeComponent();
        auto childProbe = new ProbeComponent();
        root->AddComponent(rootProbe);
        child->AddComponent(childProbe);

        root->SetActive(false);
        scene.Update(0.016f);
        CHECK(rootProbe->updates == 0);
        CHECK(childProbe->updates == 0);

        root->SetActive(true);
        scene.Update(0.016f);
        CHECK(rootProbe->updates == 1);
        CHECK(childProbe->updates == 1);
    }

    TEST_CASE("objects marked for destroy are removed on the next update")
    {
        // Declared before the scene: components write to it from their destructors.
        std::vector<std::string> log;
        Geni::Scene scene;
        auto root = scene.CreateObject("root");
        auto child = scene.CreateObject("child", root);
        auto loose = scene.CreateObject("loose");
        child->AddComponent(new ProbeComponent(&log));
        loose->AddComponent(new ProbeComponent(&log));
        log.clear();

        child->MarkForDestroy();
        loose->MarkForDestroy();
        CHECK_FALSE(child->IsAlive());
        // Deferred: nothing is freed until the scene is updated.
        CHECK(log.empty());
        CHECK(root->GetChildren().size() == 1);

        scene.Update(0.016f);

        CHECK(root->GetChildren().empty());
        REQUIRE(log.size() == 2);
        CHECK(log[0] == "child:destroy");
        CHECK(log[1] == "loose:destroy");
    }

    TEST_CASE("Clear destroys every object")
    {
        // Declared before the scene: components write to it from their destructors.
        std::vector<std::string> log;
        Geni::Scene scene;
        auto root = scene.CreateObject("root");
        scene.CreateObject("child", root)->AddComponent(new ProbeComponent(&log));
        log.clear();

        scene.Clear();
        REQUIRE(log.size() == 1);
        CHECK(log[0] == "child:destroy");
    }
}

TEST_SUITE("components")
{
    TEST_CASE("AddComponent sets the owner before calling Init")
    {
        Geni::Scene scene;
        auto object = scene.CreateObject("object");
        auto probe = new ProbeComponent();
        object->AddComponent(probe);

        CHECK(probe->ownerWasSetInInit);
        CHECK(probe->GetOwner() == object);
    }

    TEST_CASE("GetComponent finds components by exact type")
    {
        Geni::Scene scene;
        auto object = scene.CreateObject("object");
        CHECK(object->GetComponent<ProbeComponent>() == nullptr);

        auto probe = new ProbeComponent();
        auto other = new OtherComponent();
        object->AddComponent(probe);
        object->AddComponent(other);

        CHECK(object->GetComponent<ProbeComponent>() == probe);
        CHECK(object->GetComponent<OtherComponent>() == other);
        CHECK(object->GetComponent<Geni::LightComponent>() == nullptr);
    }

    TEST_CASE("GetComponent does not match a subclass when asked for its base")
    {
        Geni::Scene scene;
        auto object = scene.CreateObject("object");
        auto derived = new DerivedProbe();
        object->AddComponent(derived);

        CHECK(object->GetComponent<DerivedProbe>() == derived);
        CHECK(object->GetComponent<ProbeComponent>() == nullptr);
    }

    TEST_CASE("adding a null component is ignored")
    {
        Geni::Scene scene;
        auto object = scene.CreateObject("object");
        object->AddComponent(nullptr);
        scene.Update(0.016f);
        CHECK(object->GetComponent<ProbeComponent>() == nullptr);
    }
}

TEST_SUITE("scene queries")
{
    TEST_CASE("CollectLights gathers lights at any depth with world positions")
    {
        Geni::Scene scene;
        auto root = scene.CreateObject("root");
        root->SetPosition(glm::vec3(1.0f, 0.0f, 0.0f));

        auto lamp = scene.CreateObject("lamp", root);
        lamp->SetPosition(glm::vec3(0.0f, 2.0f, 0.0f));
        auto lampLight = new Geni::LightComponent();
        lampLight->SetColor(glm::vec3(1.0f, 0.5f, 0.25f));
        lamp->AddComponent(lampLight);

        auto sun = scene.CreateObject("sun");
        sun->AddComponent(new Geni::LightComponent());

        scene.CreateObject("not a light");

        const auto lights = scene.CollectLights();
        REQUIRE(lights.size() == 2);
        CHECK(GeniTest::Near(lights[0].position, glm::vec3(1.0f, 2.0f, 0.0f)));
        CHECK(GeniTest::Near(lights[0].color, glm::vec3(1.0f, 0.5f, 0.25f)));
        CHECK(GeniTest::Near(lights[1].color, glm::vec3(1.0f)));
    }

    TEST_CASE("the main camera is whatever was set")
    {
        Geni::Scene scene;
        CHECK(scene.GetMainCamera() == nullptr);
        auto camera = scene.CreateObject("camera");
        scene.SetMainCamera(camera);
        CHECK(scene.GetMainCamera() == camera);
    }
}
