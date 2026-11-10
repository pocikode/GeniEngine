#pragma once
#include "glm/ext/vector_float3.hpp"
#include "graphics/VertexLayout.h"
#include "rhi/Types.h"
#include <memory>
#include <stdint.h>
#include <vector>

namespace Geni
{

namespace rhi
{
class CommandList;
}

// Vertex (and optionally index) data on the GPU, with the layout and primitive type
// pipelines are built for.
class Mesh
{
  public:
    Mesh(const VertexLayout &layout, const std::vector<float> &vertices, const std::vector<uint32_t> &indices);
    Mesh(const VertexLayout &layout, const std::vector<float> &vertices);
    Mesh(const VertexLayout &layout, const std::vector<float> &vertices, rhi::PrimitiveTopology topology);
    Mesh(const Mesh &) = delete;
    Mesh &operator=(const Mesh &) = delete;
    ~Mesh();

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

    static std::shared_ptr<Mesh> CreateBox(const glm::vec3 &extents = glm::vec3(1.0f));
    static std::shared_ptr<Mesh> CreateLines(const std::vector<glm::vec3> &positions,
                                             const std::vector<glm::vec3> &colors);

  private:
    void Create(const VertexLayout &layout, const std::vector<float> &vertices, const std::vector<uint32_t> *indices,
                rhi::PrimitiveTopology topology);

    rhi::BufferHandle m_vertexBuffer;
    rhi::BufferHandle m_indexBuffer;
    size_t m_vertexCount = 0;
    size_t m_indexCount = 0;
    rhi::PrimitiveTopology m_topology = rhi::PrimitiveTopology::TriangleList;
    uint64_t m_layoutKey = 0;
    VertexLayout m_vertexLayout;
};

} // namespace Geni
