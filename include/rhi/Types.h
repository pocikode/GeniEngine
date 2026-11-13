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
    // HDR render target (renderer v1). 16-bit float, 4 channels.
    RGBA16Float,
    Depth32Float,
    Depth24UnormStencil8,
    Depth16Unorm,
    // Block-compressed sampled textures, cooked by tools/asset_cooker.
    BC1Unorm,
    BC3Unorm,
    BC4Unorm,
    BC5Unorm,
    BC7Unorm,
    ASTC4x4Unorm,
    // sRGB-encoded colour textures (base colour, emissive). The sampler decodes them to
    // linear before filtering, so shaders read linear values.
    RGBA8Srgb,
    BC7Srgb,
    ASTC4x4Srgb,
};

// True for the block-compressed formats above. Their sizes are counted in 4x4 blocks,
// not pixels.
inline bool IsCompressedFormat(Format format)
{
    switch (format)
    {
    case Format::BC1Unorm:
    case Format::BC3Unorm:
    case Format::BC4Unorm:
    case Format::BC5Unorm:
    case Format::BC7Unorm:
    case Format::ASTC4x4Unorm:
    case Format::BC7Srgb:
    case Format::ASTC4x4Srgb:
        return true;
    default:
        return false;
    }
}

// Bytes in one 4x4 block (or one pixel for uncompressed formats).
inline uint32_t BytesPerBlock(Format format)
{
    switch (format)
    {
    case Format::RGBA8Unorm:
    case Format::BGRA8Unorm:
    case Format::RGBA8Srgb:
        return 4;
    case Format::RGBA16Float:
        return 8;
    case Format::BC1Unorm:
    case Format::BC4Unorm:
        return 8;
    case Format::BC3Unorm:
    case Format::BC5Unorm:
    case Format::BC7Unorm:
    case Format::ASTC4x4Unorm:
    case Format::BC7Srgb:
    case Format::ASTC4x4Srgb:
        return 16;
    default:
        return 0;
    }
}

inline uint64_t TextureLevelSize(Format format, uint32_t width, uint32_t height)
{
    if (width == 0 || height == 0)
    {
        return 0;
    }
    if (IsCompressedFormat(format))
    {
        const uint64_t blocksX = (width + 3) / 4;
        const uint64_t blocksY = (height + 3) / 4;
        return blocksX * blocksY * BytesPerBlock(format);
    }
    return uint64_t(width) * height * BytesPerBlock(format);
}

enum class BufferUsage : uint32_t
{
    Vertex = 1u << 0,
    Index = 1u << 1,
    Uniform = 1u << 2,
    // Read-only in shaders (clustered light grid, light list), or read-write in compute
    // shaders (BindingType::StorageBufferReadWrite).
    Storage = 1u << 3,
    // Arguments of DrawIndexedIndirect (docs/design/0029).
    Indirect = 1u << 4,
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

enum class TextureUsage : uint32_t
{
    Sampled = 1u << 0,
    // Can be a colour attachment in a render pass.
    RenderTarget = 1u << 1,
    // Can be the depth attachment in a render pass.
    DepthStencil = 1u << 2,
};

inline TextureUsage operator|(TextureUsage a, TextureUsage b)
{
    return static_cast<TextureUsage>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
inline bool HasFlag(TextureUsage set, TextureUsage flag)
{
    return (static_cast<uint32_t>(set) & static_cast<uint32_t>(flag)) != 0;
}

struct TextureDesc
{
    uint32_t width = 1;
    uint32_t height = 1;
    uint32_t mipLevels = 1;
    // RGBA8Unorm for images, RGBA16Float for HDR colour targets, a depth format for
    // shadow maps and depth attachments.
    Format format = Format::RGBA8Unorm;
    // Sampled textures are read in shaders; targets are rendered into. A target that is
    // also sampled (shadow map, resolved HDR image) sets both flags.
    TextureUsage usage = TextureUsage::Sampled;
    // 1 for normal textures, 4 for 4x MSAA colour/depth attachments.
    uint32_t sampleCount = 1;
    // Optional per-level byte sizes for uploaded textures (mipData). Cooked KTX2 textures
    // fill this because a compressed level's size is not just width * height * bpp. Empty
    // means TextureLevelSize(format, width, height) for every level.
    std::vector<uint64_t> mipByteSizes;
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
    Compute = 1u << 2,
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
    // Read-only structured data: clustered light grid and light list.
    StorageBuffer,
    // A texture and a sampler bound together (Slang `Sampler2D`).
    SampledTexture,
    // Structured data a compute shader writes (Slang `RWStructuredBuffer`).
    StorageBufferReadWrite,
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

enum class VertexStepMode : uint8_t
{
    // One element per vertex (mesh data).
    Vertex,
    // One element per instance (per-object transforms).
    Instance,
};

struct VertexAttribute
{
    uint32_t location = 0;
    VertexFormat format = VertexFormat::Float3;
    uint32_t offset = 0;
    // Which entry of VertexLayoutDesc::buffers this attribute reads.
    uint32_t buffer = 0;
};

// One interleaved vertex or instance buffer. Mesh data is binding 0 with step mode
// Vertex; the renderer's per-instance transforms are binding 1 with step mode Instance.
struct VertexBufferLayout
{
    uint32_t binding = 0;
    uint32_t stride = 0;
    VertexStepMode stepMode = VertexStepMode::Vertex;
};

struct VertexLayoutDesc
{
    std::vector<VertexBufferLayout> buffers;
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
    // src * a + dst; destination alpha is kept.
    Additive,
};

// The attachments a pipeline is created for. Vulkan pipelines are tied to a render-pass
// shape, so the renderer declares it: the backbuffer, the HDR scene target, a
// depth-only shadow pass.
struct PipelineTargets
{
    // Draw into the window's backbuffer (takes precedence over colorFormats).
    bool backbuffer = true;
    // Empty with backbuffer == false means depth-only.
    std::vector<Format> colorFormats;
    // Undefined means no depth attachment. For a backbuffer pass any other value means
    // "with the shared depth buffer" (RenderPassDesc::backbufferDepth). The default matches
    // the backbuffer pass the engine leaves open for Application::Render, which has no depth.
    Format depthFormat = Format::Undefined;
    uint32_t sampleCount = 1;
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
    // Slope-scaled depth bias for shadow passes (0 = off).
    float depthBiasConstant = 0.0f;
    float depthBiasSlope = 0.0f;
    PipelineTargets targets;
    // Bind group layouts for set 0, 1, 2, ...
    std::vector<BindGroupLayoutHandle> bindGroupLayouts;
    const char *debugName = nullptr;
};

// A compute pipeline (docs/design/0029): one compute shader and its bind group layouts.
struct ComputePipelineDesc
{
    ShaderModuleHandle shader;
    std::vector<BindGroupLayoutHandle> bindGroupLayouts;
    const char *debugName = nullptr;
};

// The arguments DrawIndexedIndirect reads, five 32-bit values per draw (Vulkan's and
// WebGPU's layout).
struct DrawIndexedIndirectArgs
{
    uint32_t indexCount = 0;
    uint32_t instanceCount = 0;
    uint32_t firstIndex = 0;
    int32_t vertexOffset = 0;
    uint32_t firstInstance = 0;
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

// A pass that draws into the backbuffer or into off-screen targets. Load and store
// operations are explicit so tile-based GPUs do not load or store attachments they do
// not need. An invalid target handle means the backbuffer (and its shared depth buffer).
struct RenderPassDesc
{
    // Off-screen colour attachment; must have TextureUsage::RenderTarget.
    TextureHandle colorTarget;
    // Optional: when set, the multisampled colourTarget is resolved into this texture at
    // the end of the pass. Sample count comes from colorTarget.
    TextureHandle resolveTarget;
    // Off-screen depth attachment; must have TextureUsage::DepthStencil.
    TextureHandle depthTarget;

    // Backbuffer passes: include the shared depth buffer. Ignored when colorTarget or
    // depthTarget is set. Pipelines drawn in such a pass need a non-Undefined
    // PipelineTargets::depthFormat.
    bool backbufferDepth = false;

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
