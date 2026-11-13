#include "scene/components/TimeOfDayComponent.h"

#include "core/Engine.h"
#include "core/Log.h"
#include "glm/common.hpp"
#include "glm/gtc/quaternion.hpp"
#include "render/Renderer.h"
#include "scene/GameObject.h"
#include "scene/Reflection.h"
#include "scene/components/LightComponent.h"

#include <algorithm>
#include <cmath>

namespace Geni
{

namespace
{
const bool s_registered = RegisterComponentType<TimeOfDayComponent>(
    "TimeOfDayComponent",
    {
        FloatProperty<TimeOfDayComponent>("time", &TimeOfDayComponent::GetTime, &TimeOfDayComponent::SetTime),
        FloatProperty<TimeOfDayComponent>(
            "dayLengthMinutes", &TimeOfDayComponent::GetDayLengthMinutes, &TimeOfDayComponent::SetDayLengthMinutes
        ),
        FloatProperty<TimeOfDayComponent>(
            "maxElevation", &TimeOfDayComponent::GetMaxElevation, &TimeOfDayComponent::SetMaxElevation
        ),
        FloatProperty<TimeOfDayComponent>("pathYaw", &TimeOfDayComponent::GetPathYaw, &TimeOfDayComponent::SetPathYaw),
        FloatProperty<TimeOfDayComponent>(
            "sunIntensity", &TimeOfDayComponent::GetSunIntensity, &TimeOfDayComponent::SetSunIntensity
        ),
        FloatProperty<TimeOfDayComponent>(
            "moonIntensity", &TimeOfDayComponent::GetMoonIntensity, &TimeOfDayComponent::SetMoonIntensity
        ),
        FloatProperty<TimeOfDayComponent>(
            "fogDensity", &TimeOfDayComponent::GetFogDensity, &TimeOfDayComponent::SetFogDensity
        ),
    }
);

// A rotation whose local -Z axis points along `direction` (unit length).
glm::quat FacingRotation(const glm::vec3 &direction)
{
    const glm::vec3 up = std::abs(direction.y) > 0.999f ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
    return glm::quatLookAtRH(direction, up);
}
} // namespace

void TimeOfDayComponent::SetTime(float hours)
{
    m_time = WrapHours(hours);
}

void TimeOfDayComponent::SetDayLengthMinutes(float minutes)
{
    m_dayLengthMinutes = std::isfinite(minutes) ? std::max(0.0f, minutes) : 0.0f;
}

void TimeOfDayComponent::SetMaxElevation(float degrees)
{
    m_settings.maxElevation = glm::clamp(degrees, 1.0f, 90.0f);
}

void TimeOfDayComponent::SetSunIntensity(float intensity)
{
    m_settings.sunIntensity = std::max(0.0f, intensity);
}

void TimeOfDayComponent::SetMoonIntensity(float intensity)
{
    m_settings.moonIntensity = std::max(0.0f, intensity);
}

void TimeOfDayComponent::SetFogDensity(float density)
{
    m_settings.fogDensity = std::max(0.0f, density);
}

void TimeOfDayComponent::Update(float deltaTime)
{
    if (m_dayLengthMinutes > 0.0f && deltaTime > 0.0f)
    {
        // A day of 24 game hours lasts dayLengthMinutes * 60 real seconds.
        SetTime(m_time + deltaTime * 24.0f / (m_dayLengthMinutes * 60.0f));
    }
    Apply();
}

void TimeOfDayComponent::EditUpdate()
{
    Apply();
}

void TimeOfDayComponent::Apply()
{
    m_state = EvaluateTimeOfDay(m_time, m_settings);
    if (!m_owner)
    {
        return;
    }

    LightComponent *light = m_owner->GetComponent<LightComponent>();
    if (light && light->GetType() == LightType::Directional)
    {
        const glm::quat world = FacingRotation(m_state.lightDirection);
        GameObject *parent = m_owner->GetParent();
        m_owner->SetRotation(parent ? glm::inverse(parent->GetWorldRotation()) * world : world);
        light->SetColor(m_state.lightColor);
        light->SetIntensity(m_state.lightIntensity);
    }
    else if (!m_warnedNoLight)
    {
        GENI_LOG_WARN(
            "TimeOfDayComponent on '%s': no directional LightComponent on the same object; only the sky changes",
            m_owner->GetName().c_str()
        );
        m_warnedNoLight = true;
    }

    Engine::GetInstance().GetRenderer().SetAtmosphereOverride(m_state.atmosphere);
}

} // namespace Geni

namespace Geni
{
void LinkTimeOfDayComponent()
{
}
} // namespace Geni
