#include "scene/components/AnimationComponent.h"
#include "assets/AssetManager.h"
#include "glm/common.hpp"
#include "glm/ext/quaternion_common.hpp"
#include "glm/ext/vector_float3.hpp"
#include "scene/GameObject.h"
#include "scene/Reflection.h"
#include <cmath>
#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

namespace Geni
{

void AnimationComponent::ApplyAnimationAsset()
{
    if (m_animationApplied || !m_animationHandle.IsReady())
    {
        return;
    }
    m_animationApplied = true;
    for (const auto &clip : m_animationHandle->clips)
    {
        if (clip)
        {
            RegisterClip(clip->name, clip);
        }
    }
    if (!m_pendingClipName.empty())
    {
        Play(m_pendingClipName, m_looping);
    }
    else if (m_clip == nullptr)
    {
        PlayFirst(m_looping);
    }
}

void AnimationComponent::ResolveAssets(SerializationContext &context)
{
    if (context.assets && m_animationID.IsValid() && !m_animationHandle.IsValid())
    {
        SetAnimation(context.assets->LoadAnimation(m_animationID, true));
    }
}

void AnimationComponent::SetAnimation(const AssetHandle<AnimationAsset> &animation)
{
    m_animationHandle = animation;
    m_animationID = animation.GetID();
    m_animationApplied = false;
}

void AnimationComponent::SetClipName(const std::string &name)
{
    m_pendingClipName = name;
    if (m_clips.find(name) != m_clips.end())
    {
        Play(name, m_looping);
    }
}

void AnimationComponent::Update(float deltaTime)
{
    if (m_owner && m_owner->GetScene())
    {
        return; // Scene::Update has already run AdvanceClock, SamplePose and ApplyPose
    }
    if (AdvanceClock(deltaTime))
    {
        SamplePose();
        ApplyPose();
    }
}

bool AnimationComponent::AdvanceClock(float deltaTime)
{
    ApplyAnimationAsset();

    if (!m_clip || !m_isPlaying)
    {
        return false;
    }

    m_time += deltaTime;

    if (m_time > m_clip->duration)
    {
        if (m_looping)
        {
            m_time = std::fmod(m_time, m_clip->duration);
        }
        else
        {
            m_time = 0.0f;
            m_isPlaying = false;
            return false;
        }
    }
    return true;
}

void AnimationComponent::SamplePose()
{
    m_pose.clear();
    if (!m_clip)
    {
        return;
    }
    const float time = m_time;
    for (auto &binding : m_bindings)
    {
        for (auto i : binding.second->trackIndices)
        {
            const auto &track = m_clip->tracks[i];
            PoseEntry entry;
            entry.object = binding.first;
            if (!track.positions.empty())
            {
                entry.hasPosition = true;
                entry.position = Interpolate(track.positions, time);
            }
            if (!track.rotations.empty())
            {
                entry.hasRotation = true;
                entry.rotation = Interpolate(track.rotations, time);
            }
            if (!track.scales.empty())
            {
                entry.hasScale = true;
                entry.scale = Interpolate(track.scales, time);
            }
            m_pose.push_back(entry);
        }
    }
}

void AnimationComponent::ApplyPose()
{
    for (const auto &entry : m_pose)
    {
        if (entry.hasPosition)
        {
            entry.object->SetPosition(entry.position);
        }
        if (entry.hasRotation)
        {
            entry.object->SetRotation(entry.rotation);
        }
        if (entry.hasScale)
        {
            entry.object->SetScale(entry.scale);
        }
    }
    m_pose.clear();
}

void AnimationComponent::SetClip(AnimationClip *clip)
{
    m_clip = clip;
    BuildBindings();
}

void AnimationComponent::RegisterClip(const std::string &name, const std::shared_ptr<AnimationClip> &clip)
{
    m_clips[name] = clip;
}

void AnimationComponent::Play(const std::string &name, bool loop)
{
    if (m_clip && m_clip->name == name)
    {
        m_time = 0.0f;
        m_isPlaying = true;
        m_looping = loop;
    }
    else
    {
        auto it = m_clips.find(name);
        if (it != m_clips.end())
        {
            SetClip(it->second.get());
            m_time = 0.0f;
            m_isPlaying = true;
            m_looping = loop;
        }
    }
}

void AnimationComponent::PlayFirst(bool loop)
{
    if (m_clips.empty())
    {
        return;
    }
    SetClip(m_clips.begin()->second.get());
    m_time = 0.0f;
    m_isPlaying = true;
    m_looping = loop;
}

glm::vec3 AnimationComponent::Interpolate(const std::vector<KeyFrameVec3> &keys, float time) const
{
    if (keys.empty())
    {
        return glm::vec3(0.0f);
    }

    if (keys.size() == 1)
    {
        return keys[0].value;
    }

    if (time <= keys.front().time)
    {
        return keys.front().value;
    }

    if (time >= keys.back().time)
    {
        return keys.back().value;
    }

    size_t i0 = 0;
    size_t i1 = 0;

    for (size_t i = 1; i < keys.size(); ++i)
    {
        if (time <= keys[i].time)
        {
            i1 = i;
            break;
        }
    }

    i0 = i1 > 0 ? i1 - 1 : 0;

    if (time >= keys[i0].time && time <= keys[i1].time)
    {
        float deltaTime = keys[i1].time - keys[i0].time;
        float k = (time - keys[i0].time) / deltaTime;

        return glm::mix(keys[i0].value, keys[i1].value, k);
    }

    return keys.back().value;
}

glm::quat AnimationComponent::Interpolate(const std::vector<KeyFrameQuat> &keys, float time) const
{
    if (keys.empty())
    {
        return glm::vec3(0.0f);
    }

    if (keys.size() == 1)
    {
        return keys[0].value;
    }

    if (time <= keys.front().time)
    {
        return keys.front().value;
    }

    if (time >= keys.back().time)
    {
        return keys.back().value;
    }

    size_t i0 = 0;
    size_t i1 = 0;

    for (size_t i = 1; i < keys.size(); ++i)
    {
        if (time <= keys[i].time)
        {
            i1 = i;
            break;
        }
    }

    i0 = i1 > 0 ? i1 - 1 : 0;

    if (time >= keys[i0].time && time <= keys[i1].time)
    {
        float deltaTime = keys[i1].time - keys[i0].time;
        float k = (time - keys[i0].time) / deltaTime;

        return glm::slerp(keys[i0].value, keys[i1].value, k);
    }

    return keys.back().value;
}

void AnimationComponent::BuildBindings()
{
    m_bindings.clear();
    if (!m_clip)
    {
        return;
    }

    for (size_t i = 0; i < m_clip->tracks.size(); ++i)
    {
        auto &track = m_clip->tracks[i];
        auto targetObject = m_owner->FindChildByName(track.targetName);
        if (targetObject)
        {
            auto it = m_bindings.find(targetObject);
            if (it != m_bindings.end())
            {
                it->second->trackIndices.push_back(i);
            }
            else
            {
                auto binding = std::make_unique<ObjectBinding>();
                binding->object = targetObject;
                binding->trackIndices.push_back(i);
                m_bindings.emplace(targetObject, std::move(binding));
            }
        }
    }
}

namespace
{
const bool s_registered = RegisterComponentType<AnimationComponent>(
    "AnimationComponent",
    {
        AssetIDProperty<AnimationComponent>(
            "animation", AssetType::Animation, &AnimationComponent::GetAnimationID, &AnimationComponent::SetAnimationID
        ),
        StringProperty<AnimationComponent>("clip", &AnimationComponent::GetClipName, &AnimationComponent::SetClipName),
        FloatProperty<AnimationComponent>("time", &AnimationComponent::GetTime, &AnimationComponent::SetTime),
        BoolProperty<AnimationComponent>("playing", &AnimationComponent::IsPlaying, &AnimationComponent::SetPlaying),
        BoolProperty<AnimationComponent>("looping", &AnimationComponent::IsLooping, &AnimationComponent::SetLooping),
    }
);
} // namespace

} // namespace Geni

namespace Geni
{
void LinkAnimationComponent()
{
}
} // namespace Geni
