#include "TestEngine.h"
#include "scene/GameObject.h"
#include "scene/Scene.h"
#include "scene/components/AnimationComponent.h"
#include <doctest/doctest.h>
#include <memory>

using GeniTest::Near;

namespace
{

std::shared_ptr<Geni::AnimationClip> MakeSlideClip()
{
    auto clip = std::make_shared<Geni::AnimationClip>();
    clip->name = "slide";
    clip->duration = 2.0f;

    Geni::TransformTrack track;
    track.targetName = "target";
    track.positions = {{0.0f, glm::vec3(0.0f)}, {2.0f, glm::vec3(4.0f, 0.0f, 0.0f)}};
    track.rotations = {
        {0.0f, glm::quat(1.0f, 0.0f, 0.0f, 0.0f)},
        {2.0f, glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f))}
    };
    track.scales = {{0.0f, glm::vec3(1.0f)}, {2.0f, glm::vec3(3.0f)}};
    clip->tracks.push_back(track);

    Geni::TransformTrack orphan;
    orphan.targetName = "no such object";
    orphan.positions = {{0.0f, glm::vec3(9.0f)}};
    clip->tracks.push_back(orphan);

    return clip;
}

} // namespace

TEST_SUITE("animation")
{
    TEST_CASE("a playing clip interpolates the target's transform")
    {
        Geni::Scene scene;
        auto root = scene.CreateObject("root");
        auto target = scene.CreateObject("target", root);
        auto animation = new Geni::AnimationComponent();
        root->AddComponent(animation);
        animation->RegisterClip("slide", MakeSlideClip());
        animation->Play("slide");

        scene.Update(0.5f);
        CHECK(Near(target->GetPosition(), glm::vec3(1.0f, 0.0f, 0.0f)));
        CHECK(Near(target->GetScale(), glm::vec3(1.5f)));
        CHECK(
            GeniTest::SameRotation(
                target->GetRotation(), glm::angleAxis(glm::radians(22.5f), glm::vec3(0.0f, 1.0f, 0.0f))
            )
        );

        scene.Update(0.5f);
        CHECK(Near(target->GetPosition(), glm::vec3(2.0f, 0.0f, 0.0f)));
    }

    TEST_CASE("nothing moves until a clip is played")
    {
        Geni::Scene scene;
        auto root = scene.CreateObject("root");
        auto target = scene.CreateObject("target", root);
        target->SetPosition(glm::vec3(7.0f));
        auto animation = new Geni::AnimationComponent();
        root->AddComponent(animation);
        animation->RegisterClip("slide", MakeSlideClip());

        scene.Update(1.0f);
        CHECK(Near(target->GetPosition(), glm::vec3(7.0f)));

        animation->Play("unknown clip");
        scene.Update(1.0f);
        CHECK(Near(target->GetPosition(), glm::vec3(7.0f)));
    }

    TEST_CASE("a looping clip wraps around")
    {
        Geni::Scene scene;
        auto root = scene.CreateObject("root");
        auto target = scene.CreateObject("target", root);
        auto animation = new Geni::AnimationComponent();
        root->AddComponent(animation);
        animation->RegisterClip("slide", MakeSlideClip());
        animation->Play("slide", true);

        scene.Update(1.5f);
        scene.Update(1.0f); // 2.5 s into a 2 s clip -> 0.5 s
        CHECK(Near(target->GetPosition(), glm::vec3(1.0f, 0.0f, 0.0f)));
    }

    TEST_CASE("a non-looping clip stops at its end")
    {
        Geni::Scene scene;
        auto root = scene.CreateObject("root");
        auto target = scene.CreateObject("target", root);
        auto animation = new Geni::AnimationComponent();
        root->AddComponent(animation);
        animation->RegisterClip("slide", MakeSlideClip());
        animation->Play("slide", false);

        scene.Update(1.0f);
        const glm::vec3 midway = target->GetPosition();
        CHECK(Near(midway, glm::vec3(2.0f, 0.0f, 0.0f)));

        scene.Update(1.5f); // past the end: playback stops
        scene.Update(0.5f);
        scene.Update(0.5f);
        CHECK(Near(target->GetPosition(), midway));
    }

    TEST_CASE("PlayFirst starts the only registered clip")
    {
        Geni::Scene scene;
        auto root = scene.CreateObject("root");
        auto target = scene.CreateObject("target", root);
        auto animation = new Geni::AnimationComponent();
        root->AddComponent(animation);

        animation->PlayFirst(); // no clips yet: must not crash
        animation->RegisterClip("slide", MakeSlideClip());
        animation->PlayFirst();

        scene.Update(1.0f);
        CHECK(Near(target->GetPosition(), glm::vec3(2.0f, 0.0f, 0.0f)));
    }
}
