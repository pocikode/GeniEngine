#pragma once

#include "animation/AnimationGraph.h"
#include "assets/AssetHandle.h"
#include "scene/Component.h"
#include <memory>
#include <string>
#include <vector>

namespace Geni
{

class AnimationAsset;
class GameObject;

// Plays an animation graph (docs/design/0011) on the joints under its object: the clips
// come from an animation asset, the states and transitions from a .animgraph file in the
// assets folder. Gameplay sets parameters; Scene::Update advances, samples (on workers)
// and applies the pose like it does for AnimationComponent.
class AnimatorComponent : public Component
{
    COMPONENT(AnimatorComponent)

  public:
    // Inside a scene, Scene::Update drives the three steps below; Update only does it for
    // an object that is not in a scene.
    void Update(float deltaTime) override;
    void ResolveAssets(SerializationContext &context) override;

    // Main thread: binds once the clips and the graph are ready, advances the graph.
    // True when a pose should be sampled.
    bool Advance(float deltaTime);
    // Any thread.
    void SamplePose();
    // Main thread.
    void ApplyPose();

    void SetAnimation(const AssetHandle<AnimationAsset> &animation);
    AssetID GetAnimationID() const
    {
        return m_animationID;
    }
    void SetAnimationID(AssetID id);
    std::string GetGraphPath() const
    {
        return m_graphPath;
    }
    void SetGraphPath(const std::string &path);
    // A graph made in code instead of a file.
    void SetGraph(std::shared_ptr<const AnimationGraph> graph);
    bool IsPlaying() const
    {
        return m_playing;
    }
    void SetPlaying(bool playing)
    {
        m_playing = playing;
    }

    void SetFloat(const std::string &name, float value)
    {
        m_instance.SetFloat(name, value);
    }
    void SetBool(const std::string &name, bool value)
    {
        m_instance.SetBool(name, value);
    }
    void SetTrigger(const std::string &name)
    {
        m_instance.SetTrigger(name);
    }
    float GetFloat(const std::string &name) const
    {
        return m_instance.GetFloat(name);
    }
    std::string GetStateName() const
    {
        return m_instance.GetStateName();
    }
    void Play(const std::string &state, float fadeSeconds = 0.0f)
    {
        m_instance.Play(state, fadeSeconds);
    }
    bool IsBound() const
    {
        return m_instance.IsBound();
    }
    // Clip events fired since the last call (footsteps).
    std::vector<std::string> ConsumeEvents()
    {
        return m_instance.TakeEvents();
    }
    const AnimationGraphInstance &GetInstance() const
    {
        return m_instance;
    }

    // Graphs are shared by path; tests and hot reload drop the cache.
    static std::shared_ptr<const AnimationGraph> LoadGraph(const std::string &path);
    static void ClearGraphCache();

  private:
    bool TryBind();

    AssetHandle<AnimationAsset> m_animation;
    AssetID m_animationID;
    std::string m_graphPath;
    std::shared_ptr<const AnimationGraph> m_graph;
    bool m_graphFailed = false;
    bool m_playing = true;
    AnimationGraphInstance m_instance;
    std::vector<GameObject *> m_joints;
    AnimPose m_pose;
    bool m_hasPose = false;
};

} // namespace Geni
