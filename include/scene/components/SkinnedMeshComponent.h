#pragma once

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
    SkinnedMeshComponent(const std::shared_ptr<Material> &material, const std::shared_ptr<Mesh> &mesh,
                         const std::shared_ptr<Skeleton> &skeleton);

    void Update(float deltaTime) override;

    const std::shared_ptr<Skeleton> &GetSkeleton() const;

  private:
    std::shared_ptr<Material> m_material;
    std::shared_ptr<Mesh> m_mesh;
    std::shared_ptr<Skeleton> m_skeleton;
};

} // namespace Geni
