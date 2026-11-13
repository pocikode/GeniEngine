#pragma once

#include "render/TimeOfDay.h"
#include "scene/Component.h"

namespace Geni
{

// A day-night cycle (docs/design/0018). Put it on the object with the directional
// LightComponent: every frame it turns that object to face along the sun's (or at night
// the moon's) light, sets the light's colour and intensity, and gives the renderer the
// sky, fog and ambient for the hour. The light is driven; rotating it by hand has no
// lasting effect.
//
// `time` is in hours (0..24). `dayLengthMinutes` is how many real minutes a game day
// lasts; 0 stops the clock. In the editor the clock does not run, but changing `time`
// relights the viewport.
class TimeOfDayComponent : public Component
{
    COMPONENT(TimeOfDayComponent)

  public:
    void Update(float deltaTime) override;
    void EditUpdate() override;

    float GetTime() const
    {
        return m_time;
    }
    // Wrapped into 0..24.
    void SetTime(float hours);

    float GetDayLengthMinutes() const
    {
        return m_dayLengthMinutes;
    }
    void SetDayLengthMinutes(float minutes);

    const TimeOfDaySettings &GetSettings() const
    {
        return m_settings;
    }
    void SetSettings(const TimeOfDaySettings &settings)
    {
        m_settings = settings;
    }

    float GetMaxElevation() const
    {
        return m_settings.maxElevation;
    }
    void SetMaxElevation(float degrees);
    float GetPathYaw() const
    {
        return m_settings.pathYaw;
    }
    void SetPathYaw(float degrees)
    {
        m_settings.pathYaw = degrees;
    }
    float GetSunIntensity() const
    {
        return m_settings.sunIntensity;
    }
    void SetSunIntensity(float intensity);
    float GetMoonIntensity() const
    {
        return m_settings.moonIntensity;
    }
    void SetMoonIntensity(float intensity);
    float GetFogDensity() const
    {
        return m_settings.fogDensity;
    }
    void SetFogDensity(float density);

    // What the last Update or EditUpdate applied.
    const TimeOfDayState &GetState() const
    {
        return m_state;
    }

    // Evaluates the current time and applies it to the owner, its light and the renderer.
    void Apply();

  private:
    float m_time = 12.0f;
    float m_dayLengthMinutes = 24.0f;
    TimeOfDaySettings m_settings;
    TimeOfDayState m_state;
    bool m_warnedNoLight = false;
};

} // namespace Geni
