#pragma once

#include "assets/AssetHandle.h"
#include "scene/Component.h"

#include <glm/vec3.hpp>

#include <memory>

namespace Geni
{

class HeightfieldAsset;
class RigidBody;

// A cooked terrain's root (docs/design/0022): loads the height field, answers height and
// normal queries and owns one static height-field body. The chunks that draw it are
// child objects with MeshComponents, made by the cooker.
class TerrainComponent : public Component
{
    COMPONENT(TerrainComponent);

  public:
    TerrainComponent();
    ~TerrainComponent() override;

    void Init() override;
    void Update(float deltaTime) override;
    void OnDestroy() override;
    void ResolveAssets(SerializationContext &context) override;

    AssetID GetHeightsID() const
    {
        return m_heightsID;
    }
    // Changing the ID drops the loaded heights and the collider; ResolveAssets loads them.
    void SetHeightsID(AssetID id);
    void SetHeights(const AssetHandle<HeightfieldAsset> &heights);
    bool IsReady() const;

    // The terrain's height under a scene-space point (its X and Z), in scene space, and
    // the surface normal there. Outside the terrain the nearest edge is used. False until
    // the heights are loaded.
    bool GetHeight(const glm::vec3 &scenePosition, float &height) const;
    bool GetNormal(const glm::vec3 &scenePosition, glm::vec3 &normal) const;
    // The side length in metres (0 until loaded).
    float GetExtent() const;

    // The collider exists (physics initialised and heights loaded).
    bool HasCollider() const
    {
        return m_body != nullptr;
    }

  private:
    void CreateBody();
    void DestroyBody();

    AssetID m_heightsID;
    AssetHandle<HeightfieldAsset> m_heights;
    std::unique_ptr<RigidBody> m_body;
    bool m_initialized = false;
};

} // namespace Geni
