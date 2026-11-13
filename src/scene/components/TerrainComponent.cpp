#include "scene/components/TerrainComponent.h"

#include "assets/AssetManager.h"
#include "core/Engine.h"
#include "physics/Collider.h"
#include "physics/PhysicsManager.h"
#include "physics/RigidBody.h"
#include "scene/GameObject.h"
#include "scene/Reflection.h"
#include "scene/Scene.h"

#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Geni
{

TerrainComponent::TerrainComponent() = default;

TerrainComponent::~TerrainComponent()
{
    DestroyBody();
}

void TerrainComponent::SetHeightsID(AssetID id)
{
    if (id != m_heightsID)
    {
        m_heights.Reset();
        DestroyBody();
    }
    m_heightsID = id;
}

void TerrainComponent::SetHeights(const AssetHandle<HeightfieldAsset> &heights)
{
    DestroyBody();
    m_heights = heights;
    m_heightsID = heights.GetID();
    if (m_initialized)
    {
        CreateBody();
    }
}

bool TerrainComponent::IsReady() const
{
    return m_heights.IsReady();
}

void TerrainComponent::Init()
{
    m_initialized = true;
    CreateBody();
}

void TerrainComponent::Update(float deltaTime)
{
    (void)deltaTime;
    // Heights that finished loading after Init.
    if (!m_body && m_heights.IsReady())
    {
        CreateBody();
    }
}

void TerrainComponent::OnDestroy()
{
    DestroyBody();
}

void TerrainComponent::ResolveAssets(SerializationContext &context)
{
    if (context.assets && m_heightsID.IsValid() && !m_heights.IsValid())
    {
        // A few hundred kilobytes: read it now so collision exists with the first frame.
        SetHeights(context.assets->LoadHeightfield(m_heightsID, false));
    }
}

float TerrainComponent::GetExtent() const
{
    return m_heights.IsReady() ? m_heights->heights.Extent() : 0.0f;
}

bool TerrainComponent::GetHeight(const glm::vec3 &scenePosition, float &height) const
{
    if (!m_heights.IsReady() || !m_owner)
    {
        return false;
    }
    const glm::mat4 world = m_owner->GetWorldTransform();
    const glm::vec3 local(glm::inverse(world) * glm::vec4(scenePosition, 1.0f));
    const float h = m_heights->heights.HeightAt(local.x, local.z);
    height = glm::vec3(world * glm::vec4(local.x, h, local.z, 1.0f)).y;
    return true;
}

bool TerrainComponent::GetNormal(const glm::vec3 &scenePosition, glm::vec3 &normal) const
{
    if (!m_heights.IsReady() || !m_owner)
    {
        return false;
    }
    const glm::mat4 world = m_owner->GetWorldTransform();
    const glm::vec3 local(glm::inverse(world) * glm::vec4(scenePosition, 1.0f));
    normal = glm::normalize(glm::mat3(glm::inverseTranspose(world)) * m_heights->heights.NormalAt(local.x, local.z));
    return true;
}

void TerrainComponent::CreateBody()
{
    if (m_body || !m_owner || !m_heights.IsReady())
    {
        return;
    }
    PhysicsManager &physics = Engine::GetInstance().GetPhysicsManager();
    if (!physics.IsInitialized())
    {
        return;
    }
    const HeightfieldData &field = m_heights->heights;
    auto collider = std::make_shared<HeightFieldCollider>(field.heights, field.samples, field.cellSize);
    m_body = std::make_unique<RigidBody>(BodyType::Static, collider, 0.0f, 0.8f);
    const glm::mat4 world = m_owner->GetWorldTransform();
    const Scene *scene = m_owner->GetScene();
    const glm::vec3 position(world[3]);
    m_body->SetScale(
        glm::vec3(glm::length(glm::vec3(world[0])), glm::length(glm::vec3(world[1])), glm::length(glm::vec3(world[2])))
    );
    m_body->SetWorldPosition(scene ? scene->ToWorld(position) : glm::dvec3(position));
    m_body->SetRotation(m_owner->GetWorldRotation());
    m_body->SetUserData(m_owner->GetID());
    physics.AddRigidBody(m_body.get());
}

void TerrainComponent::DestroyBody()
{
    if (!m_body)
    {
        return;
    }
    PhysicsManager &physics = Engine::GetInstance().GetPhysicsManager();
    if (physics.IsInitialized())
    {
        physics.RemoveRigidBody(m_body.get());
    }
    m_body.reset();
}

namespace
{
const bool s_registered = RegisterComponentType<TerrainComponent>(
    "TerrainComponent",
    {
        AssetIDProperty<TerrainComponent>(
            "heights", AssetType::Heightfield, &TerrainComponent::GetHeightsID, &TerrainComponent::SetHeightsID
        ),
    }
);
} // namespace

void LinkTerrainComponent()
{
}

} // namespace Geni
