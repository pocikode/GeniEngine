#include "scene/components/LightComponent.h"
#include "glm/geometric.hpp"
#include "scene/GameObject.h"

namespace Geni
{

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