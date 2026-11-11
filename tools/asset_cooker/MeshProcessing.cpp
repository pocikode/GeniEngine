#include "MeshProcessing.h"

#include <meshoptimizer.h>

#include <algorithm>
#include <cstdint>
#include <vector>

namespace Geni::cook
{

namespace
{

// The position attribute's offset in floats, or -1 when the layout has none.
int PositionOffset(const VertexLayout &layout)
{
    for (const auto &element : layout.elements)
    {
        if (element.index == VertexElement::PositionIndex && element.size >= 3)
        {
            return static_cast<int>(element.offset / sizeof(float));
        }
    }
    return -1;
}

bool CanProcess(const cooked::CookedMeshData &mesh)
{
    return mesh.topology == rhi::PrimitiveTopology::TriangleList && !mesh.indices.empty() &&
           mesh.indices.size() % 3 == 0 && mesh.layout.stride > 0 && PositionOffset(mesh.layout) >= 0;
}

size_t VertexCount(const cooked::CookedMeshData &mesh)
{
    return mesh.vertices.size() * sizeof(float) / mesh.layout.stride;
}

const float *Positions(const cooked::CookedMeshData &mesh)
{
    return mesh.vertices.data() + PositionOffset(mesh.layout);
}

// Drops vertices no index refers to and puts the rest in first-use order.
void CompactVertices(cooked::CookedMeshData &mesh)
{
    const size_t vertexCount = VertexCount(mesh);
    std::vector<float> compacted(mesh.vertices.size());
    const size_t used = meshopt_optimizeVertexFetch(
        compacted.data(), mesh.indices.data(), mesh.indices.size(), mesh.vertices.data(), vertexCount,
        mesh.layout.stride
    );
    compacted.resize(used * mesh.layout.stride / sizeof(float));
    mesh.vertices = std::move(compacted);
}

} // namespace

void OptimizeAndBuildLods(cooked::CookedMeshData &mesh)
{
    mesh.lods.clear();
    if (!CanProcess(mesh))
    {
        return;
    }

    const size_t vertexCount = VertexCount(mesh);
    meshopt_optimizeVertexCache(mesh.indices.data(), mesh.indices.data(), mesh.indices.size(), vertexCount);
    CompactVertices(mesh);

    const size_t baseCount = mesh.indices.size();
    mesh.lods.push_back(MeshLod{0, static_cast<uint32_t>(baseCount), 0.0f});
    if (baseCount / 3 < MinTrianglesForLods)
    {
        return;
    }

    const size_t positionsStride = mesh.layout.stride;
    const float scale = meshopt_simplifyScale(Positions(mesh), VertexCount(mesh), positionsStride);

    // Each LOD halves the previous one, simplified from it, so errors accumulate the way
    // they will be seen. A level that does not shrink by at least a fifth ends the chain.
    std::vector<uint32_t> previous(mesh.indices.begin(), mesh.indices.end());
    float previousError = 0.0f;
    while (mesh.lods.size() < MaxMeshLods)
    {
        const size_t target = std::max<size_t>(previous.size() / 2 / 3 * 3, 3 * 8);
        std::vector<uint32_t> lod(previous.size());
        float error = 0.0f;
        const size_t count = meshopt_simplify(
            lod.data(), previous.data(), previous.size(), Positions(mesh), VertexCount(mesh), positionsStride, target,
            0.05f, 0, &error
        );
        if (count == 0 || count > previous.size() * 4 / 5)
        {
            break;
        }
        lod.resize(count);
        meshopt_optimizeVertexCache(lod.data(), lod.data(), lod.size(), VertexCount(mesh));

        previousError = std::max(previousError, error * scale);
        MeshLod range;
        range.firstIndex = static_cast<uint32_t>(mesh.indices.size());
        range.indexCount = static_cast<uint32_t>(lod.size());
        range.error = previousError;
        mesh.indices.insert(mesh.indices.end(), lod.begin(), lod.end());
        mesh.lods.push_back(range);
        previous = std::move(lod);
    }
}

float SimplifyInPlace(cooked::CookedMeshData &mesh, float targetRatio, float targetError)
{
    if (!CanProcess(mesh))
    {
        return 0.0f;
    }
    const size_t stride = mesh.layout.stride;
    const float scale = meshopt_simplifyScale(Positions(mesh), VertexCount(mesh), stride);
    const size_t target =
        std::max<size_t>(static_cast<size_t>(static_cast<float>(mesh.indices.size()) * targetRatio) / 3 * 3, 3);

    // Keep the best non-empty result: pruning may remove every small part of a mesh, and
    // an HLOD that loses whole objects is worse than one that is a little heavier.
    std::vector<uint32_t> best = mesh.indices;
    float bestError = 0.0f;
    std::vector<uint32_t> out(mesh.indices.size());
    float error = 0.0f;
    const size_t count = meshopt_simplify(
        out.data(), mesh.indices.data(), mesh.indices.size(), Positions(mesh), VertexCount(mesh), stride, target,
        targetError, meshopt_SimplifyPrune, &error
    );
    if (count >= 3)
    {
        best.assign(out.begin(), out.begin() + static_cast<std::ptrdiff_t>(count));
        bestError = error;
    }
    if (best.size() > target + target / 2)
    {
        // Topology stopped the simplifier (separate boxes cannot collapse into each
        // other); a sloppy pass may merge them.
        float sloppyError = 0.0f;
        const size_t sloppyCount = meshopt_simplifySloppy(
            out.data(), mesh.indices.data(), mesh.indices.size(), Positions(mesh), VertexCount(mesh), stride, nullptr,
            target, targetError, &sloppyError
        );
        if (sloppyCount >= 3 && sloppyCount < best.size())
        {
            best.assign(out.begin(), out.begin() + static_cast<std::ptrdiff_t>(sloppyCount));
            bestError = std::max(bestError, sloppyError);
        }
    }
    mesh.indices = std::move(best);
    meshopt_optimizeVertexCache(mesh.indices.data(), mesh.indices.data(), mesh.indices.size(), VertexCount(mesh));
    CompactVertices(mesh);
    return bestError * scale;
}

} // namespace Geni::cook
