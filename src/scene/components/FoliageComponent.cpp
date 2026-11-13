#include "scene/components/FoliageComponent.h"

#include "assets/AssetManager.h"
#include "core/Engine.h"
#include "render/Material.h"
#include "render/Mesh.h"
#include "render/RenderQueue.h"
#include "scene/GameObject.h"
#include "scene/Reflection.h"

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <map>

namespace Geni
{

namespace
{

glm::vec3 ReadVec3(const nlohmann::json &node, const char *key, const glm::vec3 &fallback)
{
    const auto it = node.find(key);
    if (it == node.end() || !it->is_array() || it->size() != 3)
    {
        return fallback;
    }
    return glm::vec3((*it)[0].get<float>(), (*it)[1].get<float>(), (*it)[2].get<float>());
}

glm::mat4 LocalTransform(const nlohmann::json &node)
{
    glm::quat rotation(1.0f, 0.0f, 0.0f, 0.0f);
    const auto it = node.find("rotation");
    if (it != node.end() && it->is_array() && it->size() == 4)
    {
        // Scene files store quaternions as [x, y, z, w].
        rotation =
            glm::quat((*it)[3].get<float>(), (*it)[0].get<float>(), (*it)[1].get<float>(), (*it)[2].get<float>());
    }
    return glm::translate(glm::mat4(1.0f), ReadVec3(node, "position", glm::vec3(0.0f))) * glm::mat4_cast(rotation) *
           glm::scale(glm::mat4(1.0f), ReadVec3(node, "scale", glm::vec3(1.0f)));
}

} // namespace

void FoliageComponent::SetPrefabID(AssetID id)
{
    if (id != m_prefabID)
    {
        m_parts.clear();
        m_resolvedPrefab = false;
    }
    m_prefabID = id;
}

void FoliageComponent::SetInstancesID(AssetID id)
{
    if (id != m_instancesID)
    {
        m_resolvedInstances = false;
    }
    m_instancesID = id;
}

void FoliageComponent::SetDrawDistance(float metres)
{
    m_drawDistance = std::isfinite(metres) ? std::max(0.0f, metres) : 0.0f;
}

void FoliageComponent::SetInstances(std::vector<FoliageInstance> instances)
{
    m_instances = std::move(instances);
    BuildCells();
}

void FoliageComponent::AddPart(
    const AssetHandle<Mesh> &mesh, const AssetHandle<Material> &material, const glm::mat4 &local
)
{
    m_parts.push_back({mesh, material, local});
}

void FoliageComponent::BuildCells()
{
    // Instances grouped into square cells, so whole cells are skipped by distance.
    std::map<std::pair<int, int>, std::vector<uint32_t>> byCell;
    for (uint32_t i = 0; i < m_instances.size(); ++i)
    {
        const glm::vec3 &p = m_instances[i].position;
        byCell[{static_cast<int>(std::floor(p.x / CellSize)), static_cast<int>(std::floor(p.z / CellSize))}].push_back(
            i
        );
    }
    m_cells.clear();
    m_matrices.clear();
    m_matrices.reserve(m_instances.size());
    for (const auto &[key, members] : byCell)
    {
        Cell cell;
        cell.first = static_cast<uint32_t>(m_matrices.size());
        cell.count = static_cast<uint32_t>(members.size());
        glm::vec3 low(1e30f);
        glm::vec3 high(-1e30f);
        float largest = 1.0f;
        for (uint32_t index : members)
        {
            const FoliageInstance &instance = m_instances[index];
            m_matrices.push_back(
                glm::translate(glm::mat4(1.0f), instance.position) *
                glm::rotate(glm::mat4(1.0f), instance.yaw, glm::vec3(0.0f, 1.0f, 0.0f)) *
                glm::scale(glm::mat4(1.0f), glm::vec3(instance.scale))
            );
            low = glm::min(low, instance.position);
            high = glm::max(high, instance.position);
            largest = std::max(largest, instance.scale);
        }
        cell.center = (low + high) * 0.5f;
        // Plants reach a few metres beyond their base; 20 m per unit of scale is generous.
        cell.radius = glm::length(high - low) * 0.5f + 20.0f * largest;
        m_cells.push_back(cell);
    }
}

void FoliageComponent::ResolveAssets(SerializationContext &context)
{
    if (!context.assets)
    {
        return;
    }
    if (m_instancesID.IsValid() && !m_resolvedInstances)
    {
        m_resolvedInstances = true;
        const AssetHandle<FoliageAsset> instances = context.assets->LoadFoliage(m_instancesID, false);
        if (instances.IsReady())
        {
            SetInstances(instances->instances);
        }
    }
    if (m_prefabID.IsValid() && !m_resolvedPrefab)
    {
        m_resolvedPrefab = true;
        m_parts.clear();
        const AssetHandle<SceneAsset> prefab = context.assets->LoadPrefab(m_prefabID, false);
        if (!prefab.IsReady() || !prefab->document.contains("nodes"))
        {
            return;
        }
        // Each MeshComponent of the prefab, relative to its root (whose own transform is the
        // instance's).
        const auto &nodes = prefab->document.at("nodes");
        std::vector<glm::mat4> local(nodes.size(), glm::mat4(1.0f));
        for (size_t i = 0; i < nodes.size(); ++i)
        {
            const int parent = nodes[i].value("parent", -1);
            if (i > 0)
            {
                const glm::mat4 base = parent >= 0 && parent < static_cast<int>(i) ? local[parent] : glm::mat4(1.0f);
                local[i] = base * LocalTransform(nodes[i]);
            }
            for (const auto &component : nodes[i].value("components", nlohmann::json::array()))
            {
                if (component.value("type", std::string()) != "MeshComponent")
                {
                    continue;
                }
                const auto properties = component.value("properties", nlohmann::json::object());
                const AssetID mesh = AssetID::FromHex(properties.value("mesh", std::string()));
                const AssetID material = AssetID::FromHex(properties.value("material", std::string()));
                if (mesh.IsValid() && material.IsValid())
                {
                    AddPart(
                        context.assets->LoadMesh(mesh, true), context.assets->LoadMaterial(material, true), local[i]
                    );
                }
            }
        }
    }
}

void FoliageComponent::Update(float deltaTime)
{
    (void)deltaTime;
    Draw();
}

void FoliageComponent::Draw()
{
    m_drawn = 0;
    if (m_parts.empty() || m_cells.empty() || !m_owner)
    {
        return;
    }
    glm::mat4 view(1.0f);
    if (!Engine::GetInstance().GetActiveView(view))
    {
        return;
    }
    const glm::mat4 world = m_owner->GetWorldTransform();
    const glm::vec3 eye(view[3]);
    RenderQueue &queue = Engine::GetInstance().GetRenderQueue();
    for (const Cell &cell : m_cells)
    {
        const glm::vec3 center(world * glm::vec4(cell.center, 1.0f));
        if (glm::length(center - eye) - cell.radius > m_drawDistance)
        {
            continue;
        }
        for (uint32_t i = cell.first; i < cell.first + cell.count; ++i)
        {
            const glm::mat4 instance = world * m_matrices[i];
            for (const Part &part : m_parts)
            {
                if (!part.mesh.IsReady() || !part.material.IsReady())
                {
                    continue;
                }
                RenderCommand command;
                command.mesh = part.mesh.Get();
                command.material = part.material.Get();
                command.modelMatrix = instance * part.local;
                queue.Submit(command);
            }
        }
        m_drawn += cell.count;
    }
}

namespace
{
const bool s_registered = RegisterComponentType<FoliageComponent>(
    "FoliageComponent",
    {
        AssetIDProperty<FoliageComponent>(
            "prefab", AssetType::Prefab, &FoliageComponent::GetPrefabID, &FoliageComponent::SetPrefabID
        ),
        AssetIDProperty<FoliageComponent>(
            "instances", AssetType::Foliage, &FoliageComponent::GetInstancesID, &FoliageComponent::SetInstancesID
        ),
        FloatProperty<FoliageComponent>(
            "drawDistance", &FoliageComponent::GetDrawDistance, &FoliageComponent::SetDrawDistance
        ),
    }
);
} // namespace

void LinkFoliageComponent()
{
}

} // namespace Geni
