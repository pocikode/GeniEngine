#include "render/Renderer.h"
#include "core/Log.h"
#include "core/Profiler.h"
#include "graphics/Texture.h"
#include "render/ClusterGrid.h"
#include "render/Frustum.h"
#include "render/Material.h"
#include "render/Shader.h"
#include "rhi/Device.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <limits>

// Built-in shaders, compiled at build time (geni_embed_shaders in CMakeLists.txt).
#include "default_frag.spv.h"
#include "default_vert.spv.h"
#include "shadow_frag.spv.h"
#include "shadow_skinned_vert.spv.h"
#include "shadow_vert.spv.h"
#include "skinned_frag.spv.h"
#include "skinned_vert.spv.h"
#include "sky_frag.spv.h"
#include "sky_vert.spv.h"
#include "sprite_frag.spv.h"
#include "sprite_lit_frag.spv.h"
#include "sprite_lit_vert.spv.h"
#include "sprite_vert.spv.h"
#include "terrain_frag.spv.h"
#include "terrain_vert.spv.h"
#include "tonemap_frag.spv.h"
#include "tonemap_vert.spv.h"

namespace Geni
{

// One sprite corner (docs/design/0017): camera-relative position with the sRGB-decode
// flag in w, texture coordinate, linear colour.
struct SpriteVertex
{
    glm::vec4 position;
    glm::vec2 uv;
    glm::vec4 color;
};
static_assert(sizeof(SpriteVertex) == 40, "sprite vertex stride");

using namespace ShaderInterface;

namespace
{
// CameraComponent builds OpenGL-style projections (depth -1..1). The RHI's clip space has
// depth 0..1: z' = 0.5 * z + 0.5 * w.
const glm::mat4 kDepthZeroToOne(
    1.0f, 0.0f, 0.0f, 0.0f, //
    0.0f, 1.0f, 0.0f, 0.0f, //
    0.0f, 0.0f, 0.5f, 0.0f, //
    0.0f, 0.0f, 0.5f, 1.0f
);

constexpr uint64_t kInitialDrawCapacity = 1u << 20;
constexpr uint64_t kInstanceStride = sizeof(glm::mat4);          // 64
constexpr uint64_t kShadowInstanceStride = kInstanceStride + 16; // matrix + cascade index + padding

constexpr uint64_t kSpriteQuadBytes = 4 * sizeof(SpriteVertex);
// Bind groups for sprite textures not drawn for this many frames are released.
constexpr uint64_t kSpriteGroupLifetime = 240;

bool IsSrgbFormat(rhi::Format format)
{
    return format == rhi::Format::RGBA8Srgb || format == rhi::Format::BC7Srgb || format == rhi::Format::ASTC4x4Srgb;
}

float SrgbChannelToLinear(float c)
{
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

struct ShadowInstance
{
    glm::mat4 model;
    float cascade = 0.0f;
    float padding[3] = {0.0f, 0.0f, 0.0f};
};
static_assert(sizeof(ShadowInstance) == kShadowInstanceStride, "shadow instance stride");

rhi::VertexFormat ToVertexFormat(uint32_t size)
{
    switch (size)
    {
    case 1:
        return rhi::VertexFormat::Float1;
    case 2:
        return rhi::VertexFormat::Float2;
    case 3:
        return rhi::VertexFormat::Float3;
    default:
        return rhi::VertexFormat::Float4;
    }
}

float MaxScale(const glm::mat4 &matrix)
{
    return std::max(
        {glm::length(glm::vec3(matrix[0])), glm::length(glm::vec3(matrix[1])), glm::length(glm::vec3(matrix[2]))}
    );
}

// A world transform moved into the camera-relative space the shaders use.
glm::mat4 RelativeTo(const glm::mat4 &model, const glm::vec3 &origin)
{
    glm::mat4 relative = model;
    relative[3] -= glm::vec4(origin, 0.0f);
    return relative;
}

// World-space bounding sphere of a command. Returns radius 0 when the mesh has no bounds.
void WorldBounds(const RenderCommand &command, glm::vec3 &center, float &radius)
{
    const MeshBounds &bounds = command.mesh->GetBounds();
    const bool skinned = command.bonePalette && !command.bonePalette->empty();
    if (!skinned || bounds.radius <= 0.0f)
    {
        center = glm::vec3(command.modelMatrix * glm::vec4(bounds.center, 1.0f));
        radius = bounds.radius * MaxScale(command.modelMatrix);
        return;
    }

    // A skinned vertex is a weighted average (weights sum to 1) of bone_i * v, and each
    // bone_i * v lies in the sphere (bone_i * c, r * scale_i) when v lies in the bind-pose
    // sphere (c, r). A sphere around all the bone spheres therefore bounds the posed mesh.
    const std::vector<glm::mat4> &palette = *command.bonePalette;
    size_t count = std::min<size_t>(palette.size(), MaxBones);
    if (command.boneCount > 0)
    {
        count = std::min<size_t>(count, command.boneCount);
    }
    glm::vec3 lo(std::numeric_limits<float>::max());
    glm::vec3 hi(-std::numeric_limits<float>::max());
    for (size_t i = 0; i < count; ++i)
    {
        const glm::vec3 c = glm::vec3(palette[i] * glm::vec4(bounds.center, 1.0f));
        const float r = bounds.radius * MaxScale(palette[i]);
        lo = glm::min(lo, c - glm::vec3(r));
        hi = glm::max(hi, c + glm::vec3(r));
    }
    glm::vec3 posedCenter = (lo + hi) * 0.5f;
    float posedRadius = 0.0f;
    for (size_t i = 0; i < count; ++i)
    {
        const glm::vec3 c = glm::vec3(palette[i] * glm::vec4(bounds.center, 1.0f));
        posedRadius = std::max(posedRadius, glm::distance(posedCenter, c) + bounds.radius * MaxScale(palette[i]));
    }
    // The shader applies the model matrix after skinning.
    center = glm::vec3(command.modelMatrix * glm::vec4(posedCenter, 1.0f));
    radius = posedRadius * MaxScale(command.modelMatrix);
}
} // namespace

glm::vec4 SrgbToLinear(const glm::vec4 &srgb)
{
    return glm::vec4(SrgbChannelToLinear(srgb.r), SrgbChannelToLinear(srgb.g), SrgbChannelToLinear(srgb.b), srgb.a);
}

bool Renderer::Init(rhi::Device *device, const RendererConfig &config)
{
    m_device = device;
    m_config = config;
    m_config.msaaSamples = m_config.msaaSamples == 1 || m_config.msaaSamples == 2 ? m_config.msaaSamples : 4;
    m_config.cascadeCount = std::clamp<uint32_t>(m_config.cascadeCount, 1, Cascades);
    m_config.shadowMapSize = std::clamp<uint32_t>(m_config.shadowMapSize, 256, 8192);
    m_config.shadowDistance = std::max(1.0f, m_config.shadowDistance);
    m_config.clusterCountX = std::max(1u, m_config.clusterCountX);
    m_config.clusterCountY = std::max(1u, m_config.clusterCountY);
    m_config.clusterCountZ = std::max(1u, m_config.clusterCountZ);
    m_config.maxLightsPerCluster = std::clamp<uint32_t>(m_config.maxLightsPerCluster, 1, 1024);
    m_alignment = std::max<uint32_t>(16, device->GetUniformBufferAlignment());

    rhi::BindGroupLayoutDesc frameLayout;
    frameLayout.entries.push_back(
        {FrameBinding, rhi::BindingType::UniformBuffer, rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment}
    );
    frameLayout.entries.push_back({LightsBinding, rhi::BindingType::StorageBuffer, rhi::ShaderStage::Fragment});
    frameLayout.entries.push_back({ClusterGridBinding, rhi::BindingType::StorageBuffer, rhi::ShaderStage::Fragment});
    frameLayout.entries.push_back({ClusterLightsBinding, rhi::BindingType::StorageBuffer, rhi::ShaderStage::Fragment});
    for (uint32_t i = 0; i < Cascades; ++i)
    {
        frameLayout.entries.push_back(
            {ShadowBinding0 + i, rhi::BindingType::SampledTexture, rhi::ShaderStage::Fragment}
        );
    }
    m_frameLayout = m_device->CreateBindGroupLayout(frameLayout);

    rhi::BindGroupLayoutDesc drawLayout;
    drawLayout.entries.push_back({BonesBinding, rhi::BindingType::DynamicUniformBuffer, rhi::ShaderStage::Vertex});
    m_drawLayout = m_device->CreateBindGroupLayout(drawLayout);

    rhi::BindGroupLayoutDesc tonemapLayout;
    tonemapLayout.entries.push_back({0, rhi::BindingType::SampledTexture, rhi::ShaderStage::Fragment});
    m_tonemapLayout = m_device->CreateBindGroupLayout(tonemapLayout);
    m_emptyLayout = m_device->CreateBindGroupLayout(rhi::BindGroupLayoutDesc());

    m_sampler = m_device->CreateSampler(rhi::SamplerDesc());
    rhi::SamplerDesc shadowSampler;
    shadowSampler.addressU = rhi::AddressMode::ClampToEdge;
    shadowSampler.addressV = rhi::AddressMode::ClampToEdge;
    // Nearest: the shader filters with its own PCF, and linear filtering of depth formats
    // is an optional Vulkan feature.
    shadowSampler.minFilter = rhi::Filter::Nearest;
    shadowSampler.magFilter = rhi::Filter::Nearest;
    shadowSampler.mipmapFilter = rhi::Filter::Nearest;
    m_shadowSampler = m_device->CreateSampler(shadowSampler);

    m_defaultShader = Shader::Create(
        geni_spirv_default_vert, sizeof(geni_spirv_default_vert), geni_spirv_default_frag,
        sizeof(geni_spirv_default_frag), "builtin/default"
    );
    m_skinnedShader = Shader::Create(
        geni_spirv_skinned_vert, sizeof(geni_spirv_skinned_vert), geni_spirv_skinned_frag,
        sizeof(geni_spirv_skinned_frag), "builtin/skinned"
    );
    m_terrainShader = Shader::Create(
        geni_spirv_terrain_vert, sizeof(geni_spirv_terrain_vert), geni_spirv_terrain_frag,
        sizeof(geni_spirv_terrain_frag), "builtin/terrain"
    );
    m_skyShader = Shader::Create(
        geni_spirv_sky_vert, sizeof(geni_spirv_sky_vert), geni_spirv_sky_frag, sizeof(geni_spirv_sky_frag),
        "builtin/sky"
    );
    if (!m_defaultShader || !m_skinnedShader || !m_skyShader || !m_terrainShader)
    {
        return false;
    }

    m_shadowVertexModule = m_device->CreateShaderModule(
        {rhi::ShaderStage::Vertex, geni_spirv_shadow_vert, sizeof(geni_spirv_shadow_vert), "builtin/shadow.vert"}
    );
    m_shadowSkinnedVertexModule = m_device->CreateShaderModule(
        {rhi::ShaderStage::Vertex, geni_spirv_shadow_skinned_vert, sizeof(geni_spirv_shadow_skinned_vert),
         "builtin/shadow_skinned.vert"}
    );
    m_shadowFragmentModule = m_device->CreateShaderModule(
        {rhi::ShaderStage::Fragment, geni_spirv_shadow_frag, sizeof(geni_spirv_shadow_frag), "builtin/shadow.frag"}
    );
    m_tonemapVertexModule = m_device->CreateShaderModule(
        {rhi::ShaderStage::Vertex, geni_spirv_tonemap_vert, sizeof(geni_spirv_tonemap_vert), "builtin/tonemap.vert"}
    );
    m_tonemapFragmentModule = m_device->CreateShaderModule(
        {rhi::ShaderStage::Fragment, geni_spirv_tonemap_frag, sizeof(geni_spirv_tonemap_frag), "builtin/tonemap.frag"}
    );
    m_spriteVertexModule = m_device->CreateShaderModule(
        {rhi::ShaderStage::Vertex, geni_spirv_sprite_vert, sizeof(geni_spirv_sprite_vert), "builtin/sprite.vert"}
    );
    m_spriteFragmentModule = m_device->CreateShaderModule(
        {rhi::ShaderStage::Fragment, geni_spirv_sprite_frag, sizeof(geni_spirv_sprite_frag), "builtin/sprite.frag"}
    );
    m_spriteLitVertexModule = m_device->CreateShaderModule(
        {rhi::ShaderStage::Vertex, geni_spirv_sprite_lit_vert, sizeof(geni_spirv_sprite_lit_vert),
         "builtin/sprite_lit.vert"}
    );
    m_spriteLitFragmentModule = m_device->CreateShaderModule(
        {rhi::ShaderStage::Fragment, geni_spirv_sprite_lit_frag, sizeof(geni_spirv_sprite_lit_frag),
         "builtin/sprite_lit.frag"}
    );
    rhi::SamplerDesc spriteSampler;
    spriteSampler.addressU = rhi::AddressMode::ClampToEdge;
    spriteSampler.addressV = rhi::AddressMode::ClampToEdge;
    m_spriteLinearSampler = m_device->CreateSampler(spriteSampler);
    spriteSampler.minFilter = rhi::Filter::Nearest;
    spriteSampler.magFilter = rhi::Filter::Nearest;
    spriteSampler.mipmapFilter = rhi::Filter::Nearest;
    m_spriteNearestSampler = m_device->CreateSampler(spriteSampler);
    {
        const uint8_t white[4] = {255, 255, 255, 255};
        const void *levels[1] = {white};
        rhi::TextureDesc whiteDesc;
        whiteDesc.debugName = "sprite white";
        m_spriteWhiteTexture = m_device->CreateTexture(whiteDesc, levels);
    }
    if (!m_spriteVertexModule.IsValid() || !m_spriteFragmentModule.IsValid() || !m_spriteLitVertexModule.IsValid() ||
        !m_spriteLitFragmentModule.IsValid() || !m_spriteLinearSampler.IsValid() || !m_spriteNearestSampler.IsValid() ||
        !m_spriteWhiteTexture.IsValid())
    {
        return false;
    }

    if (!m_shadowVertexModule.IsValid() || !m_shadowSkinnedVertexModule.IsValid() ||
        !m_shadowFragmentModule.IsValid() || !m_tonemapVertexModule.IsValid() || !m_tonemapFragmentModule.IsValid())
    {
        return false;
    }

    if (!CreateShadowMaps())
    {
        return false;
    }
    m_frames.resize(m_device->GetFramesInFlight());
    uint32_t width = 1;
    uint32_t height = 1;
    m_device->GetBackbufferSize(width, height);
    for (auto &frame : m_frames)
    {
        if (!CreateFrameResources(frame, std::max(1u, width), std::max(1u, height)))
        {
            return false;
        }
    }

    if (!GetSkyPipeline().IsValid() || !GetTonemapPipeline().IsValid() || !GetSpritePipeline().IsValid() ||
        !GetSpritePipeline(SpriteBlend::Additive).IsValid() || !GetSpritePipeline(SpriteBlend::Alpha, true).IsValid() ||
        !GetSpritePipeline(SpriteBlend::Additive, true).IsValid())
    {
        return false;
    }
    return m_frameLayout.IsValid() && m_drawLayout.IsValid() && m_tonemapLayout.IsValid() && m_sampler.IsValid() &&
           m_shadowSampler.IsValid();
}

void Renderer::Shutdown()
{
    if (!m_device)
    {
        return;
    }
    m_defaultShader.reset();
    m_skinnedShader.reset();
    m_terrainShader.reset();
    m_skyShader.reset();
    for (auto &entry : m_mainPipelines)
    {
        m_device->DestroyPipeline(entry.second);
    }
    for (auto &entry : m_shadowPipelines)
    {
        m_device->DestroyPipeline(entry.second);
    }
    m_mainPipelines.clear();
    m_shadowPipelines.clear();
    m_device->DestroyPipeline(m_skyPipeline);
    m_device->DestroyPipeline(m_tonemapPipeline);
    for (auto &byBlend : m_spritePipelines)
    {
        for (auto &pipeline : byBlend)
        {
            m_device->DestroyPipeline(pipeline);
            pipeline = {};
        }
    }
    m_skyPipeline = {};
    m_tonemapPipeline = {};

    for (auto &entry : m_spriteGroups)
    {
        m_device->DestroyBindGroup(entry.second.group);
    }
    m_spriteGroups.clear();
    m_spriteBatches.clear();
    m_device->DestroyBuffer(m_spriteIndexBuffer);
    m_spriteIndexBuffer = {};
    m_spriteIndexCapacity = 0;
    m_device->DestroyTexture(m_spriteWhiteTexture);
    m_spriteWhiteTexture = {};
    m_device->DestroySampler(m_spriteLinearSampler);
    m_device->DestroySampler(m_spriteNearestSampler);
    m_spriteLinearSampler = {};
    m_spriteNearestSampler = {};
    m_device->DestroyShaderModule(m_spriteVertexModule);
    m_device->DestroyShaderModule(m_spriteFragmentModule);
    m_device->DestroyShaderModule(m_spriteLitVertexModule);
    m_device->DestroyShaderModule(m_spriteLitFragmentModule);
    m_spriteLitVertexModule = {};
    m_spriteLitFragmentModule = {};
    m_spriteVertexModule = {};
    m_spriteFragmentModule = {};

    m_device->DestroyShaderModule(m_shadowVertexModule);
    m_device->DestroyShaderModule(m_shadowSkinnedVertexModule);
    m_device->DestroyShaderModule(m_shadowFragmentModule);
    m_device->DestroyShaderModule(m_tonemapVertexModule);
    m_device->DestroyShaderModule(m_tonemapFragmentModule);
    m_shadowVertexModule = {};
    m_shadowSkinnedVertexModule = {};
    m_shadowFragmentModule = {};
    m_tonemapVertexModule = {};
    m_tonemapFragmentModule = {};

    for (auto &frame : m_frames)
    {
        DestroyFrameResources(frame);
    }
    m_frames.clear();
    for (auto &shadow : m_shadowMaps)
    {
        m_device->DestroyTexture(shadow);
        shadow = {};
    }
    m_device->DestroySampler(m_shadowSampler);
    m_device->DestroySampler(m_sampler);
    m_device->DestroyBindGroupLayout(m_tonemapLayout);
    m_device->DestroyBindGroupLayout(m_emptyLayout);
    m_device->DestroyBindGroupLayout(m_drawLayout);
    m_device->DestroyBindGroupLayout(m_frameLayout);
    m_device = nullptr;
}

void Renderer::DestroyFrameResources(FrameResources &frame)
{
    m_device->DestroyBindGroup(frame.tonemapGroup);
    m_device->DestroyBindGroup(frame.frameGroup);
    m_device->DestroyBindGroup(frame.drawGroup);
    frame.tonemapGroup = {};
    frame.frameGroup = {};
    frame.drawGroup = {};

    m_device->DestroyTexture(frame.hdrColor);
    m_device->DestroyTexture(frame.msaaColor);
    m_device->DestroyTexture(frame.depth);
    frame.hdrColor = {};
    frame.msaaColor = {};
    frame.depth = {};

    m_device->DestroyBuffer(frame.frameBuffer);
    m_device->DestroyBuffer(frame.lightsBuffer);
    m_device->DestroyBuffer(frame.clusterGridBuffer);
    m_device->DestroyBuffer(frame.clusterIndicesBuffer);
    m_device->DestroyBuffer(frame.instanceBuffer);
    m_device->DestroyBuffer(frame.drawBuffer);
    m_device->DestroyBuffer(frame.spriteVertexBuffer);
    frame.spriteVertexBuffer = {};
    frame.spriteQuadCapacity = 0;
    frame.frameBuffer = {};
    frame.lightsBuffer = {};
    frame.clusterGridBuffer = {};
    frame.clusterIndicesBuffer = {};
    frame.instanceBuffer = {};
    frame.drawBuffer = {};
    frame.lightsCapacity = 0;
    frame.gridCapacity = 0;
    frame.indexCapacity = 0;
    frame.instanceCapacity = 0;
    frame.drawCapacity = 0;
    frame.drawCursor = 0;

    for (uint32_t i = 0; i < Cascades; ++i)
    {
        m_device->DestroyBuffer(frame.shadowInstanceBuffers[i]);
        frame.shadowInstanceBuffers[i] = {};
        frame.shadowInstanceCapacities[i] = 0;
    }
}

bool Renderer::CreateFrameResources(FrameResources &frame, uint32_t width, uint32_t height)
{
    const rhi::Format depthFormat = m_device->GetDepthFormat();
    const uint32_t samples = m_config.msaaSamples;

    if (frame.hdrColor.IsValid() && frame.width == width && frame.height == height)
    {
        return true;
    }

    m_device->DestroyBindGroup(frame.tonemapGroup);
    m_device->DestroyTexture(frame.hdrColor);
    m_device->DestroyTexture(frame.msaaColor);
    m_device->DestroyTexture(frame.depth);
    frame.tonemapGroup = {};
    frame.hdrColor = {};
    frame.msaaColor = {};
    frame.depth = {};

    rhi::TextureDesc colorDesc;
    colorDesc.width = width;
    colorDesc.height = height;
    colorDesc.format = rhi::Format::RGBA16Float;
    colorDesc.usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::RenderTarget;
    colorDesc.debugName = "HDR colour";
    frame.hdrColor = m_device->CreateTexture(colorDesc, nullptr);

    if (samples > 1)
    {
        rhi::TextureDesc msaaDesc = colorDesc;
        msaaDesc.usage = rhi::TextureUsage::RenderTarget;
        msaaDesc.sampleCount = samples;
        msaaDesc.debugName = "HDR colour (MSAA)";
        frame.msaaColor = m_device->CreateTexture(msaaDesc, nullptr);
    }

    rhi::TextureDesc depthDesc;
    depthDesc.width = width;
    depthDesc.height = height;
    depthDesc.format = depthFormat;
    depthDesc.usage = rhi::TextureUsage::DepthStencil;
    depthDesc.sampleCount = samples;
    depthDesc.debugName = "scene depth";
    frame.depth = m_device->CreateTexture(depthDesc, nullptr);

    if (!frame.hdrColor.IsValid() || !frame.depth.IsValid() || (samples > 1 && !frame.msaaColor.IsValid()))
    {
        GENI_LOG_ERROR("Renderer: could not create the HDR scene targets");
        return false;
    }

    rhi::BindGroupDesc tonemap;
    tonemap.layout = m_tonemapLayout;
    tonemap.entries.push_back({0, {}, 0, 0, frame.hdrColor, m_sampler});
    frame.tonemapGroup = m_device->CreateBindGroup(tonemap);

    // Buffers that only need to exist, not resize with the window.
    if (!frame.frameBuffer.IsValid())
    {
        frame.frameBuffer = m_device->CreateBuffer(
            {sizeof(FrameUniforms), rhi::BufferUsage::Uniform, rhi::MemoryUsage::CpuToGpu, "frame uniforms"}
        );
    }
    if (!frame.instanceBuffer.IsValid())
    {
        frame.instanceBuffer = m_device->CreateBuffer(
            {1024 * kInstanceStride, rhi::BufferUsage::Vertex, rhi::MemoryUsage::CpuToGpu, "instance transforms"}
        );
        frame.instanceCapacity = 1024;
    }
    if (!frame.drawBuffer.IsValid())
    {
        frame.drawBuffer = m_device->CreateBuffer(
            {kInitialDrawCapacity, rhi::BufferUsage::Uniform, rhi::MemoryUsage::CpuToGpu, "draw uniforms"}
        );
        frame.drawCapacity = kInitialDrawCapacity;
        frame.drawCursor = Align(sizeof(BoneUniforms));
        rhi::BindGroupDesc draw;
        draw.layout = m_drawLayout;
        draw.entries.push_back({BonesBinding, frame.drawBuffer, 0, sizeof(BoneUniforms)});
        frame.drawGroup = m_device->CreateBindGroup(draw);
    }
    for (uint32_t i = 0; i < Cascades; ++i)
    {
        if (!frame.shadowInstanceBuffers[i].IsValid())
        {
            frame.shadowInstanceBuffers[i] = m_device->CreateBuffer(
                {1024 * kShadowInstanceStride, rhi::BufferUsage::Vertex, rhi::MemoryUsage::CpuToGpu, "shadow instances"}
            );
            frame.shadowInstanceCapacities[i] = 1024;
        }
    }
    if (!frame.lightsBuffer.IsValid())
    {
        frame.lightsBuffer = m_device->CreateBuffer(
            {64 * sizeof(GpuLight), rhi::BufferUsage::Storage, rhi::MemoryUsage::CpuToGpu, "lights"}
        );
        frame.lightsCapacity = 64;
    }
    if (!frame.clusterGridBuffer.IsValid())
    {
        const uint64_t clusters = uint64_t(m_config.clusterCountX) * m_config.clusterCountY * m_config.clusterCountZ;
        frame.clusterGridBuffer = m_device->CreateBuffer(
            {clusters * sizeof(ClusterRange), rhi::BufferUsage::Storage, rhi::MemoryUsage::CpuToGpu, "cluster grid"}
        );
        frame.clusterIndicesBuffer = m_device->CreateBuffer(
            {clusters * 8 * sizeof(uint32_t), rhi::BufferUsage::Storage, rhi::MemoryUsage::CpuToGpu,
             "cluster light indices"}
        );
        frame.gridCapacity = clusters;
        frame.indexCapacity = clusters * 8;
    }

    frame.width = width;
    frame.height = height;
    CreateFrameBindGroup(frame);
    return frame.frameGroup.IsValid() && frame.tonemapGroup.IsValid();
}

bool Renderer::CreateShadowMaps()
{
    // Independent of the window size, so they are made once and never resized.
    for (uint32_t i = 0; i < m_config.cascadeCount; ++i)
    {
        rhi::TextureDesc shadowDesc;
        shadowDesc.width = m_config.shadowMapSize;
        shadowDesc.height = m_config.shadowMapSize;
        shadowDesc.format = m_device->GetSampledDepthFormat();
        shadowDesc.usage = rhi::TextureUsage::DepthStencil | rhi::TextureUsage::Sampled;
        shadowDesc.debugName = "shadow cascade";
        m_shadowMaps[i] = m_device->CreateTexture(shadowDesc, nullptr);
        if (!m_shadowMaps[i].IsValid())
        {
            GENI_LOG_ERROR("Renderer: could not create shadow map %u", i);
            return false;
        }
    }
    return true;
}

void Renderer::CreateFrameBindGroup(FrameResources &frame)
{
    m_device->DestroyBindGroup(frame.frameGroup);
    rhi::BindGroupDesc desc;
    desc.layout = m_frameLayout;
    desc.entries.push_back({FrameBinding, frame.frameBuffer, 0, sizeof(FrameUniforms)});
    desc.entries.push_back({LightsBinding, frame.lightsBuffer, 0, 0});
    desc.entries.push_back({ClusterGridBinding, frame.clusterGridBuffer, 0, 0});
    desc.entries.push_back({ClusterLightsBinding, frame.clusterIndicesBuffer, 0, 0});
    for (uint32_t i = 0; i < Cascades; ++i)
    {
        rhi::BindGroupEntry entry;
        entry.binding = ShadowBinding0 + i;
        entry.texture = m_shadowMaps[std::min<uint32_t>(i, m_config.cascadeCount - 1)];
        entry.sampler = m_shadowSampler;
        desc.entries.push_back(entry);
    }
    frame.frameGroup = m_device->CreateBindGroup(desc);
}

bool Renderer::EnsureInstanceCapacity(FrameResources &frame, uint64_t instances)
{
    if (instances <= frame.instanceCapacity)
    {
        return true;
    }
    uint64_t capacity = std::max<uint64_t>(frame.instanceCapacity, 1);
    while (capacity < instances)
    {
        capacity *= 2;
    }
    m_device->DestroyBuffer(frame.instanceBuffer);
    frame.instanceBuffer = m_device->CreateBuffer(
        {capacity * kInstanceStride, rhi::BufferUsage::Vertex, rhi::MemoryUsage::CpuToGpu, "instance transforms"}
    );
    frame.instanceCapacity = capacity;
    return frame.instanceBuffer.IsValid();
}

bool Renderer::EnsureShadowInstanceCapacity(FrameResources &frame, uint32_t cascade, uint64_t instances)
{
    if (instances <= frame.shadowInstanceCapacities[cascade])
    {
        return true;
    }
    uint64_t capacity = std::max<uint64_t>(frame.shadowInstanceCapacities[cascade], 1);
    while (capacity < instances)
    {
        capacity *= 2;
    }
    m_device->DestroyBuffer(frame.shadowInstanceBuffers[cascade]);
    frame.shadowInstanceBuffers[cascade] = m_device->CreateBuffer(
        {capacity * kShadowInstanceStride, rhi::BufferUsage::Vertex, rhi::MemoryUsage::CpuToGpu, "shadow instances"}
    );
    frame.shadowInstanceCapacities[cascade] = capacity;
    return frame.shadowInstanceBuffers[cascade].IsValid();
}

bool Renderer::EnsureDrawCapacity(FrameResources &frame, uint64_t capacity)
{
    if (capacity <= frame.drawCapacity)
    {
        return true;
    }
    uint64_t size = std::max<uint64_t>(frame.drawCapacity, 1);
    while (size < capacity)
    {
        size *= 2;
    }
    m_device->DestroyBindGroup(frame.drawGroup);
    m_device->DestroyBuffer(frame.drawBuffer);
    frame.drawBuffer =
        m_device->CreateBuffer({size, rhi::BufferUsage::Uniform, rhi::MemoryUsage::CpuToGpu, "draw uniforms"});
    rhi::BindGroupDesc desc;
    desc.layout = m_drawLayout;
    desc.entries.push_back({BonesBinding, frame.drawBuffer, 0, sizeof(BoneUniforms)});
    frame.drawGroup = m_device->CreateBindGroup(desc);
    frame.drawCapacity = size;
    frame.drawCursor = Align(sizeof(BoneUniforms));
    return frame.drawBuffer.IsValid() && frame.drawGroup.IsValid();
}

bool Renderer::EnsureClusterCapacity(
    FrameResources &frame, uint64_t lightCount, uint64_t clusterCount, uint64_t indexCount
)
{
    bool changed = false;
    if (lightCount > frame.lightsCapacity)
    {
        uint64_t capacity = std::max<uint64_t>(frame.lightsCapacity, 1);
        while (capacity < lightCount)
        {
            capacity *= 2;
        }
        m_device->DestroyBuffer(frame.lightsBuffer);
        frame.lightsBuffer = m_device->CreateBuffer(
            {capacity * sizeof(GpuLight), rhi::BufferUsage::Storage, rhi::MemoryUsage::CpuToGpu, "lights"}
        );
        frame.lightsCapacity = capacity;
        changed = true;
    }
    if (clusterCount > frame.gridCapacity)
    {
        m_device->DestroyBuffer(frame.clusterGridBuffer);
        frame.clusterGridBuffer = m_device->CreateBuffer(
            {clusterCount * sizeof(ClusterRange), rhi::BufferUsage::Storage, rhi::MemoryUsage::CpuToGpu, "cluster grid"}
        );
        frame.gridCapacity = clusterCount;
        changed = true;
    }
    if (indexCount > frame.indexCapacity)
    {
        uint64_t capacity = std::max<uint64_t>(frame.indexCapacity, 1);
        while (capacity < indexCount)
        {
            capacity *= 2;
        }
        m_device->DestroyBuffer(frame.clusterIndicesBuffer);
        frame.clusterIndicesBuffer = m_device->CreateBuffer(
            {capacity * sizeof(uint32_t), rhi::BufferUsage::Storage, rhi::MemoryUsage::CpuToGpu,
             "cluster light indices"}
        );
        frame.indexCapacity = capacity;
        changed = true;
    }
    if (changed)
    {
        CreateFrameBindGroup(frame);
    }
    return frame.lightsBuffer.IsValid() && frame.clusterGridBuffer.IsValid() && frame.clusterIndicesBuffer.IsValid();
}

Atmosphere Renderer::GetConfigAtmosphere() const
{
    Atmosphere atmosphere;
    atmosphere.skyZenith = m_config.skyZenith;
    atmosphere.skyHorizon = m_config.skyHorizon;
    atmosphere.fogColor = m_config.fogColor;
    atmosphere.fogDensity = m_config.fogDensity;
    atmosphere.ambientIntensity = m_config.ambientIntensity;
    return atmosphere;
}

uint64_t Renderer::Align(uint64_t size) const
{
    return (size + m_alignment - 1) / m_alignment * m_alignment;
}

void Renderer::PrepareFrame(FrameResources &frame, const CameraData &camera, const std::vector<LightData> &sceneLights)
{
    m_renderOrigin = camera.position;
    m_frameAtmosphere = m_hasAtmosphereOverride ? m_atmosphereOverride : GetConfigAtmosphere();
    m_hasAtmosphereOverride = false;
    m_frameWeather = m_hasWeather ? m_weather : WeatherState();
    m_hasWeather = false;
    const float sunScale = ApplyWeather(m_frameWeather, m_frameAtmosphere);

    // Night-only lights follow the darkness; at zero they are left out of the cluster grid.
    const float darkness = glm::clamp(m_frameAtmosphere.darkness, 0.0f, 1.0f);
    m_frameLights.clear();
    m_frameLights.reserve(sceneLights.size());
    for (const LightData &light : sceneLights)
    {
        if (!light.nightOnly)
        {
            m_frameLights.push_back(light);
            if (light.type == LightType::Directional)
            {
                m_frameLights.back().color *= sunScale;
            }
        }
        else if (darkness > 0.0f)
        {
            m_frameLights.push_back(light);
            m_frameLights.back().color *= darkness;
        }
    }
    const std::vector<LightData> &lights = m_frameLights;

    m_hasSun = false;
    for (const auto &light : lights)
    {
        if (light.type == LightType::Directional)
        {
            m_sun = light;
            m_hasSun = true;
            break;
        }
    }

    const float near = std::max(0.01f, camera.nearPlane);
    const float far = std::max(near + 1.0f, camera.farPlane);
    m_cascades = CascadeSetup();
    if (m_hasSun && m_config.shadows)
    {
        m_cascades = ComputeCascades(
            camera.viewMatrix, camera.projectionMatrix, near, far, m_sun.direction, m_config.shadowDistance,
            m_config.cascadeCount, m_config.shadowMapSize, m_config.shadowCasterDistance
        );
    }

    m_clusteredLights.clear();
    m_clusteredLights.reserve(lights.size());
    for (const auto &light : lights)
    {
        if (!IsClusteredLight(light.type))
        {
            continue;
        }
        GpuLight gpu;
        gpu.positionRange = glm::vec4(light.position - m_renderOrigin, EffectiveLightRange(light));
        gpu.colorIntensity = glm::vec4(light.color, 1.0f);
        if (light.type == LightType::Spot)
        {
            const float cosOuter = std::cos(std::clamp(light.outerConeAngle, 0.0f, 1.55f));
            const float cosInner = std::cos(std::clamp(light.innerConeAngle, 0.0f, 1.55f));
            gpu.directionOuter = glm::vec4(glm::normalize(light.direction), cosOuter);
            gpu.coneParams = glm::vec4(cosInner, 1.0f / std::max(cosInner - cosOuter, 1e-4f), 0.0f, 0.0f);
        }
        else
        {
            // cosOuter = -2 marks a point light: its cosine is never below -1.
            gpu.directionOuter = glm::vec4(0.0f, -1.0f, 0.0f, -2.0f);
            gpu.coneParams = glm::vec4(0.0f);
        }
        m_clusteredLights.push_back(gpu);
    }

    ClusterSettings clusterSettings;
    clusterSettings.countX = m_config.clusterCountX;
    clusterSettings.countY = m_config.clusterCountY;
    clusterSettings.countZ = m_config.clusterCountZ;
    clusterSettings.nearPlane = near;
    clusterSettings.farPlane = far;
    clusterSettings.maxLightsPerCluster = m_config.maxLightsPerCluster;
    ClusterAssignment assignment;
    if (!m_clusteredLights.empty())
    {
        assignment = AssignLightsToClusters(camera.viewMatrix, camera.projectionMatrix, clusterSettings, lights);
    }
    else
    {
        assignment.ranges.assign(
            size_t(m_config.clusterCountX) * m_config.clusterCountY * m_config.clusterCountZ, ClusterRange{}
        );
    }
    m_clusters = std::move(assignment.ranges);
    m_clusterIndices = std::move(assignment.lightIndices);
    m_stats.clusteredLights = static_cast<uint32_t>(m_clusteredLights.size());
    m_stats.overflowLights = assignment.overflowLights;

    if (!EnsureClusterCapacity(frame, m_clusteredLights.size(), m_clusters.size(), m_clusterIndices.size()))
    {
        return;
    }

    if (!m_clusteredLights.empty())
    {
        std::memcpy(
            m_device->MapBuffer(frame.lightsBuffer), m_clusteredLights.data(),
            m_clusteredLights.size() * sizeof(GpuLight)
        );
    }
    std::memcpy(
        m_device->MapBuffer(frame.clusterGridBuffer), m_clusters.data(), m_clusters.size() * sizeof(ClusterRange)
    );
    if (!m_clusterIndices.empty())
    {
        std::memcpy(
            m_device->MapBuffer(frame.clusterIndicesBuffer), m_clusterIndices.data(),
            m_clusterIndices.size() * sizeof(uint32_t)
        );
    }

    // Shaders work relative to the camera: `fromRender` takes a camera-relative position
    // back to the world, so every world-space matrix is multiplied by it.
    const glm::mat4 fromRender = glm::translate(glm::mat4(1.0f), m_renderOrigin);
    FrameUniforms uniforms{};
    uniforms.view = camera.viewMatrix * fromRender;
    uniforms.projection = kDepthZeroToOne * camera.projectionMatrix;
    uniforms.viewProjection = uniforms.projection * uniforms.view;
    uniforms.inverseViewProjection = glm::inverse(uniforms.viewProjection);
    uniforms.cameraPosition = glm::vec4(0.0f, 0.0f, 0.0f, m_config.exposure);
    if (m_hasSun)
    {
        uniforms.sunDirection = glm::vec4(glm::normalize(-m_sun.direction), 1.0f);
        uniforms.sunColor = glm::vec4(m_sun.color, 0.0f);
    }
    else
    {
        uniforms.sunDirection = glm::vec4(0.0f, 1.0f, 0.0f, 0.0f);
        uniforms.sunColor = glm::vec4(0.0f);
    }
    const Atmosphere &atmosphere = m_frameAtmosphere;
    uniforms.skyZenith = glm::vec4(atmosphere.skyZenith, atmosphere.ambientIntensity);
    uniforms.skyHorizon = glm::vec4(atmosphere.skyHorizon, atmosphere.fogDensity);
    uniforms.fogColor = glm::vec4(atmosphere.fogColor, m_config.tonemapper == Tonemapper::None ? 1.0f : 0.0f);
    for (uint32_t i = 0; i < Cascades; ++i)
    {
        // The cascades are fitted with OpenGL-style depth; the RHI clips at 0..1.
        uniforms.shadowMatrices[i] = kDepthZeroToOne * m_cascades.cascades[i].viewProjection * fromRender;
    }
    uniforms.cascadeSplits = m_cascades.splits;
    // Depth bias in shadow texels (the shader converts it to each cascade's depth units, so
    // it does not grow with the caster distance), normal offset in metres.
    uniforms.shadowParams =
        glm::vec4(4.0f, 0.15f, static_cast<float>(m_cascades.count), 1.0f / static_cast<float>(m_config.shadowMapSize));
    uniforms.clusterGrid = glm::vec4(
        static_cast<float>(m_config.clusterCountX), static_cast<float>(m_config.clusterCountY),
        static_cast<float>(m_config.clusterCountZ), static_cast<float>(m_config.maxLightsPerCluster)
    );
    const float logRatio = std::log(far / near);
    uniforms.clusterDepth = glm::vec4(near, far, logRatio, 1.0f / std::max(logRatio, 1e-6f));
    uniforms.screenParams = glm::vec4(
        static_cast<float>(frame.width), static_cast<float>(frame.height),
        1.0f / static_cast<float>(std::max(1u, frame.width)), 1.0f / static_cast<float>(std::max(1u, frame.height))
    );
    std::memcpy(m_device->MapBuffer(frame.frameBuffer), &uniforms, sizeof(uniforms));

    frame.drawCursor = Align(sizeof(BoneUniforms));
}

rhi::PipelineHandle Renderer::GetMainPipeline(const Shader &shader, const Mesh &mesh)
{
    const MainPipelineKey key{shader.GetId(), mesh.GetLayoutKey(), static_cast<uint8_t>(mesh.GetTopology())};
    auto it = m_mainPipelines.find(key);
    if (it != m_mainPipelines.end())
    {
        return it->second;
    }

    rhi::PipelineDesc desc;
    desc.vertexShader = shader.GetVertexModule();
    desc.fragmentShader = shader.GetFragmentModule();
    desc.topology = mesh.GetTopology();
    BuildVertexInput(mesh, desc.vertexLayout, &shader.GetVertexInputLocations());

    // Every shader input must be provided by the mesh or by the instance stream.
    bool inputsProvided = true;
    for (uint32_t location : shader.GetVertexInputLocations())
    {
        const auto found = std::find_if(
            desc.vertexLayout.attributes.begin(), desc.vertexLayout.attributes.end(),
            [&](const rhi::VertexAttribute &attribute) { return attribute.location == location; }
        );
        if (found == desc.vertexLayout.attributes.end())
        {
            GENI_LOG_ERROR(
                "Shader '%s' reads vertex input %u, which neither the mesh nor the instance stream provides; "
                "those meshes are skipped",
                shader.GetName().c_str(), location
            );
            inputsProvided = false;
            break;
        }
    }
    if (inputsProvided)
    {
        desc.targets.backbuffer = false;
        desc.targets.colorFormats = {rhi::Format::RGBA16Float};
        desc.targets.depthFormat = m_device->GetDepthFormat();
        desc.targets.sampleCount = m_config.msaaSamples;
        desc.bindGroupLayouts = {m_frameLayout, shader.GetMaterialLayout(), m_drawLayout};
        desc.debugName = shader.GetName().c_str();
        it = m_mainPipelines.emplace(key, m_device->CreatePipeline(desc)).first;
        return it->second;
    }
    m_mainPipelines[key] = {};
    return {};
}

rhi::PipelineHandle Renderer::GetShadowPipeline(const Mesh &mesh, bool skinned)
{
    const ShadowPipelineKey key{mesh.GetLayoutKey(), static_cast<uint8_t>(mesh.GetTopology()), skinned};
    auto it = m_shadowPipelines.find(key);
    if (it != m_shadowPipelines.end())
    {
        return it->second;
    }

    rhi::PipelineDesc desc;
    desc.vertexShader = skinned ? m_shadowSkinnedVertexModule : m_shadowVertexModule;
    desc.fragmentShader = m_shadowFragmentModule;
    desc.topology = mesh.GetTopology();
    desc.cullMode = rhi::CullMode::Front;
    desc.depthTest = true;
    desc.depthWrite = true;
    desc.depthCompare = rhi::CompareOp::LessOrEqual;
    desc.depthBiasConstant = 2.0f;
    desc.depthBiasSlope = 3.0f;
    desc.targets.backbuffer = false;
    desc.targets.depthFormat = m_device->GetSampledDepthFormat();
    desc.targets.sampleCount = 1;

    desc.vertexLayout.buffers.push_back({0, mesh.GetVertexLayout().stride, rhi::VertexStepMode::Vertex});
    desc.vertexLayout.buffers.push_back(
        {1, static_cast<uint32_t>(kShadowInstanceStride), rhi::VertexStepMode::Instance}
    );
    for (const auto &element : mesh.GetVertexLayout().elements)
    {
        if (element.index == VertexElement::JointsIndex || element.index == VertexElement::WeightsIndex ||
            element.index == VertexElement::PositionIndex)
        {
            if (element.size >= 1 && element.size <= 4)
            {
                desc.vertexLayout.attributes.push_back(
                    {element.index, ToVertexFormat(element.size), element.offset, 0}
                );
            }
        }
    }
    for (uint32_t i = 0; i < 4; ++i)
    {
        desc.vertexLayout.attributes.push_back({InstanceMatrixLocation + i, rhi::VertexFormat::Float4, i * 16, 1});
    }
    desc.vertexLayout.attributes.push_back({10, rhi::VertexFormat::Float1, 64, 1});
    if (skinned)
    {
        desc.bindGroupLayouts = {m_frameLayout, m_emptyLayout, m_drawLayout};
    }
    else
    {
        desc.bindGroupLayouts = {m_frameLayout};
    }
    desc.debugName = skinned ? "shadow (skinned)" : "shadow";
    it = m_shadowPipelines.emplace(key, m_device->CreatePipeline(desc)).first;
    return it->second;
}

rhi::PipelineHandle Renderer::GetSkyPipeline()
{
    if (m_skyPipeline.IsValid())
    {
        return m_skyPipeline;
    }
    rhi::PipelineDesc desc;
    desc.vertexShader = m_skyShader->GetVertexModule();
    desc.fragmentShader = m_skyShader->GetFragmentModule();
    desc.depthTest = false;
    desc.depthWrite = false;
    desc.cullMode = rhi::CullMode::None;
    desc.targets.backbuffer = false;
    desc.targets.colorFormats = {rhi::Format::RGBA16Float};
    desc.targets.depthFormat = m_device->GetDepthFormat();
    desc.targets.sampleCount = m_config.msaaSamples;
    desc.bindGroupLayouts = {m_frameLayout};
    desc.debugName = "sky";
    m_skyPipeline = m_device->CreatePipeline(desc);
    return m_skyPipeline;
}

rhi::PipelineHandle Renderer::GetSpritePipeline(SpriteBlend blend, bool lit)
{
    rhi::PipelineHandle &pipeline = m_spritePipelines[blend == SpriteBlend::Additive ? 1 : 0][lit ? 1 : 0];
    if (pipeline.IsValid())
    {
        return pipeline;
    }
    rhi::PipelineDesc desc;
    desc.vertexShader = lit ? m_spriteLitVertexModule : m_spriteVertexModule;
    desc.fragmentShader = lit ? m_spriteLitFragmentModule : m_spriteFragmentModule;
    desc.vertexLayout.buffers.push_back({0, static_cast<uint32_t>(sizeof(SpriteVertex)), rhi::VertexStepMode::Vertex});
    desc.vertexLayout.attributes.push_back({0, rhi::VertexFormat::Float4, 0, 0});
    desc.vertexLayout.attributes.push_back({1, rhi::VertexFormat::Float2, 16, 0});
    desc.vertexLayout.attributes.push_back({2, rhi::VertexFormat::Float4, 24, 0});
    // Sprites are hidden by opaque geometry in front of them but do not hide each other:
    // their order comes from the sort.
    desc.depthTest = true;
    desc.depthWrite = false;
    desc.depthCompare = rhi::CompareOp::LessOrEqual;
    desc.blend = blend == SpriteBlend::Additive ? rhi::BlendMode::Additive : rhi::BlendMode::Alpha;
    desc.cullMode = rhi::CullMode::None;
    desc.targets.backbuffer = false;
    desc.targets.colorFormats = {rhi::Format::RGBA16Float};
    desc.targets.depthFormat = m_device->GetDepthFormat();
    desc.targets.sampleCount = m_config.msaaSamples;
    desc.bindGroupLayouts = {m_frameLayout, m_tonemapLayout};
    desc.debugName = blend == SpriteBlend::Additive ? (lit ? "sprite lit additive" : "sprite additive")
                                                    : (lit ? "sprite lit" : "sprite");
    pipeline = m_device->CreatePipeline(desc);
    return pipeline;
}

rhi::PipelineHandle Renderer::GetTonemapPipeline()
{
    if (m_tonemapPipeline.IsValid())
    {
        return m_tonemapPipeline;
    }
    rhi::PipelineDesc desc;
    desc.vertexShader = m_tonemapVertexModule;
    desc.fragmentShader = m_tonemapFragmentModule;
    desc.depthTest = false;
    desc.depthWrite = false;
    desc.cullMode = rhi::CullMode::None;
    desc.targets.backbuffer = true;
    desc.targets.depthFormat = rhi::Format::Undefined;
    desc.targets.sampleCount = 1;
    desc.bindGroupLayouts = {m_frameLayout, m_tonemapLayout};
    desc.debugName = "tonemap";
    m_tonemapPipeline = m_device->CreatePipeline(desc);
    return m_tonemapPipeline;
}

void Renderer::BuildVertexInput(
    const Mesh &mesh, rhi::VertexLayoutDesc &layout, const std::vector<uint32_t> *shaderInputs
) const
{
    const VertexLayout &vertexLayout = mesh.GetVertexLayout();
    layout.buffers.push_back({0, vertexLayout.stride, rhi::VertexStepMode::Vertex});
    layout.buffers.push_back({1, static_cast<uint32_t>(kInstanceStride), rhi::VertexStepMode::Instance});

    auto wanted = [&](uint32_t location) {
        return !shaderInputs || std::find(shaderInputs->begin(), shaderInputs->end(), location) != shaderInputs->end();
    };
    for (const auto &element : vertexLayout.elements)
    {
        if (element.size < 1 || element.size > 4 || !wanted(element.index))
        {
            continue;
        }
        layout.attributes.push_back({element.index, ToVertexFormat(element.size), element.offset, 0});
    }
    for (uint32_t i = 0; i < 4; ++i)
    {
        if (wanted(InstanceMatrixLocation + i))
        {
            layout.attributes.push_back({InstanceMatrixLocation + i, rhi::VertexFormat::Float4, i * 16, 1});
        }
    }
}

void Renderer::RenderShadows(rhi::CommandList &cmd, std::vector<DrawItem> &casters)
{
    if (m_cascades.count == 0)
    {
        return;
    }
    FrameResources &frame = m_frames[m_device->GetFrameSlot()];

    struct ShadowRun
    {
        const Mesh *mesh = nullptr;
        bool skinned = false;
        uint32_t lod = 0;
        uint32_t firstIndex = 0;
        uint32_t indexCount = 0;
        uint32_t firstInstance = 0;
        uint32_t count = 0;
        uint32_t boneOffset = 0;
        rhi::PipelineHandle pipeline;
    };

    // Group by mesh so neighbours can share one instanced draw.
    std::sort(casters.begin(), casters.end(), [](const DrawItem &a, const DrawItem &b) {
        if (a.skinned != b.skinned)
        {
            return a.skinned < b.skinned;
        }
        if (a.mesh != b.mesh)
        {
            return std::less<const Mesh *>()(a.mesh, b.mesh);
        }
        return a.lod < b.lod;
    });

    for (uint32_t cascade = 0; cascade < m_cascades.count; ++cascade)
    {
        const rhi::TextureHandle target = m_shadowMaps[cascade];
        if (!target.IsValid())
        {
            continue;
        }
        // Sized before writing: growing the buffer replaces it and loses what was written.
        if (!EnsureShadowInstanceCapacity(frame, cascade, casters.size()))
        {
            continue;
        }
        // The cascade's near plane already sits RendererConfig::shadowCasterDistance beyond
        // the slice towards the sun, so this keeps every caster the pass can rasterise.
        const Frustum lightFrustum = Frustum::FromViewProjection(m_cascades.cascades[cascade].viewProjection, false);
        auto *mapped = static_cast<uint8_t *>(m_device->MapBuffer(frame.shadowInstanceBuffers[cascade]));

        std::vector<ShadowRun> runs;
        uint32_t written = 0;
        for (const DrawItem &item : casters)
        {
            if (item.radius > 0.0f && !lightFrustum.IntersectsSphere(item.center, item.radius))
            {
                continue;
            }
            ShadowInstance instance;
            instance.model = RelativeTo(item.command->modelMatrix, m_renderOrigin);
            instance.cascade = static_cast<float>(cascade);
            std::memcpy(mapped + size_t(written) * kShadowInstanceStride, &instance, kShadowInstanceStride);
            ++written;

            // Skinned items never merge: each has its own bone palette.
            const bool newRun = item.skinned || runs.empty() || runs.back().skinned || runs.back().mesh != item.mesh ||
                                runs.back().lod != item.lod;
            if (newRun)
            {
                ShadowRun run;
                run.mesh = item.mesh;
                run.skinned = item.skinned;
                run.lod = item.lod;
                run.firstIndex = item.firstIndex;
                run.indexCount = item.indexCount;
                run.firstInstance = written - 1;
                run.count = 1;
                run.boneOffset = item.boneOffset;
                run.pipeline = GetShadowPipeline(*item.mesh, item.skinned);
                runs.push_back(run);
            }
            else
            {
                ++runs.back().count;
            }
        }

        rhi::RenderPassDesc pass;
        pass.depthTarget = target;
        pass.colorLoad = rhi::LoadOp::DontCare;
        pass.depthLoad = rhi::LoadOp::Clear;
        pass.depthStore = rhi::StoreOp::Store;
        cmd.BeginRenderPass(pass);

        for (const ShadowRun &run : runs)
        {
            if (!run.pipeline.IsValid())
            {
                continue;
            }
            cmd.SetPipeline(run.pipeline);
            cmd.SetBindGroup(FrameSet, frame.frameGroup);
            if (run.skinned)
            {
                const uint32_t offset = run.boneOffset;
                cmd.SetBindGroup(DrawSet, frame.drawGroup, &offset, 1);
            }
            cmd.SetVertexBuffer(0, run.mesh->GetVertexBuffer());
            cmd.SetVertexBuffer(1, frame.shadowInstanceBuffers[cascade]);
            if (run.mesh->GetIndexBuffer().IsValid())
            {
                cmd.SetIndexBuffer(run.mesh->GetIndexBuffer(), rhi::IndexFormat::Uint32);
                cmd.DrawIndexedInstanced(run.indexCount, run.count, run.firstIndex, 0, run.firstInstance);
            }
            else
            {
                cmd.DrawInstanced(static_cast<uint32_t>(run.mesh->GetVertexCount()), run.count, run.firstInstance);
            }
            ++m_stats.shadowDraws;
        }
        cmd.EndRenderPass();
    }
}

void Renderer::RenderMain(rhi::CommandList &cmd, const std::vector<DrawItem> &items)
{
    FrameResources &frame = m_frames[m_device->GetFrameSlot()];

    rhi::RenderPassDesc pass;
    pass.colorTarget = m_config.msaaSamples > 1 ? frame.msaaColor : frame.hdrColor;
    pass.resolveTarget = m_config.msaaSamples > 1 ? frame.hdrColor : rhi::TextureHandle{};
    pass.depthTarget = frame.depth;
    pass.colorLoad = rhi::LoadOp::Clear;
    pass.depthLoad = rhi::LoadOp::Clear;
    pass.depthStore = rhi::StoreOp::DontCare;
    // Without a sky the clear colour is what shows; it is given in sRGB.
    const glm::vec3 clear =
        m_config.sky ? m_frameAtmosphere.skyHorizon : glm::vec3(SrgbToLinear(glm::vec4(m_config.clearColor, 1.0f)));
    pass.clearColor[0] = clear.x;
    pass.clearColor[1] = clear.y;
    pass.clearColor[2] = clear.z;
    pass.clearColor[3] = 1.0f;
    cmd.BeginRenderPass(pass);

    if (m_config.sky && m_skyPipeline.IsValid())
    {
        cmd.SetPipeline(m_skyPipeline);
        cmd.SetBindGroup(FrameSet, frame.frameGroup);
        cmd.Draw(3);
        ++m_stats.fullscreenDraws;
    }

    for (size_t i = 0; i < items.size();)
    {
        const DrawItem &item = items[i];
        const rhi::PipelineHandle pipeline = GetMainPipeline(*item.command->material->GetShader(), *item.mesh);
        if (!pipeline.IsValid())
        {
            ++i;
            continue;
        }
        const rhi::BindGroupHandle materialGroup = item.command->material->PrepareBindGroup(*this);
        if (!materialGroup.IsValid())
        {
            ++i;
            continue;
        }

        cmd.SetPipeline(pipeline);
        cmd.SetBindGroup(FrameSet, frame.frameGroup);
        cmd.SetBindGroup(MaterialSet, materialGroup);
        item.mesh->Bind(cmd);
        cmd.SetVertexBuffer(1, frame.instanceBuffer);

        auto drawInstances = [&](uint32_t instances, uint32_t firstInstance) {
            if (item.mesh->GetIndexBuffer().IsValid())
            {
                cmd.DrawIndexedInstanced(item.indexCount, instances, item.firstIndex, 0, firstInstance);
            }
            else
            {
                cmd.DrawInstanced(static_cast<uint32_t>(item.mesh->GetVertexCount()), instances, firstInstance);
            }
        };

        if (item.skinned)
        {
            const uint32_t offset = item.boneOffset;
            cmd.SetBindGroup(DrawSet, frame.drawGroup, &offset, 1);
            drawInstances(1, item.instanceIndex);
            ++m_stats.mainDraws;
            ++m_stats.instances;
            ++m_stats.lodInstances[item.lod];
            ++i;
            continue;
        }

        // Merge consecutive commands that share mesh, material and pipeline into one
        // instanced draw. Their instance indices are contiguous because they are sorted.
        size_t count = 1;
        while (i + count < items.size())
        {
            const DrawItem &next = items[i + count];
            if (next.skinned || next.mesh != item.mesh || next.material != item.material || next.lod != item.lod)
            {
                break;
            }
            ++count;
        }
        const uint32_t offset = item.boneOffset;
        cmd.SetBindGroup(DrawSet, frame.drawGroup, &offset, 1);
        drawInstances(static_cast<uint32_t>(count), item.instanceIndex);
        ++m_stats.mainDraws;
        m_stats.instances += static_cast<uint32_t>(count);
        m_stats.lodInstances[item.lod] += static_cast<uint32_t>(count);
        i += count;
    }

    RenderSprites(cmd, frame);
    cmd.EndRenderPass();
}

void Renderer::RenderTonemap(rhi::CommandList &cmd, bool sceneDrawn)
{
    FrameResources &frame = m_frames[m_device->GetFrameSlot()];
    rhi::RenderPassDesc pass;
    pass.backbufferDepth = false;
    pass.colorLoad = rhi::LoadOp::Clear;
    pass.colorStore = rhi::StoreOp::Store;
    pass.clearColor[0] = 0.0f;
    pass.clearColor[1] = 0.0f;
    pass.clearColor[2] = 0.0f;
    pass.clearColor[3] = 1.0f;
    cmd.BeginRenderPass(pass);
    if (sceneDrawn && m_tonemapPipeline.IsValid() && frame.frameGroup.IsValid() && frame.tonemapGroup.IsValid())
    {
        const SceneViewport rect = GetSceneRect();
        cmd.SetViewport(
            static_cast<float>(rect.x), static_cast<float>(rect.y), static_cast<float>(rect.width),
            static_cast<float>(rect.height)
        );
        cmd.SetPipeline(m_tonemapPipeline);
        cmd.SetBindGroup(0, frame.frameGroup);
        cmd.SetBindGroup(1, frame.tonemapGroup);
        cmd.Draw(3);
        ++m_stats.fullscreenDraws;

        // Whatever draws next (Application::Render, Dear ImGui) gets the whole backbuffer.
        uint32_t width = 0;
        uint32_t height = 0;
        m_device->GetBackbufferSize(width, height);
        cmd.SetViewport(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height));
    }
    // Left open, even when the scene could not be drawn: Application::Render and Dear
    // ImGui draw into this pass.
}

SceneViewport Renderer::GetSceneRect() const
{
    uint32_t width = 0;
    uint32_t height = 0;
    if (m_device)
    {
        m_device->GetBackbufferSize(width, height);
    }
    width = std::max(1u, width);
    height = std::max(1u, height);

    SceneViewport rect;
    rect.width = width;
    rect.height = height;
    if (m_sceneViewport.width > 0 && m_sceneViewport.height > 0 && m_sceneViewport.x < width &&
        m_sceneViewport.y < height)
    {
        rect.x = m_sceneViewport.x;
        rect.y = m_sceneViewport.y;
        rect.width = std::min(m_sceneViewport.width, width - rect.x);
        rect.height = std::min(m_sceneViewport.height, height - rect.y);
    }
    return rect;
}

void Renderer::RenderFrame(
    rhi::CommandList &cmd, const std::vector<RenderCommand> &commands, const CameraData &camera,
    const std::vector<LightData> &lights, const std::vector<SpriteCommand> &sprites
)
{
    GENI_PROFILE_SCOPE("Renderer::RenderFrame");
    ++m_frameCounter;
    const bool sceneDrawn = RenderScene(cmd, commands, camera, lights, sprites);
    RenderTonemap(cmd, sceneDrawn);
}

bool Renderer::RenderScene(
    rhi::CommandList &cmd, const std::vector<RenderCommand> &commands, const CameraData &camera,
    const std::vector<LightData> &lights, const std::vector<SpriteCommand> &sprites
)
{
    const SceneViewport rect = GetSceneRect();
    const uint32_t width = rect.width;
    const uint32_t height = rect.height;

    FrameResources &frame = m_frames[m_device->GetFrameSlot()];
    m_stats = RenderStats();
    if (!CreateFrameResources(frame, width, height))
    {
        return false;
    }
    PrepareFrame(frame, camera, lights);

    m_stats.submitted = static_cast<uint32_t>(commands.size());

    // Every drawable command with its world bounds. Shadow casters come from this list,
    // so objects the camera does not see still cast into the visible area.
    // Pixels covered by one metre one metre from the camera, for LOD selection.
    const float pixelsPerUnit = 0.5f * static_cast<float>(height) * std::abs(camera.projectionMatrix[1][1]);
    std::vector<DrawItem> all;
    all.reserve(commands.size());
    for (const auto &command : commands)
    {
        if (!command.mesh || !command.material || !command.material->GetShader())
        {
            continue;
        }
        DrawItem item;
        item.command = &command;
        item.shaderId = command.material->GetShader()->GetId();
        item.material = command.material;
        item.mesh = command.mesh;
        item.skinned = command.bonePalette != nullptr && !command.bonePalette->empty();
        WorldBounds(command, item.center, item.radius);
        item.distance = glm::distance(item.center, camera.position);
        SelectLod(item, pixelsPerUnit);
        all.push_back(item);
    }

    // Bone palettes, for the main and the shadow passes.
    uint64_t neededDraw = frame.drawCursor;
    for (const auto &item : all)
    {
        if (item.skinned)
        {
            neededDraw += Align(sizeof(BoneUniforms));
        }
    }
    if (!EnsureDrawCapacity(frame, neededDraw))
    {
        return false;
    }
    auto *drawBase = static_cast<uint8_t *>(m_device->MapBuffer(frame.drawBuffer));
    for (auto &item : all)
    {
        if (!item.skinned)
        {
            item.boneOffset = 0;
            continue;
        }
        item.boneOffset = static_cast<uint32_t>(frame.drawCursor);
        const size_t count = std::min<size_t>(item.command->bonePalette->size(), MaxBones);
        std::memcpy(drawBase + frame.drawCursor, item.command->bonePalette->data(), count * sizeof(glm::mat4));
        frame.drawCursor += Align(sizeof(BoneUniforms));
    }

    // Camera culling.
    const glm::mat4 viewProjection = kDepthZeroToOne * camera.projectionMatrix * camera.viewMatrix;
    const Frustum cameraFrustum = Frustum::FromViewProjection(viewProjection);
    std::vector<DrawItem> items;
    items.reserve(all.size());
    for (const auto &item : all)
    {
        if (item.radius > 0.0f && !cameraFrustum.IntersectsSphere(item.center, item.radius))
        {
            ++m_stats.culled;
            continue;
        }
        items.push_back(item);
    }

    std::sort(items.begin(), items.end(), [](const DrawItem &a, const DrawItem &b) {
        if (a.shaderId != b.shaderId)
        {
            return a.shaderId < b.shaderId;
        }
        if (a.material != b.material)
        {
            return std::less<const Material *>()(a.material, b.material);
        }
        if (a.mesh != b.mesh)
        {
            return std::less<const Mesh *>()(a.mesh, b.mesh);
        }
        if (a.skinned != b.skinned)
        {
            return a.skinned < b.skinned;
        }
        if (a.lod != b.lod)
        {
            return a.lod < b.lod;
        }
        return a.distance < b.distance;
    });
    m_stats.visible = static_cast<uint32_t>(items.size());

    // Pack instance transforms.
    if (!EnsureInstanceCapacity(frame, items.size()))
    {
        return false;
    }
    auto *instanceBase = static_cast<uint8_t *>(m_device->MapBuffer(frame.instanceBuffer));
    for (size_t i = 0; i < items.size(); ++i)
    {
        items[i].instanceIndex = static_cast<uint32_t>(i);
        const glm::mat4 model = RelativeTo(items[i].command->modelMatrix, m_renderOrigin);
        std::memcpy(instanceBase + i * kInstanceStride, &model, kInstanceStride);
    }

    if (!PrepareSprites(frame, sprites, camera))
    {
        m_spriteBatches.clear();
    }

    RenderShadows(cmd, all);
    RenderMain(cmd, items);
    return true;
}

bool Renderer::EnsureSpriteCapacity(FrameResources &frame, uint64_t quads)
{
    if (quads > frame.spriteQuadCapacity)
    {
        uint64_t capacity = std::max<uint64_t>(frame.spriteQuadCapacity, 256);
        while (capacity < quads)
        {
            capacity *= 2;
        }
        m_device->DestroyBuffer(frame.spriteVertexBuffer);
        frame.spriteVertexBuffer = m_device->CreateBuffer(
            {capacity * kSpriteQuadBytes, rhi::BufferUsage::Vertex, rhi::MemoryUsage::CpuToGpu, "sprite vertices"}
        );
        frame.spriteQuadCapacity = frame.spriteVertexBuffer.IsValid() ? capacity : 0;
        if (!frame.spriteVertexBuffer.IsValid())
        {
            return false;
        }
    }
    if (quads > m_spriteIndexCapacity)
    {
        uint64_t capacity = std::max<uint64_t>(m_spriteIndexCapacity, 256);
        while (capacity < quads)
        {
            capacity *= 2;
        }
        // Every frame slot shares the index buffer. The old one is destroyed once the GPU is
        // done with it; the new one is written before any frame uses it.
        m_device->DestroyBuffer(m_spriteIndexBuffer);
        m_spriteIndexBuffer = m_device->CreateBuffer(
            {capacity * 6 * sizeof(uint32_t), rhi::BufferUsage::Index, rhi::MemoryUsage::CpuToGpu, "sprite indices"}
        );
        if (!m_spriteIndexBuffer.IsValid())
        {
            m_spriteIndexCapacity = 0;
            return false;
        }
        auto *indices = static_cast<uint32_t *>(m_device->MapBuffer(m_spriteIndexBuffer));
        for (uint64_t q = 0; q < capacity; ++q)
        {
            const uint32_t v = static_cast<uint32_t>(q * 4);
            uint32_t *out = indices + q * 6;
            out[0] = v;
            out[1] = v + 1;
            out[2] = v + 2;
            out[3] = v;
            out[4] = v + 2;
            out[5] = v + 3;
        }
        m_spriteIndexCapacity = capacity;
    }
    return true;
}

rhi::BindGroupHandle Renderer::GetSpriteGroup(const Texture *texture, SpriteFilter filter)
{
    const rhi::TextureHandle handle =
        texture && texture->GetHandle().IsValid() ? texture->GetHandle() : m_spriteWhiteTexture;
    const SpriteGroupKey key{handle.index, handle.generation, filter};
    auto it = m_spriteGroups.find(key);
    if (it == m_spriteGroups.end())
    {
        rhi::BindGroupDesc desc;
        desc.layout = m_tonemapLayout; // one sampled texture at binding 0
        rhi::BindGroupEntry entry;
        entry.binding = 0;
        entry.texture = handle;
        entry.sampler = filter == SpriteFilter::Nearest ? m_spriteNearestSampler : m_spriteLinearSampler;
        desc.entries.push_back(entry);
        SpriteGroup group;
        group.group = m_device->CreateBindGroup(desc);
        it = m_spriteGroups.emplace(key, group).first;
    }
    it->second.lastUsedFrame = m_frameCounter;
    return it->second.group;
}

bool Renderer::PrepareSprites(
    FrameResources &frame, const std::vector<SpriteCommand> &sprites, const CameraData &camera
)
{
    GENI_PROFILE_SCOPE("Renderer::PrepareSprites");
    m_spriteBatches.clear();

    // Groups of textures that are no longer drawn (destruction waits for the GPU).
    if (m_frameCounter % 60 == 0)
    {
        for (auto it = m_spriteGroups.begin(); it != m_spriteGroups.end();)
        {
            if (it->second.lastUsedFrame + kSpriteGroupLifetime < m_frameCounter)
            {
                m_device->DestroyBindGroup(it->second.group);
                it = m_spriteGroups.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }
    if (sprites.empty())
    {
        return true;
    }

    struct SpriteItem
    {
        const SpriteCommand *command = nullptr;
        float viewDepth = 0.0f;
        uint32_t order = 0;
    };
    const glm::mat4 viewProjection = kDepthZeroToOne * camera.projectionMatrix * camera.viewMatrix;
    const Frustum frustum = Frustum::FromViewProjection(viewProjection);
    std::vector<SpriteItem> items;
    items.reserve(sprites.size());
    uint64_t quadCount = 0;
    for (size_t i = 0; i < sprites.size(); ++i)
    {
        const SpriteCommand &command = sprites[i];
        const glm::vec3 center = glm::vec3(command.modelMatrix * glm::vec4(command.boundsCenter, 1.0f));
        if (command.boundsRadius > 0.0f &&
            !frustum.IntersectsSphere(center, command.boundsRadius * MaxScale(command.modelMatrix)))
        {
            ++m_stats.spritesCulled;
            continue;
        }
        SpriteItem item;
        item.command = &command;
        // View space looks down -Z: the most negative depth is the farthest.
        item.viewDepth = (camera.viewMatrix * glm::vec4(center, 1.0f)).z;
        item.order = static_cast<uint32_t>(i);
        items.push_back(item);
        quadCount += command.quadCount;
    }
    if (items.empty())
    {
        return true;
    }
    std::sort(items.begin(), items.end(), [](const SpriteItem &a, const SpriteItem &b) {
        if (a.command->layer != b.command->layer)
        {
            return a.command->layer < b.command->layer;
        }
        if (a.viewDepth != b.viewDepth)
        {
            return a.viewDepth < b.viewDepth;
        }
        return a.order < b.order;
    });

    if (!EnsureSpriteCapacity(frame, quadCount))
    {
        return false;
    }
    auto *vertices = static_cast<SpriteVertex *>(m_device->MapBuffer(frame.spriteVertexBuffer));
    uint32_t written = 0;
    for (const SpriteItem &item : items)
    {
        const SpriteCommand &command = *item.command;
        const glm::mat4 model = RelativeTo(command.modelMatrix, m_renderOrigin);
        const glm::vec3 axisX(model[0]);
        const glm::vec3 axisY(model[1]);
        const glm::vec3 origin(model[3]);
        const float decode =
            command.texture && command.texture->GetHandle().IsValid() && !IsSrgbFormat(command.texture->GetFormat())
                ? 1.0f
                : 0.0f;

        const rhi::BindGroupHandle group = GetSpriteGroup(command.texture, command.filter);
        if (!group.IsValid())
        {
            continue;
        }
        if (m_spriteBatches.empty() || m_spriteBatches.back().group != group ||
            m_spriteBatches.back().blend != command.blend || m_spriteBatches.back().lit != command.lit)
        {
            m_spriteBatches.push_back({group, command.blend, command.lit, written, 0});
        }

        if (command.billboard)
        {
            WriteBillboards(command, model, camera, decode, vertices + static_cast<uint64_t>(written) * 4);
            written += command.quadCount;
            m_spriteBatches.back().quadCount = written - m_spriteBatches.back().firstQuad;
            continue;
        }

        for (uint32_t q = 0; q < command.quadCount; ++q)
        {
            const SpriteQuad &quad = command.quads[q];
            SpriteVertex *out = vertices + static_cast<uint64_t>(written) * 4;
            const glm::vec3 x0 = origin + axisX * quad.min.x;
            const glm::vec3 x1 = origin + axisX * quad.max.x;
            const glm::vec3 y0 = axisY * quad.min.y;
            const glm::vec3 y1 = axisY * quad.max.y;
            // Bottom-left, bottom-right, top-right, top-left: counter-clockwise from +Z.
            out[0] = {glm::vec4(x0 + y0, decode), {quad.uvTopLeft.x, quad.uvBottomRight.y}, quad.color};
            out[1] = {glm::vec4(x1 + y0, decode), quad.uvBottomRight, quad.color};
            out[2] = {glm::vec4(x1 + y1, decode), {quad.uvBottomRight.x, quad.uvTopLeft.y}, quad.color};
            out[3] = {glm::vec4(x0 + y1, decode), quad.uvTopLeft, quad.color};
            ++written;
        }
        m_spriteBatches.back().quadCount = written - m_spriteBatches.back().firstQuad;
    }
    m_stats.sprites = written;
    return true;
}

void Renderer::WriteBillboards(
    const SpriteCommand &command, const glm::mat4 &relativeModel, const CameraData &camera, float decode,
    SpriteVertex *out
)
{
    // The camera's right and up axes in world space: the first two rows of the view
    // rotation.
    const glm::mat4 &view = camera.viewMatrix;
    const glm::vec3 right(view[0][0], view[1][0], view[2][0]);
    const glm::vec3 up(view[0][1], view[1][1], view[2][1]);

    const bool sort = command.blend == SpriteBlend::Alpha && command.quadCount > 1;
    if (sort)
    {
        m_billboardOrder.resize(command.quadCount);
        const glm::mat4 viewModel = view * command.modelMatrix;
        for (uint32_t q = 0; q < command.quadCount; ++q)
        {
            const float depth = (viewModel * glm::vec4(command.quads[q].center, 1.0f)).z;
            m_billboardOrder[q] = {depth, q};
        }
        // View space looks down -Z: the most negative depth is the farthest, drawn first.
        std::sort(m_billboardOrder.begin(), m_billboardOrder.end(), [](const auto &a, const auto &b) {
            return a.first < b.first;
        });
    }

    for (uint32_t i = 0; i < command.quadCount; ++i)
    {
        const SpriteQuad &quad = command.quads[sort ? m_billboardOrder[i].second : i];
        const glm::vec3 center = glm::vec3(relativeModel * glm::vec4(quad.center, 1.0f));
        const float c = std::cos(quad.rotation);
        const float s = std::sin(quad.rotation);
        const glm::vec3 axisX = right * c + up * s;
        const glm::vec3 axisY = up * c - right * s;
        const glm::vec3 x0 = center + axisX * quad.min.x;
        const glm::vec3 x1 = center + axisX * quad.max.x;
        const glm::vec3 y0 = axisY * quad.min.y;
        const glm::vec3 y1 = axisY * quad.max.y;
        SpriteVertex *v = out + static_cast<uint64_t>(i) * 4;
        v[0] = {glm::vec4(x0 + y0, decode), {quad.uvTopLeft.x, quad.uvBottomRight.y}, quad.color};
        v[1] = {glm::vec4(x1 + y0, decode), quad.uvBottomRight, quad.color};
        v[2] = {glm::vec4(x1 + y1, decode), {quad.uvBottomRight.x, quad.uvTopLeft.y}, quad.color};
        v[3] = {glm::vec4(x0 + y1, decode), quad.uvTopLeft, quad.color};
    }
}

void Renderer::RenderSprites(rhi::CommandList &cmd, FrameResources &frame)
{
    if (m_spriteBatches.empty() || !m_spritePipelines[0][0].IsValid() || !frame.spriteVertexBuffer.IsValid() ||
        !m_spriteIndexBuffer.IsValid())
    {
        return;
    }
    SpriteBlend bound = SpriteBlend::Alpha;
    bool boundLit = false;
    cmd.SetPipeline(m_spritePipelines[0][0]);
    cmd.SetBindGroup(FrameSet, frame.frameGroup);
    cmd.SetVertexBuffer(0, frame.spriteVertexBuffer);
    cmd.SetIndexBuffer(m_spriteIndexBuffer, rhi::IndexFormat::Uint32);
    for (const SpriteBatch &batch : m_spriteBatches)
    {
        if (batch.quadCount == 0)
        {
            continue;
        }
        if (batch.blend != bound || batch.lit != boundLit)
        {
            bound = batch.blend;
            boundLit = batch.lit;
            cmd.SetPipeline(m_spritePipelines[bound == SpriteBlend::Additive ? 1 : 0][boundLit ? 1 : 0]);
            // A new pipeline may disturb earlier bindings on some backends; bind them again.
            cmd.SetBindGroup(FrameSet, frame.frameGroup);
        }
        cmd.SetBindGroup(1, batch.group);
        cmd.DrawIndexed(batch.quadCount * 6, batch.firstQuad * 6, 0);
        ++m_stats.spriteBatches;
    }
}

void Renderer::SelectLod(DrawItem &item, float pixelsPerUnit) const
{
    const auto &lods = item.mesh->GetLods();
    item.lod = 0;
    if (lods.empty())
    {
        item.firstIndex = 0;
        item.indexCount = 0;
        return;
    }
    // Skinned meshes deform; their simplification error is only valid in the bind pose.
    if (!item.skinned && lods.size() > 1 && m_config.lodErrorPixels > 0.0f)
    {
        const float distance = std::max(item.distance - item.radius, 0.0f);
        if (distance > 0.0f)
        {
            const float scale = MaxScale(item.command->modelMatrix);
            const float pixelsPerModelUnit = pixelsPerUnit * scale / distance;
            for (uint32_t i = static_cast<uint32_t>(lods.size()) - 1; i > 0; --i)
            {
                if (lods[i].error * pixelsPerModelUnit <= m_config.lodErrorPixels)
                {
                    item.lod = i;
                    break;
                }
            }
        }
    }
    item.firstIndex = lods[item.lod].firstIndex;
    item.indexCount = lods[item.lod].indexCount;
}

void Renderer::ReleasePipelines(uint64_t shaderId)
{
    for (auto it = m_mainPipelines.begin(); it != m_mainPipelines.end();)
    {
        if (it->first.shader == shaderId)
        {
            if (m_device)
            {
                m_device->DestroyPipeline(it->second);
            }
            it = m_mainPipelines.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

} // namespace Geni