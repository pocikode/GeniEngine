#include "scene/components/SkinnedMeshComponent.h"

#include "core/Engine.h"
#include "render/Material.h"
#include "render/Mesh.h"
#include "render/RenderQueue.h"
#include "scene/GameObject.h"
#include "scene/Skeleton.h"

namespace Geni
{

SkinnedMeshComponent::SkinnedMeshComponent(const std::shared_ptr<Material> &material,
                                           const std::shared_ptr<Mesh> &mesh,
                                           const std::shared_ptr<Skeleton> &skeleton)
    : m_material(material), m_mesh(mesh), m_skeleton(skeleton)
{
}

void SkinnedMeshComponent::Update(float /*deltaTime*/)
{
    if (!m_material || !m_mesh || !m_skeleton)
    {
        return;
    }

    // Joint world transforms are already up-to-date because each joint is a GameObject
    // whose Update() ran earlier in the frame.
    const auto &palette = m_skeleton->BuildPalette();

    RenderCommand command;
    command.material = m_material.get();
    command.mesh = m_mesh.get();
    // Per glTF spec, the skinned mesh node's transform is ignored in favour of the
    // joints' transforms, which already place vertices in world space via the palette.
    command.modelMatrix = glm::mat4(1.0f);
    command.bonePalette = &palette;

    Engine::GetInstance().GetRenderQueue().Submit(command);
}

const std::shared_ptr<Skeleton> &SkinnedMeshComponent::GetSkeleton() const
{
    return m_skeleton;
}

} // namespace Geni
