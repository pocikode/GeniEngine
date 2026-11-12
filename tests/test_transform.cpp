#include "TestEngine.h"
#include "scene/GameObject.h"
#include "scene/Scene.h"
#include "scene/components/CameraComponent.h"
#include <doctest/doctest.h>
#include <glm/gtc/matrix_transform.hpp>

using GeniTest::Near;

TEST_SUITE("transform")
{
    TEST_CASE("a new object sits at the origin with no rotation and unit scale")
    {
        Geni::Scene scene;
        auto object = scene.CreateObject("object");

        CHECK(Near(object->GetPosition(), glm::vec3(0.0f)));
        CHECK(Near(object->GetScale(), glm::vec3(1.0f)));
        CHECK(GeniTest::SameRotation(object->GetRotation(), glm::quat(1.0f, 0.0f, 0.0f, 0.0f)));
        CHECK(Near(object->GetLocalTransform(), glm::mat4(1.0f)));
        CHECK(Near(object->GetWorldTransform(), glm::mat4(1.0f)));
    }

    TEST_CASE("local transform applies scale, then rotation, then translation")
    {
        Geni::Scene scene;
        auto object = scene.CreateObject("object");
        object->SetPosition(glm::vec3(10.0f, 0.0f, 0.0f));
        object->SetRotation(glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f)));
        object->SetScale(glm::vec3(2.0f, 1.0f, 1.0f));

        // (1,0,0) -> scaled to (2,0,0) -> rotated 90 degrees about Z to (0,2,0) -> moved to (10,2,0)
        const glm::vec4 point = object->GetLocalTransform() * glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
        CHECK(Near(glm::vec3(point), glm::vec3(10.0f, 2.0f, 0.0f)));
    }

    TEST_CASE("world transform composes parents before children")
    {
        Geni::Scene scene;
        auto parent = scene.CreateObject("parent");
        auto child = scene.CreateObject("child", parent);
        auto grandchild = scene.CreateObject("grandchild", child);

        parent->SetPosition(glm::vec3(0.0f, 5.0f, 0.0f));
        parent->SetRotation(glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f)));
        child->SetPosition(glm::vec3(1.0f, 0.0f, 0.0f));
        child->SetScale(glm::vec3(2.0f));
        grandchild->SetPosition(glm::vec3(0.0f, 0.0f, 1.0f));

        // Child offset (1,0,0) rotated 90 degrees about Y becomes (0,0,-1).
        CHECK(Near(child->GetWorldPosition(), glm::vec3(0.0f, 5.0f, -1.0f)));
        // Grandchild offset (0,0,1) is doubled by the child's scale, then rotated to (2,0,0).
        CHECK(Near(grandchild->GetWorldPosition(), glm::vec3(2.0f, 5.0f, -1.0f)));

        CHECK(Near(
            grandchild->GetWorldTransform(),
            parent->GetLocalTransform() * child->GetLocalTransform() * grandchild->GetLocalTransform()
        ));
    }

    TEST_CASE("moving a parent moves its children in world space only")
    {
        Geni::Scene scene;
        auto parent = scene.CreateObject("parent");
        auto child = scene.CreateObject("child", parent);
        child->SetPosition(glm::vec3(1.0f, 2.0f, 3.0f));

        parent->SetPosition(glm::vec3(100.0f, 0.0f, 0.0f));

        CHECK(Near(child->GetPosition(), glm::vec3(1.0f, 2.0f, 3.0f)));
        CHECK(Near(child->GetWorldPosition(), glm::vec3(101.0f, 2.0f, 3.0f)));
    }

    TEST_CASE("reparenting keeps the local transform, so the world position changes")
    {
        Geni::Scene scene;
        auto a = scene.CreateObject("a");
        auto b = scene.CreateObject("b");
        auto child = scene.CreateObject("child", a);
        a->SetPosition(glm::vec3(10.0f, 0.0f, 0.0f));
        b->SetPosition(glm::vec3(-10.0f, 0.0f, 0.0f));
        child->SetPosition(glm::vec3(1.0f, 0.0f, 0.0f));

        CHECK(Near(child->GetWorldPosition(), glm::vec3(11.0f, 0.0f, 0.0f)));
        REQUIRE(child->SetParent(b));
        CHECK(Near(child->GetWorldPosition(), glm::vec3(-9.0f, 0.0f, 0.0f)));
    }
}

TEST_SUITE("camera")
{
    TEST_CASE("view matrix is the inverse of the camera's world transform")
    {
        Geni::Scene scene;
        auto rig = scene.CreateObject("rig");
        auto camera = scene.CreateObject("camera", rig);
        auto component = new Geni::CameraComponent();
        camera->AddComponent(component);

        rig->SetPosition(glm::vec3(0.0f, 2.0f, 0.0f));
        camera->SetPosition(glm::vec3(0.0f, 0.0f, 5.0f));

        // A point 3 units in front of the camera (it looks down -Z) lands at z = -3 in view space.
        const glm::vec4 inView = component->GetViewMatrix() * glm::vec4(0.0f, 2.0f, 2.0f, 1.0f);
        CHECK(Near(glm::vec3(inView), glm::vec3(0.0f, 0.0f, -3.0f)));
    }

    TEST_CASE("view matrix ignores the camera object's scale")
    {
        Geni::Scene scene;
        auto camera = scene.CreateObject("camera");
        auto component = new Geni::CameraComponent();
        camera->AddComponent(component);
        camera->SetPosition(glm::vec3(1.0f, 2.0f, 3.0f));
        camera->SetScale(glm::vec3(4.0f));

        CHECK(Near(component->GetViewMatrix(), glm::translate(glm::mat4(1.0f), glm::vec3(-1.0f, -2.0f, -3.0f))));
    }

    TEST_CASE("projection matrix uses the given aspect ratio")
    {
        Geni::Scene scene;
        auto camera = scene.CreateObject("camera");
        auto component = new Geni::CameraComponent();
        camera->AddComponent(component);

        const glm::mat4 wide = component->GetProjectionMatrix(2.0f);
        const glm::mat4 square = component->GetProjectionMatrix(1.0f);
        CHECK(Near(wide[0][0] * 2.0f, square[0][0]));
        CHECK(Near(wide[1][1], square[1][1]));
    }
}
