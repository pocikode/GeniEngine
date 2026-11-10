#pragma once

#include "core/Common.h"
#include "rhi/Handle.h"
#include <cstdint>
#include <glm/mat4x4.hpp>
#include <map>
#include <memory>
#include <tuple>
#include <vector>

namespace Geni
{

namespace rhi
{
class Device;
class CommandList;
} // namespace rhi

class Mesh;
class Material;
class Shader;

// Turns render commands into RHI draws. Owns what every draw shares: the per-frame and
// per-draw uniform buffers (render/ShaderInterface.h), the default shader and sampler,
// and the pipeline cache (one pipeline per shader, vertex layout and topology).
class Renderer
{
  public:
    bool Init(rhi::Device *device);
    // Releases GPU objects. Must run before the device is destroyed.
    void Shutdown();

    // nullptr before Init and after Shutdown.
    rhi::Device *GetDevice() const
    {
        return m_device;
    }
    // Built-in lit shader (base color texture * factor, one point light). Its material
    // parameters are `uBaseColorFactor` and `baseColorTexture`.
    const std::shared_ptr<Shader> &GetDefaultShader() const
    {
        return m_defaultShader;
    }
    rhi::SamplerHandle GetDefaultSampler() const
    {
        return m_sampler;
    }

    // Writes the per-frame uniforms. Call once per frame, before DrawMesh.
    void BeginFrame(const CameraData &camera, const std::vector<LightData> &lights);
    // Records one draw into the open render pass. Returns false if nothing was drawn
    // (no shader, or a shader whose vertex inputs the mesh does not provide).
    bool DrawMesh(
        rhi::CommandList &cmd, const Mesh &mesh, Material &material, const glm::mat4 &model,
        const std::vector<glm::mat4> *bones
    );

    // Destroys the cached pipelines made from a shader (called when it is destroyed).
    void ReleasePipelines(uint64_t shaderId);
    size_t GetPipelineCount() const
    {
        return m_pipelines.size();
    }

  private:
    struct FrameResources
    {
        rhi::BufferHandle frameBuffer;
        rhi::BindGroupHandle frameGroup;
        rhi::BufferHandle drawBuffer;
        rhi::BindGroupHandle drawGroup;
        uint64_t drawCapacity = 0;
        uint64_t cursor = 0;
    };

    struct PipelineKey
    {
        uint64_t shader;
        uint64_t layout;
        uint8_t topology;
        bool operator<(const PipelineKey &o) const
        {
            return std::tie(shader, layout, topology) < std::tie(o.shader, o.layout, o.topology);
        }
    };

    bool CreateDrawBuffer(FrameResources &frame, uint64_t capacity);
    rhi::PipelineHandle GetPipeline(const Shader &shader, const Mesh &mesh);
    uint64_t Align(uint64_t size) const;

    rhi::Device *m_device = nullptr;
    rhi::BindGroupLayoutHandle m_frameLayout;
    rhi::BindGroupLayoutHandle m_drawLayout;
    rhi::SamplerHandle m_sampler;
    std::shared_ptr<Shader> m_defaultShader;
    std::vector<FrameResources> m_frames;
    uint32_t m_alignment = 256;
    std::map<PipelineKey, rhi::PipelineHandle> m_pipelines;
};

} // namespace Geni
