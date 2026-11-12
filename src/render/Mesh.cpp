#include "render/Mesh.h"
#include "core/Engine.h"
#include "core/Log.h"
#include "rhi/Device.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <memory>
#include <vector>

namespace Geni
{

namespace
{
// FNV-1a over the layout: meshes with the same layout share pipelines.
uint64_t HashLayout(const VertexLayout &layout)
{
    uint64_t hash = 1469598103934665603ull;
    auto mix = [&](uint32_t value) {
        for (int i = 0; i < 4; ++i)
        {
            hash ^= (value >> (i * 8)) & 0xffu;
            hash *= 1099511628211ull;
        }
    };
    mix(layout.stride);
    for (const auto &element : layout.elements)
    {
        mix(element.index);
        mix(element.size);
        mix(static_cast<uint32_t>(element.type));
        mix(element.offset);
    }
    return hash;
}

MeshBounds ComputeBounds(const VertexLayout &layout, const std::vector<float> &vertices)
{
    MeshBounds bounds;
    const auto position = std::find_if(layout.elements.begin(), layout.elements.end(), [](const VertexElement &e) {
        return e.index == VertexElement::PositionIndex && e.size >= 3;
    });
    if (position == layout.elements.end() || layout.stride == 0)
    {
        return bounds;
    }
    const size_t vertexCount = (vertices.size() * sizeof(float)) / layout.stride;
    const size_t positionOffset = position->offset / sizeof(float);
    const size_t stride = layout.stride / sizeof(float);
    for (size_t i = 0; i < vertexCount; ++i)
    {
        const float *p = vertices.data() + i * stride + positionOffset;
        const glm::vec3 position(p[0], p[1], p[2]);
        if (i == 0)
        {
            bounds.min = bounds.max = position;
        }
        else
        {
            bounds.min = glm::min(bounds.min, position);
            bounds.max = glm::max(bounds.max, position);
        }
    }
    bounds.center = (bounds.min + bounds.max) * 0.5f;
    float radiusSquared = 0.0f;
    for (size_t i = 0; i < vertexCount; ++i)
    {
        const float *p = vertices.data() + i * stride + positionOffset;
        const glm::vec3 position(p[0], p[1], p[2]);
        const glm::vec3 delta = position - bounds.center;
        radiusSquared = std::max(radiusSquared, glm::dot(delta, delta));
    }
    bounds.radius = std::sqrt(radiusSquared);
    return bounds;
}
} // namespace

Mesh::Mesh(const VertexLayout &layout, const std::vector<float> &vertices, const std::vector<uint32_t> &indices)
{
    Create(layout, vertices, &indices, rhi::PrimitiveTopology::TriangleList);
}

Mesh::Mesh(const VertexLayout &layout, const std::vector<float> &vertices)
{
    Create(layout, vertices, nullptr, rhi::PrimitiveTopology::TriangleList);
}

Mesh::Mesh(const VertexLayout &layout, const std::vector<float> &vertices, rhi::PrimitiveTopology topology)
{
    Create(layout, vertices, nullptr, topology);
}

void Mesh::Create(
    const VertexLayout &layout, const std::vector<float> &vertices, const std::vector<uint32_t> *indices,
    rhi::PrimitiveTopology topology
)
{
    m_vertexLayout = layout;
    m_topology = topology;
    m_layoutKey = HashLayout(layout);
    m_vertexCount = layout.stride > 0 ? (vertices.size() * sizeof(float)) / layout.stride : 0;
    m_bounds = ComputeBounds(layout, vertices);
    m_lods.clear();
    if (indices && !indices->empty())
    {
        m_lods.push_back(MeshLod{0, static_cast<uint32_t>(indices->size()), 0.0f});
    }
    m_gpuBytes = vertices.size() * sizeof(float) + (indices ? indices->size() * sizeof(uint32_t) : 0);

    rhi::Device *device = Engine::GetInstance().GetRenderer().GetDevice();
    if (!device)
    {
        GENI_LOG_ERROR("Mesh created without a render device (call after Engine::Init)");
        return;
    }
    if (!vertices.empty())
    {
        m_vertexBuffer = device->CreateBuffer(
            {vertices.size() * sizeof(float), rhi::BufferUsage::Vertex, rhi::MemoryUsage::GpuOnly, "mesh vertices"},
            vertices.data()
        );
    }
    if (indices && !indices->empty())
    {
        m_indexCount = indices->size();
        m_indexBuffer = device->CreateBuffer(
            {indices->size() * sizeof(uint32_t), rhi::BufferUsage::Index, rhi::MemoryUsage::GpuOnly, "mesh indices"},
            indices->data()
        );
    }
}

void Mesh::SetLods(std::vector<MeshLod> lods)
{
    lods.erase(
        std::remove_if(
            lods.begin(), lods.end(),
            [this](const MeshLod &lod) {
                return lod.indexCount == 0 || uint64_t(lod.firstIndex) + lod.indexCount > m_indexCount;
            }
        ),
        lods.end()
    );
    if (lods.size() > MaxMeshLods)
    {
        lods.resize(MaxMeshLods);
    }
    if (lods.empty() && m_indexCount > 0)
    {
        lods.push_back(MeshLod{0, static_cast<uint32_t>(m_indexCount), 0.0f});
    }
    m_lods = std::move(lods);
}

Mesh::~Mesh()
{
    if (rhi::Device *device = Engine::GetInstance().GetRenderer().GetDevice())
    {
        device->DestroyBuffer(m_vertexBuffer);
        device->DestroyBuffer(m_indexBuffer);
    }
}

void Mesh::Bind(rhi::CommandList &cmd) const
{
    if (!m_vertexBuffer.IsValid())
    {
        return;
    }
    cmd.SetVertexBuffer(0, m_vertexBuffer);
    if (m_indexBuffer.IsValid())
    {
        cmd.SetIndexBuffer(m_indexBuffer, rhi::IndexFormat::Uint32);
    }
}

void Mesh::Draw(rhi::CommandList &cmd) const
{
    Bind(cmd);
    if (m_indexBuffer.IsValid())
    {
        cmd.DrawIndexed(static_cast<uint32_t>(m_indexCount));
    }
    else
    {
        cmd.Draw(static_cast<uint32_t>(m_vertexCount));
    }
}

std::shared_ptr<Mesh> Mesh::CreateBox(const glm::vec3 &extents)
{
    const glm::vec3 half = extents * 0.5f;

    // clang-format off
    std::vector<float> vertices = {
        // Front face
        half.x, half.y, half.z,     1.0f, 0.0f, 0.0f,     1.0f, 1.0f,     0.0f, 0.0f, 1.0f,
        -half.x, half.y, half.z,    0.0f, 1.0f, 0.0f,     0.0f, 1.0f,     0.0f, 0.0f, 1.0f,
        -half.x, -half.y, half.z,   0.0f, 0.0f, 1.0f,     0.0f, 0.0f,     0.0f, 0.0f, 1.0f,
        half.x, -half.y, half.z,    1.0f, 1.0f, 0.0f,     1.0f, 0.0f,     0.0f, 0.0f, 1.0f,

        // Top face
        half.x, half.y, -half.z,    1.0f, 0.0f, 0.0f,     1.0f, 0.0f,     0.0f, 1.0f, 0.0f,
        -half.x, half.y, -half.z,   0.0f, 1.0f, 0.0f,     0.0f, 0.0f,     0.0f, 1.0f, 0.0f,
        -half.x, half.y, half.z,    0.0f, 0.0f, 1.0f,     0.0f, 1.0f,     0.0f, 1.0f, 0.0f,
        half.x, half.y, half.z,     1.0f, 1.0f, 0.0f,     1.0f, 1.0f,     0.0f, 1.0f, 0.0f,

        // Right face
        half.x, half.y, -half.z,    1.0f, 0.0f, 0.0f,     1.0f, 1.0f,     1.0f, 0.0f, 0.0f,
        half.x, half.y, half.z,     0.0f, 1.0f, 0.0f,     0.0f, 1.0f,     1.0f, 0.0f, 0.0f,
        half.x, -half.y, half.z,    0.0f, 0.0f, 1.0f,     0.0f, 0.0f,     1.0f, 0.0f, 0.0f,
        half.x, -half.y, -half.z,   1.0f, 1.0f, 0.0f,     1.0f, 0.0f,     1.0f, 0.0f, 0.0f,

        // Left face
        -half.x, half.y, half.z,    1.0f, 0.0f, 0.0f,     1.0f, 1.0f,     -1.0f, 0.0f, 0.0f,
        -half.x, half.y, -half.z,   0.0f, 1.0f, 0.0f,     0.0f, 1.0f,     -1.0f, 0.0f, 0.0f,
        -half.x, -half.y, -half.z,  0.0f, 0.0f, 1.0f,     0.0f, 0.0f,     -1.0f, 0.0f, 0.0f,
        -half.x, -half.y, half.z,   1.0f, 1.0f, 0.0f,     1.0f, 0.0f,     -1.0f, 0.0f, 0.0f,

        // Bottom face
        half.x, -half.y, half.z,    1.0f, 0.0f, 0.0f,     1.0f, 1.0f,     0.0f, -1.0f, 0.0f,
        -half.x, -half.y, half.z,   0.0f, 1.0f, 0.0f,     0.0f, 1.0f,     0.0f, -1.0f, 0.0f,
        -half.x, -half.y, -half.z,  0.0f, 0.0f, 1.0f,     0.0f, 0.0f,     0.0f, -1.0f, 0.0f,
        half.x, -half.y, -half.z,   1.0f, 1.0f, 0.0f,     1.0f, 0.0f,     0.0f, -1.0f, 0.0f,

        // Back face
        -half.x, half.y, -half.z,   1.0f, 0.0f, 0.0f,     1.0f, 1.0f,     0.0f, 0.0f, -1.0f,
        half.x, half.y, -half.z,    0.0f, 1.0f, 0.0f,     0.0f, 1.0f,     0.0f, 0.0f, -1.0f,
        half.x, -half.y, -half.z,   0.0f, 0.0f, 1.0f,     0.0f, 0.0f,     0.0f, 0.0f, -1.0f,
        -half.x, -half.y, -half.z,  1.0f, 1.0f, 0.0f,     1.0f, 0.0f,     0.0f, 0.0f, -1.0f,
    };
    std::vector<uint32_t> indices = {
        // front face
        0,1,2,
        0,2,3,
        // top face
        4,5,6,
        4,6,7,
        // right face
        8,9,10,
        8,10,11,
        // left space
        12,13,14,
        12,14,15,
        //bottom face
        16,17,18,
        16,18,19,
        // back face
        20,21,22,
        20,22,23,
    };
    // clang-format on

    Geni::VertexLayout vertexLayout;
    // position
    vertexLayout.elements.push_back({
        VertexElement::PositionIndex,
        3,
        VertexElementType::Float,
        0,
    });
    // color
    vertexLayout.elements.push_back({
        VertexElement::ColorIndex,
        3,
        VertexElementType::Float,
        3 * sizeof(float),
    });
    // uv
    vertexLayout.elements.push_back({
        VertexElement::UVIndex,
        2,
        VertexElementType::Float,
        6 * sizeof(float),
    });
    // Normal
    vertexLayout.elements.push_back({
        VertexElement::NormalIndex,
        3,
        VertexElementType::Float,
        8 * sizeof(float),
    });
    vertexLayout.stride = sizeof(float) * 11;

    auto result = std::make_shared<Geni::Mesh>(vertexLayout, vertices, indices);
    return result;
}

std::shared_ptr<Mesh> Mesh::CreateLines(const std::vector<glm::vec3> &positions, const std::vector<glm::vec3> &colors)
{
    std::vector<float> vertices;
    vertices.reserve(positions.size() * 6);

    for (size_t i = 0; i < positions.size(); ++i)
    {
        vertices.push_back(positions[i].x);
        vertices.push_back(positions[i].y);
        vertices.push_back(positions[i].z);
        vertices.push_back(colors[i].x);
        vertices.push_back(colors[i].y);
        vertices.push_back(colors[i].z);
    }

    VertexLayout layout;
    layout.elements.push_back({VertexElement::PositionIndex, 3, VertexElementType::Float, 0});
    layout.elements.push_back({VertexElement::ColorIndex, 3, VertexElementType::Float, 3 * sizeof(float)});
    layout.stride = 6 * sizeof(float);

    return std::make_shared<Mesh>(layout, vertices, rhi::PrimitiveTopology::LineList);
}

} // namespace Geni
