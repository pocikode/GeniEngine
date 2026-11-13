#pragma once

#include "assets/AssetHandle.h"
#include "scene/Component.h"
#include "world/Foliage.h"

#include <glm/mat4x4.hpp>

#include <vector>

namespace Geni
{

class Material;
class Mesh;

// Draws many instances of a plant prefab without a GameObject each (docs/design/0023).
// The prefab's MeshComponents are the parts drawn for every instance; instances come from
// a cooked foliage asset (the terrain cooker's scatter) or SetInstances. Cells of 32 m
// beyond drawDistance from the view are skipped; the renderer culls, instances and
// shadows the rest as usual.
class FoliageComponent : public Component
{
    COMPONENT(FoliageComponent);

  public:
    static constexpr float CellSize = 32.0f;

    void Update(float deltaTime) override;
    void EditUpdate() override
    {
        Draw();
    }
    void ResolveAssets(SerializationContext &context) override;

    AssetID GetPrefabID() const
    {
        return m_prefabID;
    }
    // Changing the ID drops the loaded parts; ResolveAssets loads them again.
    void SetPrefabID(AssetID id);
    AssetID GetInstancesID() const
    {
        return m_instancesID;
    }
    void SetInstancesID(AssetID id);
    float GetDrawDistance() const
    {
        return m_drawDistance;
    }
    void SetDrawDistance(float metres);

    // Replaces the instances (in the owner's space).
    void SetInstances(std::vector<FoliageInstance> instances);
    const std::vector<FoliageInstance> &GetInstances() const
    {
        return m_instances;
    }
    // One drawn part of the plant.
    void AddPart(const AssetHandle<Mesh> &mesh, const AssetHandle<Material> &material, const glm::mat4 &local);
    size_t GetPartCount() const
    {
        return m_parts.size();
    }
    // Instances submitted by the last Update or EditUpdate.
    size_t GetDrawnInstances() const
    {
        return m_drawn;
    }

  private:
    struct Part
    {
        AssetHandle<Mesh> mesh;
        AssetHandle<Material> material;
        glm::mat4 local{1.0f};
    };
    struct Cell
    {
        glm::vec3 center{0.0f};
        float radius = 0.0f;
        uint32_t first = 0;
        uint32_t count = 0;
    };

    void Draw();
    void BuildCells();

    AssetID m_prefabID;
    AssetID m_instancesID;
    float m_drawDistance = 350.0f;
    bool m_resolvedPrefab = false;
    bool m_resolvedInstances = false;

    std::vector<Part> m_parts;
    std::vector<FoliageInstance> m_instances;
    // Per instance: translation * yaw * scale, in the owner's space, in cell order.
    std::vector<glm::mat4> m_matrices;
    std::vector<Cell> m_cells;
    size_t m_drawn = 0;
};

} // namespace Geni
