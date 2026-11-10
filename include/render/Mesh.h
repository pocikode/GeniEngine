#pragma once
#include "assets/Asset.h"
#include "glm/ext/vector_float3.hpp"
#include "graphics/VertexLayout.h"
#include "rhi/Types.h"
#include <glm/mat4x4.hpp>
#include <memory>
#include <stdint.h>
#include <string>
#include <utility>
#include <vector>

namespace Geni
{

namespace rhi
{
class CommandList;
}

// Local-space bounds, computed from the position attribute when the mesh is created.
// The renderer culls with the sphere and the cascades with the box.
struct MeshBounds
{
    glm::vec3 min{0.0f};
    glm::vec3 max{0.0f};
    glm::vec3 center{0.0f};
    float radius = 0.0f;
};

// Vertex (and optionally index) data on the GPU, with the layout and primitive type
// pipelines are built for.
class Mesh : public Asset
{
  public:
    Mesh(const VertexLayout &layout, const std::vector<float> &vertices, const std::vector<uint32_t> &indices);
    Mesh(const VertexLayout &layout, const std::vector<float> &vertices);
    Mesh(const VertexLayout &layout, const std::vector<float> &vertices, rhi::PrimitiveTopology topology);
    Mesh(const Mesh &) = delete;
    Mesh &operator=(const Mesh &) = delete;
    ~Mesh();

    // Binds this mesh's vertex and index buffers (vertex binding 0). The renderer binds
    // the per-instance stream at binding 1 itself.
    void Bind(rhi::CommandList &cmd) const;
    // Binds the buffers and draws, inside an open render pass with a pipeline set.
    void Draw(rhi::CommandList &cmd) const;

    const VertexLayout &GetVertexLayout() const
    {
        return m_vertexLayout;
    }
    rhi::PrimitiveTopology GetTopology() const
    {
        return m_topology;
    }
    // Equal for meshes with identical vertex layouts; part of the pipeline cache key.
    uint64_t GetLayoutKey() const
    {
        return m_layoutKey;
    }
    size_t GetVertexCount() const
    {
        return m_vertexCount;
    }
    size_t GetIndexCount() const
    {
        return m_indexCount;
    }
    const MeshBounds &GetBounds() const
    {
        return m_bounds;
    }
    rhi::BufferHandle GetVertexBuffer() const
    {
        return m_vertexBuffer;
    }
    rhi::BufferHandle GetIndexBuffer() const
    {
        return m_indexBuffer;
    }

    // Skin metadata from the cooked mesh. Empty for a static mesh; SkinnedMeshComponent
    // uses the joint names to build a Skeleton from the scene graph.
    bool IsSkinned() const
    {
        return !m_jointNames.empty();
    }
    const std::vector<std::string> &GetJointNames() const
    {
        return m_jointNames;
    }
    const std::vector<glm::mat4> &GetInverseBindMatrices() const
    {
        return m_inverseBindMatrices;
    }
    void SetSkinData(std::vector<std::string> jointNames, std::vector<glm::mat4> inverseBindMatrices)
    {
        m_jointNames = std::move(jointNames);
        m_inverseBindMatrices = std::move(inverseBindMatrices);
    }

    static std::shared_ptr<Mesh> CreateBox(const glm::vec3 &extents = glm::vec3(1.0f));
    static std::shared_ptr<Mesh> CreateLines(
        const std::vector<glm::vec3> &positions, const std::vector<glm::vec3> &colors
    );

  private:
    void Create(
        const VertexLayout &layout, const std::vector<float> &vertices, const std::vector<uint32_t> *indices,
        rhi::PrimitiveTopology topology
    );

    rhi::BufferHandle m_vertexBuffer;
    rhi::BufferHandle m_indexBuffer;
    size_t m_vertexCount = 0;
    size_t m_indexCount = 0;
    rhi::PrimitiveTopology m_topology = rhi::PrimitiveTopology::TriangleList;
    uint64_t m_layoutKey = 0;
    VertexLayout m_vertexLayout;
    MeshBounds m_bounds;
    std::vector<std::string> m_jointNames;
    std::vector<glm::mat4> m_inverseBindMatrices;
};

} // namespace Geni
