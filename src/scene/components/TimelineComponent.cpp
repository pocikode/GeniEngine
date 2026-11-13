#include "scene/components/TimelineComponent.h"

#include "core/Engine.h"
#include "core/Log.h"
#include "scene/GameObject.h"
#include "scene/Reflection.h"
#include "scene/Scene.h"

#include <algorithm>
#include <cmath>

namespace Geni
{

void TimelineComponent::Init()
{
    if (m_autoplay)
    {
        Play();
    }
}

void TimelineComponent::SetData(const TimelineData &data)
{
    m_data = data;
    m_targets.clear();
}

std::string TimelineComponent::GetTracksText() const
{
    return TimelineToJson(m_data).dump();
}

void TimelineComponent::SetTracksText(const std::string &text)
{
    const nlohmann::json document =
        text.empty() ? nlohmann::json::object() : nlohmann::json::parse(text, nullptr, false);
    TimelineData data;
    std::string error;
    if (document.is_discarded() || !ParseTimeline(document, data, &error))
    {
        GENI_LOG_WARN("TimelineComponent: %s", document.is_discarded() ? "tracks are not JSON" : error.c_str());
        return;
    }
    SetData(data);
}

void TimelineComponent::SetTime(float seconds)
{
    m_time = std::isfinite(seconds) ? std::max(0.0f, seconds) : 0.0f;
}

void TimelineComponent::SetDuration(float seconds)
{
    m_duration = std::isfinite(seconds) ? std::max(0.0f, seconds) : 0.0f;
}

void TimelineComponent::SetSpeed(float speed)
{
    m_speed = std::isfinite(speed) ? std::max(0.0f, speed) : 1.0f;
}

float TimelineComponent::GetLength() const
{
    return m_duration > 0.0f ? m_duration : m_data.LastKeyTime();
}

void TimelineComponent::Play()
{
    if (!m_playing && m_time >= GetLength() && !m_loop)
    {
        m_time = 0.0f;
    }
    m_playing = true;
}

void TimelineComponent::Stop()
{
    m_playing = false;
    m_time = 0.0f;
    ApplyAt(0.0f);
}

GameObject *TimelineComponent::FindTarget(const std::string &name)
{
    if (!m_owner || name.empty())
    {
        return nullptr;
    }
    Scene *scene = m_owner->GetScene();
    const auto cached = m_targets.find(name);
    if (cached != m_targets.end() && scene)
    {
        if (GameObject *object = scene->FindObjectByID(cached->second))
        {
            return object;
        }
    }
    // Below the timeline's object first, then anywhere in the scene.
    GameObject *object = m_owner->GetName() == name ? m_owner : m_owner->FindChildByName(name);
    if (!object && scene)
    {
        object = scene->FindObject(name);
    }
    if (object)
    {
        m_targets[name] = object->GetID();
    }
    return object;
}

void TimelineComponent::ApplyAt(float time)
{
    for (const TimelineTrack &track : m_data.transforms)
    {
        GameObject *target = FindTarget(track.target);
        glm::vec3 position;
        glm::quat rotation;
        glm::vec3 scale;
        if (target && SampleTrack(track, time, position, rotation, scale))
        {
            target->SetPosition(position);
            target->SetRotation(rotation);
            target->SetScale(scale);
        }
    }
}

void TimelineComponent::Fire(float from, float to)
{
    // Everything in (from, to], plus exactly 0 when starting from 0.
    auto passed = [from, to](float t) { return (t > from && t <= to) || (from == 0.0f && t == 0.0f); };
    Engine &engine = Engine::GetInstance();
    for (const CameraCut &cut : m_data.cuts)
    {
        if (passed(cut.time))
        {
            GameObject *camera = FindTarget(cut.camera);
            if (camera && m_owner && m_owner->GetScene())
            {
                m_owner->GetScene()->SetMainCamera(camera);
            }
        }
    }
    for (const SoundCue &cue : m_data.sounds)
    {
        if (passed(cue.time) && engine.GetAudio().IsInitialized())
        {
            SoundParams params;
            params.volume = cue.volume;
            engine.GetAudio().Play(cue.sound, params);
        }
    }
    for (const TimelineEvent &event : m_data.events)
    {
        if (passed(event.time))
        {
            m_events.push_back(event.name);
        }
    }
}

void TimelineComponent::Advance(float deltaTime)
{
    const float length = GetLength();
    const float from = m_time;
    float to = from + std::max(0.0f, deltaTime) * m_speed;
    if (to >= length)
    {
        if (m_loop && length > 0.0f)
        {
            Fire(from, length);
            to = std::fmod(to, length);
            Fire(-1.0f, to); // from the start again, including keys at 0
        }
        else
        {
            to = length;
            Fire(from, to);
            m_playing = false;
        }
    }
    else
    {
        Fire(from == 0.0f ? -1.0f : from, to);
    }
    m_time = to;
}

void TimelineComponent::Update(float deltaTime)
{
    if (m_playing)
    {
        Advance(deltaTime);
    }
    ApplyAt(m_time);
}

void TimelineComponent::EditUpdate()
{
    ApplyAt(m_time);
}

std::vector<std::string> TimelineComponent::TakeEvents()
{
    std::vector<std::string> events;
    events.swap(m_events);
    return events;
}

namespace
{
using T = TimelineComponent;
const bool s_registered = RegisterComponentType<TimelineComponent>(
    "TimelineComponent", {
                             StringProperty<T>("tracks", &T::GetTracksText, &T::SetTracksText),
                             FloatProperty<T>("time", &T::GetTime, &T::SetTime),
                             FloatProperty<T>("duration", &T::GetDuration, &T::SetDuration),
                             FloatProperty<T>("speed", &T::GetSpeed, &T::SetSpeed),
                             BoolProperty<T>("loop", &T::GetLoop, &T::SetLoop),
                             BoolProperty<T>("autoplay", &T::GetAutoplay, &T::SetAutoplay),
                         }
);
} // namespace

void LinkTimelineComponent()
{
}

} // namespace Geni
