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
    m_mesh.reset();
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
    m_material.reset();
    m_materialID = material.GetID();
}

void MeshComponent::SetMeshID(AssetID id)
{
    if (id != m_meshID)
    {
        m_meshHandle.Reset();
        m_mesh.reset();
    }
    m_meshID = id;
}

void MeshComponent::SetMaterialID(AssetID id)
{
    if (id != m_materialID)
    {
        m_materialHandle.Reset();
        m_material.reset();
    }
    m_materialID = id;
}

Mesh *MeshComponent::GetMesh() const
{
    // The handle first: after a hot reload its slot holds the new object.
    return m_meshHandle.IsValid() ? m_meshHandle.Get() : m_mesh.get();
}

Material *MeshComponent::GetMaterial() const
{
    return m_materialHandle.IsValid() ? m_materialHandle.Get() : m_material.get();
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
            "mesh", AssetType::Mesh, &MeshComponent::GetMeshID, &MeshComponent::SetMeshID
        ),
        AssetIDProperty<MeshComponent>(
            "material", AssetType::Material, &MeshComponent::GetMaterialID, &MeshComponent::SetMaterialID
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
