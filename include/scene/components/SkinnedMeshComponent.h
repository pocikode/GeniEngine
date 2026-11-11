#pragma once

#include "assets/AssetHandle.h"
#include "scene/Component.h"
#include <memory>

namespace Geni
{

class Material;
class Mesh;
class Skeleton;

class SkinnedMeshComponent : public Component
{
    COMPONENT(SkinnedMeshComponent);

  public:
    SkinnedMeshComponent() = default;
    SkinnedMeshComponent(
        const std::shared_ptr<Material> &material, const std::shared_ptr<Mesh> &mesh,
        const std::shared_ptr<Skeleton> &skeleton
    );

    void Update(float deltaTime) override;
    // An edited scene is still drawn.
    void EditUpdate() override
    {
        Update(0.0f);
    }
    void ResolveAssets(SerializationContext &context) override;

    const std::shared_ptr<Skeleton> &GetSkeleton() const;
    void SetSkeleton(const std::shared_ptr<Skeleton> &skeleton);
    void SetMesh(const std::shared_ptr<Mesh> &mesh);
    void SetMesh(const AssetHandle<Mesh> &mesh);
    void SetMaterial(const std::shared_ptr<Material> &material);
    void SetMaterial(const AssetHandle<Material> &material);

    Mesh *GetMesh() const;
    Material *GetMaterial() const;
    AssetID GetMeshID() const
    {
        return m_meshID;
    }
    // Changing an ID drops the loaded asset; ResolveAssets loads the new one.
    void SetMeshID(AssetID id);
    AssetID GetMaterialID() const
    {
        return m_materialID;
    }
    void SetMaterialID(AssetID id);

    // Builds the skeleton from the cooked mesh's joint names and the scene graph. Called
    // lazily once the mesh asset is ready.
    bool BuildSkeletonFromMesh();

  private:
    AssetHandle<Material> m_materialHandle;
    AssetHandle<Mesh> m_meshHandle;
    std::shared_ptr<Material> m_material;
    std::shared_ptr<Mesh> m_mesh;
    std::shared_ptr<Skeleton> m_skeleton;
    // The mesh m_skeleton was built from; a reloaded mesh gets a new skeleton.
    const Mesh *m_skeletonMesh = nullptr;
    AssetID m_meshID;
    AssetID m_materialID;
};

} // namespace Geni
