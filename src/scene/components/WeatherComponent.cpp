#include "scene/components/WeatherComponent.h"

#include "core/Engine.h"
#include "render/RenderQueue.h"
#include "render/Renderer.h"
#include "scene/GameObject.h"
#include "scene/Reflection.h"
#include "scene/Scene.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/mat4x4.hpp>
#include <glm/matrix.hpp>

#include <algorithm>
#include <cmath>

namespace Geni
{

namespace
{

// A hard ceiling so a typo in the inspector cannot allocate gigabytes.
constexpr int kDropLimit = 200000;

float Clamp01(float value)
{
    return std::isfinite(value) ? glm::clamp(value, 0.0f, 1.0f) : 0.0f;
}

float Approach(float current, float target, float step)
{
    return current < target ? std::min(target, current + step) : std::max(target, current - step);
}

// Wraps `value` into [low, low + span).
float Wrap(float value, float low, float span)
{
    float t = std::fmod(value - low, span);
    if (t < 0.0f)
    {
        t += span;
    }
    return low + t;
}

// The camera's position (scene space) and right/up axes this frame: the editor's view
// when it overrides the scene's camera, else the main camera.
bool FindView(glm::vec3 &position, glm::vec3 &right, glm::vec3 &up)
{
    Engine &engine = Engine::GetInstance();
    if (engine.HasViewOverride())
    {
        const glm::mat4 cameraWorld = glm::inverse(engine.GetViewOverride().viewMatrix);
        position = engine.GetViewOverride().position;
        right = glm::normalize(glm::vec3(cameraWorld[0]));
        up = glm::normalize(glm::vec3(cameraWorld[1]));
        return true;
    }
    Scene *scene = engine.GetScene();
    GameObject *camera = scene ? scene->GetMainCamera() : nullptr;
    if (!camera)
    {
        return false;
    }
    const glm::mat4 world = camera->GetWorldTransform();
    position = glm::vec3(world[3]);
    right = glm::normalize(glm::vec3(world[0]));
    up = glm::normalize(glm::vec3(world[1]));
    return true;
}

} // namespace

void WeatherComponent::SetTarget(const WeatherState &target)
{
    SetCloudCover(target.cloudCover);
    SetFog(target.fog);
    SetPrecipitation(target.precipitation);
    SetSnow(target.snow);
    SetWind(target.wind);
}

void WeatherComponent::SetCloudCover(float amount)
{
    m_target.cloudCover = Clamp01(amount);
}

void WeatherComponent::SetFog(float amount)
{
    m_target.fog = Clamp01(amount);
}

void WeatherComponent::SetPrecipitation(float amount)
{
    m_target.precipitation = Clamp01(amount);
}

void WeatherComponent::SetWind(const glm::vec3 &wind)
{
    const bool finite = std::isfinite(wind.x) && std::isfinite(wind.y) && std::isfinite(wind.z);
    m_target.wind = finite ? wind : glm::vec3(0.0f);
}

void WeatherComponent::SetTransitionSeconds(float seconds)
{
    m_transitionSeconds = std::isfinite(seconds) ? std::max(0.0f, seconds) : 0.0f;
}

void WeatherComponent::SetMaxDrops(int count)
{
    m_maxDrops = std::clamp(count, 0, kDropLimit);
}

void WeatherComponent::SetRadius(float metres)
{
    m_radius = std::isfinite(metres) ? std::max(1.0f, metres) : 30.0f;
}

void WeatherComponent::SetHeight(float metres)
{
    m_height = std::isfinite(metres) ? std::max(1.0f, metres) : 25.0f;
}

void WeatherComponent::Snap()
{
    m_current = m_target;
}

float WeatherComponent::Random01()
{
    m_random ^= m_random << 13;
    m_random ^= m_random >> 17;
    m_random ^= m_random << 5;
    return static_cast<float>(m_random >> 8) * (1.0f / 16777216.0f);
}

void WeatherComponent::ResetDrops(const glm::vec3 &viewPosition)
{
    m_drops.resize(static_cast<size_t>(m_maxDrops));
    for (Drop &drop : m_drops)
    {
        drop.position = viewPosition + glm::vec3(
                                           (Random01() * 2.0f - 1.0f) * m_radius, (Random01() - 0.5f) * m_height,
                                           (Random01() * 2.0f - 1.0f) * m_radius
                                       );
        drop.phase = Random01() * 6.2831853f;
        drop.speed = m_current.snow ? 0.8f + 0.7f * Random01() : 8.0f + 3.0f * Random01();
    }
    m_dropsSnow = m_current.snow;
    m_dropsPlaced = true;
}

void WeatherComponent::Simulate(float deltaTime, const glm::vec3 &viewPosition)
{
    if (!std::isfinite(deltaTime) || deltaTime < 0.0f)
    {
        return;
    }
    m_time += deltaTime;
    if (!m_started)
    {
        // A scene loads with its weather already there, not fading in from clear.
        Snap();
        m_started = true;
    }

    // Ease towards the target. Rain does not turn into snow in the air: it stops first.
    const float step = m_transitionSeconds > 0.0f ? deltaTime / m_transitionSeconds : 1.0f;
    m_current.cloudCover = Approach(m_current.cloudCover, m_target.cloudCover, step);
    m_current.fog = Approach(m_current.fog, m_target.fog, step);
    if (m_current.snow != m_target.snow)
    {
        m_current.precipitation = Approach(m_current.precipitation, 0.0f, step);
        if (m_current.precipitation <= 0.0f)
        {
            m_current.snow = m_target.snow;
        }
    }
    else
    {
        m_current.precipitation = Approach(m_current.precipitation, m_target.precipitation, step);
    }
    const float windStep = step * 10.0f; // m/s per unit of transition
    for (int axis = 0; axis < 3; ++axis)
    {
        m_current.wind[axis] = Approach(m_current.wind[axis], m_target.wind[axis], windStep);
    }

    // The drops: fall, drift with the wind, wrap around the box that follows the view.
    m_viewPosition = viewPosition;
    if (!m_dropsPlaced || m_drops.size() != static_cast<size_t>(m_maxDrops) || m_dropsSnow != m_current.snow)
    {
        ResetDrops(viewPosition);
    }
    const size_t active = std::min(
        m_drops.size(), static_cast<size_t>(std::lround(m_current.precipitation * static_cast<float>(m_maxDrops)))
    );
    const float span = 2.0f * m_radius;
    for (size_t i = 0; i < active; ++i)
    {
        Drop &drop = m_drops[i];
        glm::vec3 velocity = m_current.wind + glm::vec3(0.0f, -drop.speed, 0.0f);
        if (m_current.snow)
        {
            // Flakes wander.
            velocity.x += 0.4f * std::sin(m_time * 1.1f + drop.phase);
            velocity.z += 0.4f * std::cos(m_time * 0.9f + drop.phase * 1.7f);
        }
        drop.position += velocity * deltaTime;
        drop.position.x = Wrap(drop.position.x, viewPosition.x - m_radius, span);
        drop.position.y = Wrap(drop.position.y, viewPosition.y - 0.5f * m_height, m_height);
        drop.position.z = Wrap(drop.position.z, viewPosition.z - m_radius, span);
    }
    m_quads.resize(active);
    for (size_t i = 0; i < active; ++i)
    {
        m_quads[i].center = m_drops[i].position;
    }
}

void WeatherComponent::BuildQuads(const glm::vec3 &cameraRight)
{
    // Rain streaks lean along their velocity as the camera sees it sideways.
    const glm::vec3 wind = m_current.wind;
    const glm::vec4 rain(0.55f, 0.6f, 0.7f, 0.35f);
    const glm::vec4 snow(0.95f, 0.95f, 1.0f, 0.9f);
    for (size_t i = 0; i < m_quads.size(); ++i)
    {
        const Drop &drop = m_drops[i];
        SpriteQuad &quad = m_quads[i];
        if (m_current.snow)
        {
            quad.min = glm::vec2(-0.04f);
            quad.max = glm::vec2(0.04f);
            quad.rotation = drop.phase;
            quad.color = snow;
        }
        else
        {
            quad.min = glm::vec2(-0.012f, -0.4f);
            quad.max = glm::vec2(0.012f, 0.4f);
            quad.rotation = std::atan2(glm::dot(wind, cameraRight), drop.speed);
            quad.color = rain;
        }
    }
}

void WeatherComponent::Update(float deltaTime)
{
    glm::vec3 position(0.0f);
    glm::vec3 right(1.0f, 0.0f, 0.0f);
    glm::vec3 up(0.0f, 1.0f, 0.0f);
    const bool hasView = FindView(position, right, up);
    Simulate(deltaTime, hasView ? position : m_viewPosition);

    Engine &engine = Engine::GetInstance();
    engine.GetRenderer().SetWeather(m_current);
    if (!hasView || m_quads.empty())
    {
        return;
    }
    BuildQuads(right);
    SpriteCommand command;
    command.quads = m_quads.data();
    command.quadCount = static_cast<uint32_t>(m_quads.size());
    command.billboard = true;
    // Thin streaks are not worth sorting precisely, but alpha blending needs it; the
    // renderer sorts billboard quads back to front.
    command.blend = SpriteBlend::Alpha;
    command.boundsRadius = 0.0f; // around the camera: never culled
    engine.GetRenderQueue().SubmitSprites(command);
}

void WeatherComponent::EditUpdate()
{
    Snap();
    m_quads.clear();
    Engine::GetInstance().GetRenderer().SetWeather(m_current);
}

void WeatherComponent::OnOriginShift(const glm::vec3 &offset)
{
    for (Drop &drop : m_drops)
    {
        drop.position -= offset;
    }
    m_viewPosition -= offset;
}

namespace
{
using W = WeatherComponent;
const bool s_registered = RegisterComponentType<WeatherComponent>(
    "WeatherComponent", {
                            FloatProperty<W>("cloudCover", &W::GetCloudCover, &W::SetCloudCover),
                            FloatProperty<W>("fog", &W::GetFog, &W::SetFog),
                            FloatProperty<W>("precipitation", &W::GetPrecipitation, &W::SetPrecipitation),
                            BoolProperty<W>("snow", &W::GetSnow, &W::SetSnow),
                            Vec3Property<W>("wind", &W::GetWind, &W::SetWind),
                            FloatProperty<W>("transitionSeconds", &W::GetTransitionSeconds, &W::SetTransitionSeconds),
                            IntProperty<W>("maxDrops", &W::GetMaxDrops, &W::SetMaxDrops),
                            FloatProperty<W>("radius", &W::GetRadius, &W::SetRadius),
                            FloatProperty<W>("height", &W::GetHeight, &W::SetHeight),
                        }
);
} // namespace

void LinkWeatherComponent()
{
}

} // namespace Geni
