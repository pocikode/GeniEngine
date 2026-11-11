#include "scene/components/SkinnedMeshComponent.h"

#include "assets/AssetManager.h"
#include "core/Engine.h"
#include "render/Material.h"
#include "render/Mesh.h"
#include "render/RenderQueue.h"
#include "scene/GameObject.h"
#include "scene/Reflection.h"
#include "scene/Scene.h"
#include "scene/Skeleton.h"

#include <algorithm>

namespace Geni
{

SkinnedMeshComponent::SkinnedMeshComponent(
    const std::shared_ptr<Material> &material, const std::shared_ptr<Mesh> &mesh,
    const std::shared_ptr<Skeleton> &skeleton
)
    : m_material(material), m_mesh(mesh), m_skeleton(skeleton)
{
    if (material)
    {
        m_materialID = material->GetAssetID();
    }
    if (mesh)
    {
        m_meshID = mesh->GetAssetID();
    }
}

void SkinnedMeshComponent::SetMesh(const std::shared_ptr<Mesh> &mesh)
{
    m_mesh = mesh;
    m_meshHandle = AssetHandle<Mesh>();
    if (mesh)
    {
        m_meshID = mesh->GetAssetID();
    }
}

void SkinnedMeshComponent::SetMesh(const AssetHandle<Mesh> &mesh)
{
    m_meshHandle = mesh;
    m_mesh.reset();
    m_meshID = mesh.GetID();
}

void SkinnedMeshComponent::SetMaterial(const std::shared_ptr<Material> &material)
{
    m_material = material;
    m_materialHandle = AssetHandle<Material>();
    if (material)
    {
        m_materialID = material->GetAssetID();
    }
}

void SkinnedMeshComponent::SetMaterial(const AssetHandle<Material> &material)
{
    m_materialHandle = material;
    m_material.reset();
    m_materialID = material.GetID();
}

void SkinnedMeshComponent::SetMeshID(AssetID id)
{
    if (id != m_meshID)
    {
        m_meshHandle.Reset();
        m_mesh.reset();
        m_skeleton.reset();
    }
    m_meshID = id;
}

void SkinnedMeshComponent::SetMaterialID(AssetID id)
{
    if (id != m_materialID)
    {
        m_materialHandle.Reset();
        m_material.reset();
    }
    m_materialID = id;
}

Mesh *SkinnedMeshComponent::GetMesh() const
{
    // The handle first: after a hot reload its slot holds the new object.
    return m_meshHandle.IsValid() ? m_meshHandle.Get() : m_mesh.get();
}

Material *SkinnedMeshComponent::GetMaterial() const
{
    return m_materialHandle.IsValid() ? m_materialHandle.Get() : m_material.get();
}

void SkinnedMeshComponent::Update(float /*deltaTime*/)
{
    Material *material = GetMaterial();
    Mesh *mesh = GetMesh();
    if (!material || !mesh)
    {
        return;
    }

    if (mesh->IsSkinned() && (!m_skeleton || (m_skeletonMesh && m_skeletonMesh != mesh)))
    {
        BuildSkeletonFromMesh();
    }
    if (!m_skeleton)
    {
        return;
    }

    // Joint world transforms are already up-to-date because each joint is a GameObject
    // whose Update() ran earlier in the frame.
    const auto &palette = m_skeleton->BuildPalette();

    RenderCommand command;
    command.material = material;
    command.mesh = mesh;
    // Per glTF spec, the skinned mesh node's transform is ignored in favour of the
    // joints' transforms, which already place vertices in world space via the palette.
    command.modelMatrix = glm::mat4(1.0f);
    command.bonePalette = &palette;
    command.boneCount = static_cast<uint32_t>(std::max(0, m_skeleton->GetJointCount()));

    Engine::GetInstance().GetRenderQueue().Submit(command);
}

void SkinnedMeshComponent::ResolveAssets(SerializationContext &context)
{
    if (context.assets)
    {
        if (m_meshID.IsValid() && !m_meshHandle.IsValid())
        {
            SetMesh(context.assets->LoadMesh(m_meshID, true));
        }
        if (m_materialID.IsValid() && !m_materialHandle.IsValid())
        {
            SetMaterial(context.assets->LoadMaterial(m_materialID, true));
        }
    }
}

const std::shared_ptr<Skeleton> &SkinnedMeshComponent::GetSkeleton() const
{
    return m_skeleton;
}

void SkinnedMeshComponent::SetSkeleton(const std::shared_ptr<Skeleton> &skeleton)
{
    m_skeleton = skeleton;
}

bool SkinnedMeshComponent::BuildSkeletonFromMesh()
{
    Mesh *mesh = GetMesh();
    Scene *scene = GetOwner() ? GetOwner()->GetScene() : nullptr;
    if (!mesh || !mesh->IsSkinned() || !scene)
    {
        return false;
    }

    auto skeleton = std::make_shared<Skeleton>();
    const auto &names = mesh->GetJointNames();
    const auto &inverseBindMatrices = mesh->GetInverseBindMatrices();
    for (size_t i = 0; i < names.size(); ++i)
    {
        GameObject *joint = scene->FindObject(names[i]);
        if (!joint)
        {
            GENI_LOG_WARN("skinned mesh: joint '%s' is not in the scene", names[i].c_str());
        }
        skeleton->AddJoint(joint, i < inverseBindMatrices.size() ? inverseBindMatrices[i] : glm::mat4(1.0f));
    }
    m_skeleton = std::move(skeleton);
    m_skeletonMesh = mesh;
    return true;
}

namespace
{
const bool s_registered = RegisterComponentType<SkinnedMeshComponent>(
    "SkinnedMeshComponent",
    {
        AssetIDProperty<SkinnedMeshComponent>(
            "mesh", AssetType::Mesh, &SkinnedMeshComponent::GetMeshID, &SkinnedMeshComponent::SetMeshID
        ),
        AssetIDProperty<SkinnedMeshComponent>(
            "material", AssetType::Material, &SkinnedMeshComponent::GetMaterialID, &SkinnedMeshComponent::SetMaterialID
        ),
    }
);
} // namespace

} // namespace Geni

namespace Geni
{
void LinkSkinnedMeshComponent()
{
}
} // namespace Geni
