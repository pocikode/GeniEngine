#pragma once

#include "glm/ext/matrix_float4x4.hpp"
#include "glm/ext/vector_float3.hpp"

namespace Geni
{

struct CameraData
{
    glm::mat4 viewMatrix;
    glm::mat4 projectionMatrix;
};

struct LightData
{
    glm::vec3 color;
    glm::vec3 position;
};

} // namespace Geni