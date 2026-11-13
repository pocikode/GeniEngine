#pragma once

#include "render/Sprite.h"
#include "render/Weather.h"
#include "scene/Component.h"

#include <glm/vec3.hpp>

#include <cstdint>
#include <vector>

namespace Geni
{

// Clouds, fog, rain and snow (docs/design/0020). One per scene, on any object. The
// properties are the target weather; Update eases towards it over transitionSeconds, gives
// the lighting changes to the renderer for the frame and draws rain or snow in a box that
// follows the camera. EditUpdate jumps to the target and previews the lighting only.
class WeatherComponent : public Component
{
    COMPONENT(WeatherComponent);

  public:
    void Update(float deltaTime) override;
    void EditUpdate() override;
    void OnOriginShift(const glm::vec3 &offset) override;

    // Jumps to the target weather.
    void Snap();
    // Advances the weather and the drops around `viewPosition` (scene space) without
    // drawing; Update calls it with the camera's position.
    void Simulate(float deltaTime, const glm::vec3 &viewPosition);

    const WeatherState &GetTarget() const
    {
        return m_target;
    }
    void SetTarget(const WeatherState &target);
    // Where the transition has got to; what the renderer gets.
    const WeatherState &GetCurrent() const
    {
        return m_current;
    }

    float GetCloudCover() const
    {
        return m_target.cloudCover;
    }
    void SetCloudCover(float amount);
    float GetFog() const
    {
        return m_target.fog;
    }
    void SetFog(float amount);
    float GetPrecipitation() const
    {
        return m_target.precipitation;
    }
    void SetPrecipitation(float amount);
    bool GetSnow() const
    {
        return m_target.snow;
    }
    void SetSnow(bool snow)
    {
        m_target.snow = snow;
    }
    glm::vec3 GetWind() const
    {
        return m_target.wind;
    }
    void SetWind(const glm::vec3 &wind);
    // Seconds for a change from 0 to 1 (0: immediate).
    float GetTransitionSeconds() const
    {
        return m_transitionSeconds;
    }
    void SetTransitionSeconds(float seconds);

    // The precipitation box: drops at full precipitation, half-width and height in metres.
    int GetMaxDrops() const
    {
        return m_maxDrops;
    }
    void SetMaxDrops(int count);
    float GetRadius() const
    {
        return m_radius;
    }
    void SetRadius(float metres);
    float GetHeight() const
    {
        return m_height;
    }
    void SetHeight(float metres);

    // Drops drawn last frame (for tests and tools).
    int GetVisibleDrops() const
    {
        return static_cast<int>(m_quads.size());
    }
    const std::vector<SpriteQuad> &GetQuads() const
    {
        return m_quads;
    }

  private:
    struct Drop
    {
        glm::vec3 position{0.0f};
        float speed = 0.0f;
        float phase = 0.0f;
    };

    void ResetDrops(const glm::vec3 &viewPosition);
    void BuildQuads(const glm::vec3 &cameraRight);
    float Random01();

    WeatherState m_target;
    WeatherState m_current;
    float m_transitionSeconds = 20.0f;
    int m_maxDrops = 6000;
    float m_radius = 30.0f;
    float m_height = 25.0f;

    std::vector<Drop> m_drops;
    std::vector<SpriteQuad> m_quads;
    bool m_dropsSnow = false;
    bool m_dropsPlaced = false;
    bool m_started = false;
    glm::vec3 m_viewPosition{0.0f};
    float m_time = 0.0f;
    uint32_t m_random = 0x2545F491u;
};

} // namespace Geni
