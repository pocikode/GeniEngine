#include "scene/components/CameraComponent.h"
#include "glm/ext/matrix_clip_space.hpp"
#include "glm/ext/matrix_float4x4.hpp"
#include "glm/ext/matrix_transform.hpp"
#include "glm/gtc/quaternion.hpp"
#include "glm/trigonometric.hpp"
#include "scene/GameObject.h"
#include "scene/Reflection.h"
#include <glm/gtc/matrix_transform.hpp>

namespace Geni
{

namespace
{
const bool s_registered = RegisterComponentType<CameraComponent>(
    "CameraComponent",
    {
        FloatProperty<CameraComponent>("fov", &CameraComponent::GetFieldOfView, &CameraComponent::SetFieldOfView),
        FloatProperty<CameraComponent>("near", &CameraComponent::GetNearPlane, &CameraComponent::SetNearPlane),
        FloatProperty<CameraComponent>("far", &CameraComponent::GetFarPlane, &CameraComponent::SetFarPlane),
        EnumProperty<CameraComponent, CameraProjection>(
            "projection", &CameraComponent::GetProjection, &CameraComponent::SetProjection,
            {{CameraProjection::Perspective, "perspective"}, {CameraProjection::Orthographic, "orthographic"}}
        ),
        FloatProperty<CameraComponent>("orthoSize", &CameraComponent::GetOrthoSize, &CameraComponent::SetOrthoSize),
    }
);
} // namespace

void CameraComponent::Update(float deltaTime)
{
}

glm::mat4 CameraComponent::GetViewMatrix() const
{
    glm::mat4 mat = glm::mat4(1.0f);
    mat = glm::mat4_cast(m_owner->GetRotation());
    mat[3] = glm::vec4(m_owner->GetPosition(), 1.0f);

    if (m_owner->GetParent())
    {
        mat = m_owner->GetParent()->GetWorldTransform() * mat;
    }

    return glm::inverse(mat);
}

glm::mat4 CameraComponent::GetProjectionMatrix(float aspect) const
{
    if (m_projection == CameraProjection::Orthographic)
    {
        const float halfHeight = 0.5f * m_orthoSize;
        const float halfWidth = halfHeight * aspect;
        return glm::ortho(-halfWidth, halfWidth, -halfHeight, halfHeight, m_nearPlane, m_farPlane);
    }
    return glm::perspective(glm::radians(m_fov), aspect, m_nearPlane, m_farPlane);
}

} // namespace Geni

namespace Geni
{
void LinkCameraComponent()
{
}
} // namespace Geni
