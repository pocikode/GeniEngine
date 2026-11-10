#pragma once

#include "rhi/Handle.h"
#include <cstddef>
#include <cstdint>
#include <vector>

// Description structs for the rendering interface. Concepts are limited to what both
// Vulkan and WebGPU have (docs/design/0001-rhi-and-vulkan-backend.md).
//
// Conventions every backend follows:
//   - clip space: Y points up, depth runs 0 (near) to 1 (far), as in WebGPU
//   - counter-clockwise triangles are front-facing
//   - uniform buffers use std140 layout
namespace Geni::rhi
{

enum class Format : uint8_t
{
    Undefined,
    RGBA8Unorm,
    BGRA8Unorm,
    Depth32Float,
    Depth24UnormStencil8,
    Depth16Unorm,
};

enum class BufferUsage : uint32_t
{
    Vertex = 1u << 0,
    Index = 1u << 1,
    Uniform = 1u << 2,
};

inline BufferUsage operator|(BufferUsage a, BufferUsage b)
{
    return static_cast<BufferUsage>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
inline bool HasFlag(BufferUsage set, BufferUsage flag)
{
    return (static_cast<uint32_t>(set) & static_cast<uint32_t>(flag)) != 0;
}

enum class MemoryUsage : uint8_t
{
    // GPU-local. Written with initial data at creation or with Device::UpdateBuffer.
    GpuOnly,
    // Host-visible and persistently mapped (Device::MapBuffer). The caller makes sure the
    // GPU is not reading a range while it is rewritten, usually by keeping one buffer per
    // frame in flight.
    CpuToGpu,
};

struct BufferDesc
{
    uint64_t size = 0;
    BufferUsage usage = BufferUsage::Vertex;
    MemoryUsage memory = MemoryUsage::GpuOnly;
    const char *debugName = nullptr;
};

struct TextureDesc
{
    uint32_t width = 1;
    uint32_t height = 1;
    uint32_t mipLevels = 1;
    // Sampled textures only for now; RGBA8Unorm is the one format the renderer creates.
    Format format = Format::RGBA8Unorm;
    const char *debugName = nullptr;
};

enum class Filter : uint8_t
{
    Nearest,
    Linear,
};

enum class AddressMode : uint8_t
{
    Repeat,
    MirroredRepeat,
    ClampToEdge,
};

struct SamplerDesc
{
    Filter minFilter = Filter::Linear;
    Filter magFilter = Filter::Linear;
    Filter mipmapFilter = Filter::Linear;
    AddressMode addressU = AddressMode::Repeat;
    AddressMode addressV = AddressMode::Repeat;
};

enum class ShaderStage : uint8_t
{
    Vertex = 1u << 0,
    Fragment = 1u << 1,
};

inline ShaderStage operator|(ShaderStage a, ShaderStage b)
{
    return static_cast<ShaderStage>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}
inline bool HasFlag(ShaderStage set, ShaderStage flag)
{
    return (static_cast<uint8_t>(set) & static_cast<uint8_t>(flag)) != 0;
}

struct ShaderModuleDesc
{
    ShaderStage stage = ShaderStage::Vertex;
    const uint32_t *code = nullptr; // SPIR-V words
    size_t codeSize = 0;            // in bytes
    const char *debugName = nullptr;
};

enum class BindingType : uint8_t
{
    UniformBuffer,
    // A uniform buffer whose offset is given at SetBindGroup time.
    DynamicUniformBuffer,
    // A texture and a sampler bound together (GLSL `sampler2D`).
    SampledTexture,
};

struct BindGroupLayoutEntry
{
    uint32_t binding = 0;
    BindingType type = BindingType::UniformBuffer;
    ShaderStage visibility = ShaderStage::Vertex | ShaderStage::Fragment;
};

struct BindGroupLayoutDesc
{
    std::vector<BindGroupLayoutEntry> entries;
};

struct BindGroupEntry
{
    uint32_t binding = 0;
    // Uniform buffers: the buffer and the range a shader sees. For a dynamic uniform
    // buffer, `offset` is a base the dynamic offset is added to.
    BufferHandle buffer;
    uint64_t offset = 0;
    uint64_t size = 0;
    // Sampled textures.
    TextureHandle texture;
    SamplerHandle sampler;
};

struct BindGroupDesc
{
    BindGroupLayoutHandle layout;
    std::vector<BindGroupEntry> entries;
};

enum class VertexFormat : uint8_t
{
    Float1,
    Float2,
    Float3,
    Float4,
};

struct VertexAttribute
{
    uint32_t location = 0;
    VertexFormat format = VertexFormat::Float3;
    uint32_t offset = 0;
};

// One interleaved vertex buffer.
struct VertexLayoutDesc
{
    uint32_t stride = 0;
    std::vector<VertexAttribute> attributes;
};

enum class PrimitiveTopology : uint8_t
{
    TriangleList,
    LineList,
};

enum class CullMode : uint8_t
{
    None,
    Back,
    Front,
};

enum class CompareOp : uint8_t
{
    Never,
    Less,
    LessOrEqual,
    Always,
};

enum class BlendMode : uint8_t
{
    Opaque,
    // Straight alpha: src * a + dst * (1 - a).
    Alpha,
};

struct PipelineDesc
{
    ShaderModuleHandle vertexShader;
    ShaderModuleHandle fragmentShader;
    VertexLayoutDesc vertexLayout;
    PrimitiveTopology topology = PrimitiveTopology::TriangleList;
    CullMode cullMode = CullMode::None;
    bool depthTest = true;
    bool depthWrite = true;
    CompareOp depthCompare = CompareOp::Less;
    BlendMode blend = BlendMode::Opaque;
    // Bind group layouts for set 0, 1, 2, ...
    std::vector<BindGroupLayoutHandle> bindGroupLayouts;
    const char *debugName = nullptr;
};

enum class LoadOp : uint8_t
{
    Load,
    Clear,
    DontCare,
};

enum class StoreOp : uint8_t
{
    Store,
    DontCare,
};

// A pass that draws into the backbuffer and its depth buffer. Load and store operations
// are explicit so tile-based GPUs do not load or store attachments they do not need.
// Off-screen targets arrive with the first feature that needs them (shadow maps, M2).
struct RenderPassDesc
{
    LoadOp colorLoad = LoadOp::Clear;
    StoreOp colorStore = StoreOp::Store;
    float clearColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    LoadOp depthLoad = LoadOp::Clear;
    StoreOp depthStore = StoreOp::DontCare;
    float clearDepth = 1.0f;
};

enum class IndexFormat : uint8_t
{
    Uint16,
    Uint32,
};

} // namespace Geni::rhi
