#pragma once

#include "audio/AudioSystem.h"
#include "scene/Component.h"
#include <string>

namespace Geni
{

// A sound attached to an object (docs/design/0012): follows the object in 3D when
// `spatial`, starts by itself when `autoplay`, and can have its volume and pitch changed
// while it plays (a car's engine follows its RPM).
class AudioSourceComponent : public Component
{
    COMPONENT(AudioSourceComponent)

  public:
    void Init() override;
    void Update(float deltaTime) override;
    void OnDestroy() override;

    void Play();
    void Stop(float fadeSeconds = 0.0f);
    bool IsPlaying() const;

    std::string GetClip() const
    {
        return m_clip;
    }
    void SetClip(const std::string &clip);
    float GetVolume() const
    {
        return m_params.volume;
    }
    void SetVolume(float volume);
    float GetPitch() const
    {
        return m_params.pitch;
    }
    void SetPitch(float pitch);
    bool GetLoop() const
    {
        return m_params.loop;
    }
    void SetLoop(bool loop)
    {
        m_params.loop = loop;
    }
    bool GetSpatial() const
    {
        return m_params.spatial;
    }
    void SetSpatial(bool spatial)
    {
        m_params.spatial = spatial;
    }
    float GetMinDistance() const
    {
        return m_params.minDistance;
    }
    void SetMinDistance(float value)
    {
        m_params.minDistance = value;
    }
    float GetMaxDistance() const
    {
        return m_params.maxDistance;
    }
    void SetMaxDistance(float value)
    {
        m_params.maxDistance = value;
    }
    bool GetAutoplay() const
    {
        return m_autoplay;
    }
    void SetAutoplay(bool autoplay)
    {
        m_autoplay = autoplay;
    }
    bool GetStream() const
    {
        return m_params.stream;
    }
    void SetStream(bool stream)
    {
        m_params.stream = stream;
    }
    bool GetMusic() const
    {
        return m_params.bus == AudioBus::Music;
    }
    void SetMusic(bool music)
    {
        m_params.bus = music ? AudioBus::Music : AudioBus::Effects;
    }

  private:
    std::string m_clip;
    SoundParams m_params;
    bool m_autoplay = false;
    bool m_started = false;
    SoundId m_sound = InvalidSound;
};

} // namespace Geni
