#pragma once

#include "assets/AssetHandle.h"
#include "glm/ext/quaternion_float.hpp"
#include "glm/ext/vector_float3.hpp"
#include "scene/Component.h"
#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Geni
{

class AnimationAsset;

struct KeyFrameVec3
{
    float time = 0.0f;
    glm::vec3 value = glm::vec3(0.0f);
};

struct KeyFrameQuat
{
    float time = 0.0f;
    glm::quat value = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
};

struct TransformTrack
{
    std::string targetName;
    std::vector<KeyFrameVec3> positions;
    std::vector<KeyFrameQuat> rotations;
    std::vector<KeyFrameVec3> scales;
};

struct AnimationClip
{
    std::string name;
    float duration = 0.0f;
    bool looping = true;
    std::vector<TransformTrack> tracks;
};

struct ObjectBinding
{
    GameObject *object = nullptr;
    std::vector<size_t> trackIndices;
};

class AnimationComponent : public Component
{
    COMPONENT(AnimationComponent);

  public:
    // Inside a Scene, animation is driven by Scene::Update, which advances every clock,
    // samples all poses in parallel on the job system and then applies them; Update only
    // does the work itself for a component whose object is not in a scene.
    void Update(float deltaTime) override;

    // The three steps Scene::Update runs for every animation component.
    // Main thread: loads the clip list once it is ready and advances the clock. Returns
    // true when a pose should be sampled this frame.
    bool AdvanceClock(float deltaTime);
    // Any thread: interpolates the tracks at the current time into an internal buffer.
    // Reads only this component's clip and bindings; writes nothing outside it.
    void SamplePose();
    // Main thread: writes the sampled pose to the bound objects.
    void ApplyPose();

    void ResolveAssets(SerializationContext &context) override;
    void SetClip(AnimationClip *clip);
    void RegisterClip(const std::string &name, const std::shared_ptr<AnimationClip> &clip);
    void Play(const std::string &name, bool loop = true);
    void PlayFirst(bool loop = true);

    void SetAnimation(const AssetHandle<AnimationAsset> &animation);
    AssetID GetAnimationID() const
    {
        return m_animationID;
    }
    void SetAnimationID(AssetID id)
    {
        m_animationID = id;
    }

    std::string GetClipName() const
    {
        return m_clip ? m_clip->name : m_pendingClipName;
    }
    void SetClipName(const std::string &name);
    float GetTime() const
    {
        return m_time;
    }
    void SetTime(float time)
    {
        m_time = time;
    }
    bool IsPlaying() const
    {
        return m_isPlaying;
    }
    void SetPlaying(bool playing)
    {
        m_isPlaying = playing;
    }
    bool IsLooping() const
    {
        return m_looping;
    }
    void SetLooping(bool looping)
    {
        m_looping = looping;
    }

  private:
    struct PoseEntry
    {
        GameObject *object = nullptr;
        bool hasPosition = false;
        bool hasRotation = false;
        bool hasScale = false;
        glm::vec3 position{0.0f};
        glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 scale{1.0f};
    };

    void ApplyAnimationAsset();

  private:
    void BuildBindings();
    glm::vec3 Interpolate(const std::vector<KeyFrameVec3> &keys, float time) const;
    glm::quat Interpolate(const std::vector<KeyFrameQuat> &keys, float time) const;

  private:
    AnimationClip *m_clip = nullptr;
    float m_time = 0.0f;
    bool m_looping = true;
    bool m_isPlaying = false;

    std::unordered_map<std::string, std::shared_ptr<AnimationClip>> m_clips;
    std::unordered_map<GameObject *, std::unique_ptr<ObjectBinding>> m_bindings;

    AssetHandle<AnimationAsset> m_animationHandle;
    AssetID m_animationID;
    std::string m_pendingClipName;
    bool m_animationApplied = false;
    std::vector<PoseEntry> m_pose;
};

} // namespace Geni