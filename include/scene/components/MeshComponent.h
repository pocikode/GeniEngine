#pragma once

#include "assets/AssetHandle.h"
#include "scene/Component.h"
#include <memory>

namespace Geni
{

class Material;
class Mesh;

class MeshComponent : public Component
{
    COMPONENT(MeshComponent);

  public:
    MeshComponent() = default;
    MeshComponent(const std::shared_ptr<Material> &material, const std::shared_ptr<Mesh> &mesh);
    void Update(float deltaTime) override;
    void ResolveAssets(SerializationContext &context) override;

    void SetMesh(const std::shared_ptr<Mesh> &mesh);
    void SetMesh(const AssetHandle<Mesh> &mesh);
    void SetMaterial(const std::shared_ptr<Material> &material);
    void SetMaterial(const AssetHandle<Material> &material);

    Mesh *GetMesh() const;
    Material *GetMaterial() const;
    const AssetHandle<Mesh> &GetMeshHandle() const
    {
        return m_meshHandle;
    }
    const AssetHandle<Material> &GetMaterialHandle() const
    {
        return m_materialHandle;
    }

    AssetID GetMeshID() const
    {
        return m_meshID;
    }
    void SetMeshID(AssetID id)
    {
        m_meshID = id;
    }
    AssetID GetMaterialID() const
    {
        return m_materialID;
    }
    void SetMaterialID(AssetID id)
    {
        m_materialID = id;
    }

  private:
    AssetHandle<Material> m_materialHandle;
    AssetHandle<Mesh> m_meshHandle;
    std::shared_ptr<Material> m_material;
    std::shared_ptr<Mesh> m_mesh;
    AssetID m_meshID;
    AssetID m_materialID;
};

} // namespace Geni
