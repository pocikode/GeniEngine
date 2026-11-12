#include "scene/components/LightComponent.h"
#include "glm/geometric.hpp"
#include "scene/GameObject.h"
#include "scene/Reflection.h"

namespace Geni
{

namespace
{
const bool s_registered = RegisterComponentType<LightComponent>(
    "LightComponent",
    {
        EnumProperty<LightComponent, LightType>(
            "type", &LightComponent::GetType, &LightComponent::SetType,
            {{LightType::Directional, "directional"}, {LightType::Point, "point"}, {LightType::Spot, "spot"}}
        ),
        Vec3Property<LightComponent>(
            "color", [](const LightComponent &light) { return light.GetColor(); },
            [](LightComponent &light, const glm::vec3 &value) { light.SetColor(value); }
        ),
        FloatProperty<LightComponent>("intensity", &LightComponent::GetIntensity, &LightComponent::SetIntensity),
        FloatProperty<LightComponent>("range", &LightComponent::GetRange, &LightComponent::SetRange),
        FloatProperty<LightComponent>(
            "innerConeAngle", &LightComponent::GetInnerConeAngle, &LightComponent::SetInnerConeAngle
        ),
        FloatProperty<LightComponent>(
            "outerConeAngle", &LightComponent::GetOuterConeAngle, &LightComponent::SetOuterConeAngle
        ),
    }
);
} // namespace

void LightComponent::Update(float deltaTime)
{
}

void LightComponent::SetType(LightType type)
{
    m_type = type;
}

void LightComponent::SetColor(const glm::vec3 &color)
{
    m_color = color;
}

void LightComponent::SetIntensity(float intensity)
{
    m_intensity = intensity;
}

void LightComponent::SetRange(float range)
{
    m_range = range;
}

void LightComponent::SetConeAngles(float innerRadians, float outerRadians)
{
    m_innerConeAngle = innerRadians;
    m_outerConeAngle = outerRadians;
}

glm::vec3 LightComponent::GetDirection() const
{
    if (!m_owner)
    {
        return glm::vec3(0.0f, -1.0f, 0.0f);
    }
    // The owner's local -Z axis with the world rotation applied.
    const glm::vec3 direction = m_owner->GetWorldRotation() * glm::vec3(0.0f, 0.0f, -1.0f);
    const float length = glm::length(direction);
    return length > 1e-6f ? direction / length : glm::vec3(0.0f, -1.0f, 0.0f);
}

} // namespace Geni

namespace Geni
{
void LinkLightComponent()
{
}
} // namespace Geni
