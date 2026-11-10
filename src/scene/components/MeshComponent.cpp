#include "scene/components/MeshComponent.h"
#include "assets/AssetManager.h"
#include "core/Engine.h"
#include "render/Material.h"
#include "render/Mesh.h"
#include "render/RenderQueue.h"
#include "scene/GameObject.h"
#include "scene/Reflection.h"

namespace Geni
{

MeshComponent::MeshComponent(const std::shared_ptr<Material> &material, const std::shared_ptr<Mesh> &mesh)
    : m_material(material), m_mesh(mesh)
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

void MeshComponent::SetMesh(const std::shared_ptr<Mesh> &mesh)
{
    m_mesh = mesh;
    m_meshHandle = AssetHandle<Mesh>();
    if (mesh)
    {
        m_meshID = mesh->GetAssetID();
    }
}

void MeshComponent::SetMesh(const AssetHandle<Mesh> &mesh)
{
    m_meshHandle = mesh;
    m_mesh = mesh.Shared();
    m_meshID = mesh.GetID();
}

void MeshComponent::SetMaterial(const std::shared_ptr<Material> &material)
{
    m_material = material;
    m_materialHandle = AssetHandle<Material>();
    if (material)
    {
        m_materialID = material->GetAssetID();
    }
}

void MeshComponent::SetMaterial(const AssetHandle<Material> &material)
{
    m_materialHandle = material;
    m_material = material.Shared();
    m_materialID = material.GetID();
}

Mesh *MeshComponent::GetMesh() const
{
    return m_mesh ? m_mesh.get() : m_meshHandle.Get();
}

Material *MeshComponent::GetMaterial() const
{
    return m_material ? m_material.get() : m_materialHandle.Get();
}

void MeshComponent::Update(float deltaTime)
{
    (void)deltaTime;
    Material *material = GetMaterial();
    Mesh *mesh = GetMesh();
    if (!material || !mesh)
    {
        return;
    }

    Geni::RenderCommand command;
    command.material = material;
    command.mesh = mesh;
    command.modelMatrix = GetOwner()->GetWorldTransform();

    auto &renderQueue = Engine::GetInstance().GetRenderQueue();
    renderQueue.Submit(command);
}

void MeshComponent::ResolveAssets(SerializationContext &context)
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

namespace
{
const bool s_registered = RegisterComponentType<MeshComponent>(
    "MeshComponent",
    {
        AssetIDProperty<MeshComponent>(
            "mesh", PropertyType::Asset, &MeshComponent::GetMeshID, &MeshComponent::SetMeshID
        ),
        AssetIDProperty<MeshComponent>(
            "material", PropertyType::Asset, &MeshComponent::GetMaterialID, &MeshComponent::SetMaterialID
        ),
    }
);
} // namespace

} // namespace Geni

namespace Geni
{
void LinkMeshComponent()
{
}
} // namespace Geni
