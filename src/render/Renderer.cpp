#include "render/Renderer.h"
#include "core/Log.h"
#include "core/Profiler.h"
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
#include "tonemap_frag.spv.h"
#include "tonemap_vert.spv.h"

namespace Geni
{

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
    m_skyShader = Shader::Create(
        geni_spirv_sky_vert, sizeof(geni_spirv_sky_vert), geni_spirv_sky_frag, sizeof(geni_spirv_sky_frag),
        "builtin/sky"
    );
    if (!m_defaultShader || !m_skinnedShader || !m_skyShader)
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

    if (!GetSkyPipeline().IsValid() || !GetTonemapPipeline().IsValid())
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
    m_skyPipeline = {};
    m_tonemapPipeline = {};

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

uint64_t Renderer::Align(uint64_t size) const
{
    return (size + m_alignment - 1) / m_alignment * m_alignment;
}

void Renderer::PrepareFrame(FrameResources &frame, const CameraData &camera, const std::vector<LightData> &lights)
{

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
        gpu.positionRange = glm::vec4(light.position, EffectiveLightRange(light));
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

    FrameUniforms uniforms{};
    uniforms.view = camera.viewMatrix;
    uniforms.projection = kDepthZeroToOne * camera.projectionMatrix;
    uniforms.viewProjection = uniforms.projection * uniforms.view;
    uniforms.inverseViewProjection = glm::inverse(uniforms.viewProjection);
    uniforms.cameraPosition = glm::vec4(camera.position, m_config.exposure);
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
    uniforms.skyZenith = glm::vec4(m_config.skyZenith, m_config.ambientIntensity);
    uniforms.skyHorizon = glm::vec4(m_config.skyHorizon, m_config.fogDensity);
    uniforms.fogColor = glm::vec4(m_config.fogColor, 0.0f);
    for (uint32_t i = 0; i < Cascades; ++i)
    {
        // The cascades are fitted with OpenGL-style depth; the RHI clips at 0..1.
        uniforms.shadowMatrices[i] = kDepthZeroToOne * m_cascades.cascades[i].viewProjection;
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
        return std::less<const Mesh *>()(a.mesh, b.mesh);
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
            instance.model = item.command->modelMatrix;
            instance.cascade = static_cast<float>(cascade);
            std::memcpy(mapped + size_t(written) * kShadowInstanceStride, &instance, kShadowInstanceStride);
            ++written;

            // Skinned items never merge: each has its own bone palette.
            const bool newRun = item.skinned || runs.empty() || runs.back().skinned || runs.back().mesh != item.mesh;
            if (newRun)
            {
                ShadowRun run;
                run.mesh = item.mesh;
                run.skinned = item.skinned;
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
                cmd.DrawIndexedInstanced(run.mesh->GetIndexCount(), run.count, 0, 0, run.firstInstance);
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
    pass.clearColor[0] = m_config.skyHorizon.x;
    pass.clearColor[1] = m_config.skyHorizon.y;
    pass.clearColor[2] = m_config.skyHorizon.z;
    pass.clearColor[3] = 1.0f;
    cmd.BeginRenderPass(pass);

    if (m_skyPipeline.IsValid())
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
                cmd.DrawIndexedInstanced(
                    static_cast<uint32_t>(item.mesh->GetIndexCount()), instances, 0, 0, firstInstance
                );
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
            ++i;
            continue;
        }

        // Merge consecutive commands that share mesh, material and pipeline into one
        // instanced draw. Their instance indices are contiguous because they are sorted.
        size_t count = 1;
        while (i + count < items.size())
        {
            const DrawItem &next = items[i + count];
            if (next.skinned || next.mesh != item.mesh || next.material != item.material)
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
        i += count;
    }

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
    const std::vector<LightData> &lights
)
{
    GENI_PROFILE_SCOPE("Renderer::RenderFrame");
    const bool sceneDrawn = RenderScene(cmd, commands, camera, lights);
    RenderTonemap(cmd, sceneDrawn);
}

bool Renderer::RenderScene(
    rhi::CommandList &cmd, const std::vector<RenderCommand> &commands, const CameraData &camera,
    const std::vector<LightData> &lights
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
        std::memcpy(instanceBase + i * kInstanceStride, &items[i].command->modelMatrix, kInstanceStride);
    }

    RenderShadows(cmd, all);
    RenderMain(cmd, items);
    return true;
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