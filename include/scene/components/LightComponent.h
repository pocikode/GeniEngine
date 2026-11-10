#pragma once

#include "core/Common.h"
#include "glm/ext/vector_float3.hpp"
#include "scene/Component.h"

namespace Geni
{

// A light in the scene. Directional lights point along the owner's local -Z axis; spot
// lights do the same, with their cone around it. Point lights ignore the rotation.
class LightComponent : public Component
{
    COMPONENT(LightComponent);

  public:
    void Update(float deltaTime) override;

    void SetType(LightType type);
    LightType GetType() const
    {
        return m_type;
    }

    void SetColor(const glm::vec3 &color);
    const glm::vec3 &GetColor() const
    {
        return m_color;
    }

    // Multiplies the colour. 1 is a default-intensity light.
    void SetIntensity(float intensity);
    float GetIntensity() const
    {
        return m_intensity;
    }

    // Point and spot lights: distance at which the light fades out. 0 derives the range
    // from colour and intensity (where the light falls below 1%).
    void SetRange(float range);
    float GetRange() const
    {
        return m_range;
    }

    // Spot lights, in radians.
    void SetConeAngles(float innerRadians, float outerRadians);
    void SetInnerConeAngle(float innerRadians)
    {
        m_innerConeAngle = innerRadians;
    }
    void SetOuterConeAngle(float outerRadians)
    {
        m_outerConeAngle = outerRadians;
    }
    float GetInnerConeAngle() const
    {
        return m_innerConeAngle;
    }
    float GetOuterConeAngle() const
    {
        return m_outerConeAngle;
    }

    // The owner's local -Z axis in world space. Directional and spot lights use it.
    glm::vec3 GetDirection() const;

  private:
    LightType m_type = LightType::Point;
    glm::vec3 m_color{1.0f};
    float m_intensity = 1.0f;
    float m_range = 10.0f;
    float m_innerConeAngle = 0.0f;
    float m_outerConeAngle = 0.785398f;
};

} // namespace Geni
