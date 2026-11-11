#include "scene/components/AudioSourceComponent.h"

#include "core/Engine.h"
#include "scene/GameObject.h"
#include "scene/Reflection.h"

namespace Geni
{

void AudioSourceComponent::Init()
{
    // Autoplay starts on the first Update, once the object has its final transform.
    m_started = false;
}

void AudioSourceComponent::OnDestroy()
{
    Stop(0.05f);
}

void AudioSourceComponent::SetClip(const std::string &clip)
{
    if (clip != m_clip && m_sound != InvalidSound)
    {
        Stop();
        m_started = false;
    }
    m_clip = clip;
}

void AudioSourceComponent::SetVolume(float volume)
{
    m_params.volume = volume;
    if (m_sound != InvalidSound)
    {
        Engine::GetInstance().GetAudio().SetVolume(m_sound, volume);
    }
}

void AudioSourceComponent::SetPitch(float pitch)
{
    m_params.pitch = pitch;
    if (m_sound != InvalidSound)
    {
        Engine::GetInstance().GetAudio().SetPitch(m_sound, pitch);
    }
}

void AudioSourceComponent::Play()
{
    AudioSystem &audio = Engine::GetInstance().GetAudio();
    if (m_sound != InvalidSound)
    {
        audio.Stop(m_sound);
    }
    if (m_owner)
    {
        m_params.position = m_owner->GetWorldPosition();
    }
    m_sound = audio.Play(m_clip, m_params);
    m_started = true;
}

void AudioSourceComponent::Stop(float fadeSeconds)
{
    if (m_sound != InvalidSound)
    {
        Engine::GetInstance().GetAudio().Stop(m_sound, fadeSeconds);
        m_sound = InvalidSound;
    }
}

bool AudioSourceComponent::IsPlaying() const
{
    return m_sound != InvalidSound && Engine::GetInstance().GetAudio().IsPlaying(m_sound);
}

void AudioSourceComponent::Update(float deltaTime)
{
    (void)deltaTime;
    if (m_autoplay && !m_started && !m_clip.empty())
    {
        Play();
    }
    if (m_sound != InvalidSound && m_params.spatial)
    {
        Engine::GetInstance().GetAudio().SetPosition(m_sound, m_owner->GetWorldPosition());
    }
}

namespace
{
using S = AudioSourceComponent;
const bool s_registered = RegisterComponentType<S>(
    "AudioSourceComponent",
    {
        StringProperty<S>("clip", &S::GetClip, &S::SetClip),
        FloatProperty<S>("volume", &S::GetVolume, &S::SetVolume),
        FloatProperty<S>("pitch", &S::GetPitch, &S::SetPitch),
        BoolProperty<S>("loop", &S::GetLoop, &S::SetLoop),
        BoolProperty<S>("spatial", &S::GetSpatial, &S::SetSpatial),
        FloatProperty<S>("minDistance", &S::GetMinDistance, &S::SetMinDistance),
        FloatProperty<S>("maxDistance", &S::GetMaxDistance, &S::SetMaxDistance),
        BoolProperty<S>("autoplay", &S::GetAutoplay, &S::SetAutoplay),
        BoolProperty<S>("stream", &S::GetStream, &S::SetStream),
        BoolProperty<S>("music", &S::GetMusic, &S::SetMusic),
    }
);
} // namespace

void LinkAudioSourceComponent()
{
}

} // namespace Geni
