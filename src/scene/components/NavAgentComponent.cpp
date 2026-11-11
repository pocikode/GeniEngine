#include "scene/components/NavAgentComponent.h"

#include "core/Engine.h"
#include "scene/GameObject.h"
#include "scene/Reflection.h"
#include "scene/Scene.h"
#include "scene/components/AnimatorComponent.h"

#include <algorithm>
#include <cmath>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Geni
{

namespace
{
float WrapAngle(float angle)
{
    while (angle > glm::pi<float>())
    {
        angle -= glm::two_pi<float>();
    }
    while (angle < -glm::pi<float>())
    {
        angle += glm::two_pi<float>();
    }
    return angle;
}
} // namespace

void NavAgentComponent::OnDestroy()
{
    Leave();
}

void NavAgentComponent::Leave()
{
    NavigationSystem &navigation = Engine::GetInstance().GetNavigation();
    if (m_agent >= 0 && m_generation == navigation.GetGeneration())
    {
        navigation.RemoveAgent(m_agent);
    }
    m_agent = -1;
}

void NavAgentComponent::SetMaxSpeed(float value)
{
    m_params.maxSpeed = value;
    if (m_agent >= 0)
    {
        Engine::GetInstance().GetNavigation().SetAgentMaxSpeed(m_agent, value);
    }
}

bool NavAgentComponent::MoveTo(const glm::vec3 &scenePosition)
{
    if (m_agent < 0)
    {
        // Not on the mesh yet: go there once it is.
        m_pendingTarget = scenePosition;
        m_hasPendingTarget = true;
        m_arrived = false;
        return true;
    }
    const Scene *scene = m_owner->GetScene();
    const glm::dvec3 world = scene ? scene->ToWorld(scenePosition) : glm::dvec3(scenePosition);
    const bool ok = Engine::GetInstance().GetNavigation().SetAgentTarget(m_agent, world);
    m_arrived = !ok;
    m_hasPendingTarget = false;
    return ok;
}

void NavAgentComponent::Stop()
{
    m_hasPendingTarget = false;
    m_arrived = true;
    if (m_agent >= 0)
    {
        Engine::GetInstance().GetNavigation().StopAgent(m_agent);
    }
}

void NavAgentComponent::Update(float deltaTime)
{
    NavigationSystem &navigation = Engine::GetInstance().GetNavigation();
    Scene *scene = m_owner->GetScene();
    if (!navigation.IsInitialized() || !scene)
    {
        return;
    }
    if (m_agent >= 0 && m_generation != navigation.GetGeneration())
    {
        m_agent = -1; // the crowd was rebuilt
    }

    if (m_agent < 0)
    {
        m_retry -= deltaTime;
        if (m_retry > 0.0f)
        {
            return;
        }
        m_retry = 0.5f;
        m_agent = navigation.AddAgent(scene->ToWorld(m_owner->GetWorldPosition()), m_params);
        m_generation = navigation.GetGeneration();
        if (m_agent < 0)
        {
            return;
        }
        if (m_hasPendingTarget)
        {
            MoveTo(m_pendingTarget);
        }
    }

    const NavAgentState state = navigation.GetAgent(m_agent);
    if (!state.valid)
    {
        // The mesh under it went away (its cell unloaded): leave, rejoin when it returns.
        Leave();
        return;
    }
    m_arrived = !m_hasPendingTarget && state.arrived;

    glm::vec3 position = scene->ToScene(state.position);
    if (GameObject *parent = m_owner->GetParent())
    {
        position = glm::vec3(glm::inverse(parent->GetWorldTransform()) * glm::vec4(position, 1.0f));
    }
    m_owner->SetPosition(position);

    m_speed = glm::length(glm::vec2(state.velocity.x, state.velocity.z));
    if (m_speed > 0.2f)
    {
        const float target = std::atan2(-state.velocity.x, -state.velocity.z);
        m_yaw = WrapAngle(m_yaw + WrapAngle(target - m_yaw) * std::min(1.0f, deltaTime * 8.0f));
        m_owner->SetRotation(glm::angleAxis(m_yaw, glm::vec3(0.0f, 1.0f, 0.0f)));
    }

    if (!m_speedParameter.empty())
    {
        if (auto *animator = m_owner->GetComponent<AnimatorComponent>())
        {
            animator->SetFloat(m_speedParameter, m_speed);
        }
    }

    if (m_wanderRadius > 0.0f && m_arrived)
    {
        // Pause a moment at each stop, then pick the next.
        m_wanderWait -= deltaTime;
        if (m_wanderWait <= 0.0f)
        {
            glm::dvec3 next;
            if (navigation.RandomPointNear(state.position, m_wanderRadius, next))
            {
                navigation.SetAgentTarget(m_agent, next);
                m_arrived = false;
            }
            m_wanderWait = 1.0f + static_cast<float>(m_owner->GetID() % 7) * 0.4f;
        }
    }
}

namespace
{
using N = NavAgentComponent;
const bool s_registered = RegisterComponentType<N>(
    "NavAgentComponent",
    {
        FloatProperty<N>("radius", &N::GetRadius, &N::SetRadius),
        FloatProperty<N>("height", &N::GetHeight, &N::SetHeight),
        FloatProperty<N>("maxSpeed", &N::GetMaxSpeed, &N::SetMaxSpeed),
        FloatProperty<N>("maxAcceleration", &N::GetMaxAcceleration, &N::SetMaxAcceleration),
        FloatProperty<N>("wanderRadius", &N::GetWanderRadius, &N::SetWanderRadius),
        StringProperty<N>("speedParameter", &N::GetSpeedParameter, &N::SetSpeedParameter),
    }
);
} // namespace

void LinkNavAgentComponent()
{
}

} // namespace Geni
