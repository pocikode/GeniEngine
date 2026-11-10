#pragma once
#include <cstdint>
#include <vector>

namespace Geni
{

// Component type of a vertex attribute. Vertex data is always interleaved floats.
enum class VertexElementType : uint32_t
{
    Float,
};

struct VertexElement
{
    uint32_t index = 0;                                // attribute location
    uint32_t size = 0;                                 // number of components, 1-4
    VertexElementType type = VertexElementType::Float; // component type
    uint32_t offset = 0;                               // byte offset within a vertex

    static constexpr int PositionIndex = 0;
    static constexpr int ColorIndex = 1;
    static constexpr int UVIndex = 2;
    static constexpr int NormalIndex = 3;
    static constexpr int JointsIndex = 4;
    static constexpr int WeightsIndex = 5;
};

struct VertexLayout
{
    std::vector<VertexElement> elements;
    uint32_t stride = 0; // total size of a single vertex
};

} // namespace Geni
