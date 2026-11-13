#pragma once

#include "scene/Component.h"
#include "scene/Timeline.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace Geni
{

class GameObject;

// Plays a timeline (docs/design/0026): transform tracks move named objects, camera cuts
// switch the scene's main camera, sound cues play, and events queue for the game.
// EditUpdate applies the transform tracks at `time` only, so the editor previews without
// side effects.
class TimelineComponent : public Component
{
    COMPONENT(TimelineComponent);

  public:
    void Init() override;
    void Update(float deltaTime) override;
    void EditUpdate() override;

    const TimelineData &GetData() const
    {
        return m_data;
    }
    void SetData(const TimelineData &data);
    // The data as JSON text (the reflected `tracks` property).
    std::string GetTracksText() const;
    void SetTracksText(const std::string &text);

    float GetTime() const
    {
        return m_time;
    }
    // Jumps without firing what lies between.
    void SetTime(float seconds);
    float GetDuration() const
    {
        return m_duration;
    }
    // 0: the last key's time.
    void SetDuration(float seconds);
    float GetLength() const;
    bool GetLoop() const
    {
        return m_loop;
    }
    void SetLoop(bool loop)
    {
        m_loop = loop;
    }
    bool GetAutoplay() const
    {
        return m_autoplay;
    }
    void SetAutoplay(bool autoplay)
    {
        m_autoplay = autoplay;
    }
    float GetSpeed() const
    {
        return m_speed;
    }
    void SetSpeed(float speed);

    void Play();
    void Pause()
    {
        m_playing = false;
    }
    void Stop();
    bool IsPlaying() const
    {
        return m_playing;
    }
    // Moves the playhead by `deltaTime` and fires what it passes (what Update does while
    // playing); the editor's preview calls ApplyAt instead.
    void Advance(float deltaTime);
    // Sets every transform track's object to its pose at `time`.
    void ApplyAt(float time);
    // Event names fired since the last call.
    std::vector<std::string> TakeEvents();

  private:
    GameObject *FindTarget(const std::string &name);
    void Fire(float from, float to);

    TimelineData m_data;
    float m_time = 0.0f;
    float m_duration = 0.0f;
    float m_speed = 1.0f;
    bool m_loop = false;
    bool m_autoplay = false;
    bool m_playing = false;
    std::vector<std::string> m_events;
    std::map<std::string, uint64_t> m_targets;
};

} // namespace Geni
