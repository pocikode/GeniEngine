#include "render/Renderer.h"
#include "core/Log.h"
#include "core/Profiler.h"
#include "render/Material.h"
#include "render/Mesh.h"
#include "render/Shader.h"
#include "render/ShaderInterface.h"
#include "rhi/Device.h"

#include <algorithm>
#include <cstdint>
#include <cstring>

// Built-in shaders, compiled at build time (geni_embed_shaders in CMakeLists.txt).
#include "default_frag.spv.h"
#include "default_vert.spv.h"

namespace Geni
{

using namespace ShaderInterface;

namespace
{
// CameraComponent builds OpenGL-style projections (depth -1..1). The RHI's clip space has
// depth 0..1: z' = 0.5 * z + 0.5 * w.
const glm::mat4 kDepthZeroToOne(1.0f, 0.0f, 0.0f, 0.0f, //
                                0.0f, 1.0f, 0.0f, 0.0f, //
                                0.0f, 0.0f, 0.5f, 0.0f, //
                                0.0f, 0.0f, 0.5f, 1.0f);

constexpr uint64_t kInitialDrawCapacity = 1u << 20;
} // namespace

bool Renderer::Init(rhi::Device *device)
{
    m_device = device;
    m_alignment = std::max<uint32_t>(16, device->GetUniformBufferAlignment());

    rhi::BindGroupLayoutDesc frameLayout;
    frameLayout.entries.push_back({FrameBinding, rhi::BindingType::UniformBuffer,
                                   rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment});
    m_frameLayout = m_device->CreateBindGroupLayout(frameLayout);

    rhi::BindGroupLayoutDesc drawLayout;
    drawLayout.entries.push_back({DrawBinding, rhi::BindingType::DynamicUniformBuffer,
                                  rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment});
    drawLayout.entries.push_back({BonesBinding, rhi::BindingType::DynamicUniformBuffer, rhi::ShaderStage::Vertex});
    m_drawLayout = m_device->CreateBindGroupLayout(drawLayout);

    // Trilinear, repeating: what the OpenGL renderer used for every texture.
    m_sampler = m_device->CreateSampler(rhi::SamplerDesc());

    m_frames.resize(m_device->GetFramesInFlight());
    for (auto &frame : m_frames)
    {
        frame.frameBuffer = m_device->CreateBuffer(
            {sizeof(FrameUniforms), rhi::BufferUsage::Uniform, rhi::MemoryUsage::CpuToGpu, "frame uniforms"});
        rhi::BindGroupDesc group;
        group.layout = m_frameLayout;
        group.entries.push_back({FrameBinding, frame.frameBuffer, 0, sizeof(FrameUniforms)});
        frame.frameGroup = m_device->CreateBindGroup(group);
        if (!frame.frameGroup.IsValid() || !CreateDrawBuffer(frame, kInitialDrawCapacity))
        {
            return false;
        }
    }

    m_defaultShader = Shader::Create(geni_spirv_default_vert, sizeof(geni_spirv_default_vert), geni_spirv_default_frag,
                                     sizeof(geni_spirv_default_frag), "builtin/default");
    return m_frameLayout.IsValid() && m_drawLayout.IsValid() && m_sampler.IsValid() && m_defaultShader != nullptr;
}

void Renderer::Shutdown()
{
    if (!m_device)
    {
        return;
    }
    m_defaultShader.reset();
    for (auto &entry : m_pipelines)
    {
        m_device->DestroyPipeline(entry.second);
    }
    m_pipelines.clear();
    for (auto &frame : m_frames)
    {
        m_device->DestroyBindGroup(frame.frameGroup);
        m_device->DestroyBuffer(frame.frameBuffer);
        m_device->DestroyBindGroup(frame.drawGroup);
        m_device->DestroyBuffer(frame.drawBuffer);
    }
    m_frames.clear();
    m_device->DestroySampler(m_sampler);
    m_device->DestroyBindGroupLayout(m_drawLayout);
    m_device->DestroyBindGroupLayout(m_frameLayout);
    m_device = nullptr;
}

uint64_t Renderer::Align(uint64_t size) const
{
    return (size + m_alignment - 1) / m_alignment * m_alignment;
}

bool Renderer::CreateDrawBuffer(FrameResources &frame, uint64_t capacity)
{
    // Earlier draws of this frame keep using the old buffer; it is destroyed once the GPU
    // is done with it.
    m_device->DestroyBindGroup(frame.drawGroup);
    m_device->DestroyBuffer(frame.drawBuffer);

    frame.drawBuffer =
        m_device->CreateBuffer({capacity, rhi::BufferUsage::Uniform, rhi::MemoryUsage::CpuToGpu, "draw uniforms"});
    rhi::BindGroupDesc group;
    group.layout = m_drawLayout;
    group.entries.push_back({DrawBinding, frame.drawBuffer, 0, sizeof(DrawUniforms)});
    group.entries.push_back({BonesBinding, frame.drawBuffer, 0, sizeof(BoneUniforms)});
    frame.drawGroup = m_device->CreateBindGroup(group);
    frame.drawCapacity = capacity;
    // The first bone-palette-sized region is what unskinned draws point binding 1 at.
    frame.cursor = Align(sizeof(BoneUniforms));
    return frame.drawBuffer.IsValid() && frame.drawGroup.IsValid();
}

void Renderer::BeginFrame(const CameraData &camera, const std::vector<LightData> &lights)
{
    FrameResources &frame = m_frames[m_device->GetFrameSlot()];
    frame.cursor = Align(sizeof(BoneUniforms));

    FrameUniforms uniforms{};
    uniforms.view = camera.viewMatrix;
    uniforms.projection = kDepthZeroToOne * camera.projectionMatrix;
    uniforms.cameraPos = glm::vec4(camera.position, 1.0f);
    // Only the first light is used until clustered lighting (M2).
    if (!lights.empty())
    {
        uniforms.lightColor = glm::vec4(lights[0].color, 0.0f);
        uniforms.lightPosition = glm::vec4(lights[0].position, 1.0f);
    }
    std::memcpy(m_device->MapBuffer(frame.frameBuffer), &uniforms, sizeof(uniforms));
}

rhi::PipelineHandle Renderer::GetPipeline(const Shader &shader, const Mesh &mesh)
{
    const PipelineKey key{shader.GetId(), mesh.GetLayoutKey(), static_cast<uint8_t>(mesh.GetTopology())};
    auto it = m_pipelines.find(key);
    if (it != m_pipelines.end())
    {
        return it->second;
    }

    const VertexLayout &layout = mesh.GetVertexLayout();
    rhi::PipelineDesc desc;
    desc.vertexShader = shader.GetVertexModule();
    desc.fragmentShader = shader.GetFragmentModule();
    desc.topology = mesh.GetTopology();
    desc.vertexLayout.stride = layout.stride;
    const auto &inputs = shader.GetVertexInputLocations();
    for (const auto &element : layout.elements)
    {
        static const rhi::VertexFormat formats[] = {rhi::VertexFormat::Float1, rhi::VertexFormat::Float2,
                                                    rhi::VertexFormat::Float3, rhi::VertexFormat::Float4};
        // Attributes the shader does not read are left out; the stride still skips them.
        if (element.size < 1 || element.size > 4 ||
            std::find(inputs.begin(), inputs.end(), element.index) == inputs.end())
        {
            continue;
        }
        desc.vertexLayout.attributes.push_back({element.index, formats[element.size - 1], element.offset});
    }
    desc.bindGroupLayouts = {m_frameLayout, shader.GetMaterialLayout(), m_drawLayout};
    desc.debugName = shader.GetName().c_str();

    rhi::PipelineHandle pipeline;
    bool inputsProvided = true;
    for (uint32_t location : shader.GetVertexInputLocations())
    {
        const bool found = std::any_of(desc.vertexLayout.attributes.begin(), desc.vertexLayout.attributes.end(),
                                       [&](const rhi::VertexAttribute &a) { return a.location == location; });
        if (!found)
        {
            GENI_LOG_ERROR("Shader '%s' reads vertex input %u, which a mesh drawn with it does not have; those "
                           "meshes are skipped",
                           shader.GetName().c_str(), location);
            inputsProvided = false;
            break;
        }
    }
    if (inputsProvided)
    {
        pipeline = m_device->CreatePipeline(desc);
    }
    // A failure is cached too, so it is reported once rather than every frame.
    m_pipelines[key] = pipeline;
    return pipeline;
}

void Renderer::ReleasePipelines(uint64_t shaderId)
{
    for (auto it = m_pipelines.begin(); it != m_pipelines.end();)
    {
        if (it->first.shader == shaderId)
        {
            if (m_device)
            {
                m_device->DestroyPipeline(it->second);
            }
            it = m_pipelines.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

bool Renderer::DrawMesh(rhi::CommandList &cmd, const Mesh &mesh, Material &material, const glm::mat4 &model,
                        const std::vector<glm::mat4> *bones)
{
    Shader *shader = material.GetShader();
    if (!shader)
    {
        return false;
    }
    const rhi::PipelineHandle pipeline = GetPipeline(*shader, mesh);
    if (!pipeline.IsValid())
    {
        return false;
    }
    const rhi::BindGroupHandle materialGroup = material.PrepareBindGroup(*this);
    if (!materialGroup.IsValid())
    {
        return false;
    }

    FrameResources &frame = m_frames[m_device->GetFrameSlot()];
    const bool skinned = bones && !bones->empty();
    const uint64_t needed = Align(sizeof(DrawUniforms)) + (skinned ? Align(sizeof(BoneUniforms)) : 0);
    if (frame.cursor + needed > frame.drawCapacity)
    {
        uint64_t capacity = frame.drawCapacity * 2;
        while (Align(sizeof(BoneUniforms)) + needed > capacity)
        {
            capacity *= 2;
        }
        if (!CreateDrawBuffer(frame, capacity))
        {
            return false;
        }
    }

    auto *base = static_cast<uint8_t *>(m_device->MapBuffer(frame.drawBuffer));
    uint32_t offsets[2] = {static_cast<uint32_t>(frame.cursor), 0};
    std::memcpy(base + frame.cursor, &model, sizeof(glm::mat4));
    frame.cursor += Align(sizeof(DrawUniforms));
    if (skinned)
    {
        offsets[1] = static_cast<uint32_t>(frame.cursor);
        const size_t count = std::min<size_t>(bones->size(), MaxBones);
        std::memcpy(base + frame.cursor, bones->data(), count * sizeof(glm::mat4));
        frame.cursor += Align(sizeof(BoneUniforms));
    }

    cmd.SetPipeline(pipeline);
    cmd.SetBindGroup(FrameSet, frame.frameGroup);
    cmd.SetBindGroup(MaterialSet, materialGroup);
    cmd.SetBindGroup(DrawSet, frame.drawGroup, offsets, 2);
    mesh.Draw(cmd);
    return true;
}

} // namespace Geni
