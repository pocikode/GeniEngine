#pragma once

#include "glm/ext/vector_float3.hpp"
#include "scene/Component.h"

namespace Geni
{

class LightComponent : public Component
{
    COMPONENT(LightComponent);

  public:
    void Update(float deltaTime) override;

    void SetColor(const glm::vec3 &color);
    const glm::vec3 &GetColor() const;

  private:
    glm::vec3 m_color = glm::vec3(1.0f);
};

} // namespace Geni