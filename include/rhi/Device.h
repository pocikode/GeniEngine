#pragma once

#include "rhi/Types.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct GLFWwindow;

namespace Geni::rhi
{

// Records commands for one frame. Obtained from Device::BeginFrame and valid until
// Device::EndFrame.
class CommandList
{
  public:
    virtual ~CommandList() = default;

    // Starts drawing into the backbuffer. The viewport and scissor cover the whole
    // backbuffer until SetViewport changes them.
    virtual void BeginRenderPass(const RenderPassDesc &desc) = 0;
    virtual void EndRenderPass() = 0;
    virtual bool IsInRenderPass() const = 0;

    // Pixel rectangle with its origin at the top-left corner.
    virtual void SetViewport(float x, float y, float width, float height) = 0;
    virtual void SetPipeline(PipelineHandle pipeline) = 0;
    // Binds a group to `set` of the current pipeline. One dynamic offset per dynamic
    // uniform buffer in the group, in binding order.
    virtual void SetBindGroup(
        uint32_t set, BindGroupHandle group, const uint32_t *dynamicOffsets = nullptr, uint32_t dynamicOffsetCount = 0
    ) = 0;
    virtual void SetVertexBuffer(uint32_t binding, BufferHandle buffer, uint64_t offset = 0) = 0;
    virtual void SetIndexBuffer(BufferHandle buffer, IndexFormat format, uint64_t offset = 0) = 0;
    virtual void Draw(uint32_t vertexCount, uint32_t firstVertex = 0) = 0;
    virtual void DrawIndexed(uint32_t indexCount, uint32_t firstIndex = 0, int32_t vertexOffset = 0) = 0;
    // One draw for `instanceCount` copies of the mesh; per-instance attributes come from
    // the buffer bound at binding 1.
    virtual void DrawIndexedInstanced(
        uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex = 0, int32_t vertexOffset = 0,
        uint32_t firstInstance = 0
    ) = 0;
    virtual void DrawInstanced(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstInstance = 0) = 0;

    // Compute (docs/design/0029), outside render passes. SetPipeline with a compute
    // pipeline binds it (and SetBindGroup then binds for compute).
    virtual void Dispatch(uint32_t groupsX, uint32_t groupsY = 1, uint32_t groupsZ = 1) = 0;
    // Makes compute writes visible to the draws that follow (vertex attributes, indirect
    // arguments, shader reads).
    virtual void ComputeToDrawBarrier() = 0;
    // One draw whose DrawIndexedIndirectArgs are at `offset` in `buffer`. Keep
    // firstInstance 0 (a non-zero one needs an optional Vulkan feature); bind the instance
    // buffer at an offset instead.
    virtual void DrawIndexedIndirect(BufferHandle buffer, uint64_t offset) = 0;
};

struct DeviceConfig
{
    const char *applicationName = "GeniEngine";
    bool vsync = true;
    // Turns on the backend's validation (Vulkan validation layers) when available.
    // Every validation error is logged and counted (GetValidationErrorCount).
    bool validation = false;
};

// The backbuffer contents of a frame, as 8-bit RGB with the top row first.
struct BackbufferCapture
{
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgb;
};

// Creates every other GPU object and runs the frame loop:
//
//   CommandList *cmd = device.BeginFrame();   // nullptr: nothing to draw into (minimized)
//   cmd->BeginRenderPass(...); ...; cmd->EndRenderPass();
//   device.EndFrame();                        // submit and present
//
// Destroy* calls are deferred until the GPU has finished every frame that could use the
// object, so an object may be destroyed right after it was used in a command.
class Device
{
  public:
    virtual ~Device() = default;

    virtual BufferHandle CreateBuffer(const BufferDesc &desc, const void *initialData = nullptr) = 0;
    virtual void DestroyBuffer(BufferHandle buffer) = 0;
    // Writes into a buffer of any memory usage. Waits for the GPU to finish all submitted
    // work first, so it is safe but slow: for data that changes rarely.
    virtual void UpdateBuffer(BufferHandle buffer, uint64_t offset, const void *data, uint64_t size) = 0;
    // Persistent pointer into a CpuToGpu buffer; nullptr for GpuOnly buffers.
    virtual void *MapBuffer(BufferHandle buffer) = 0;

    // mipData holds desc.mipLevels pointers to tightly packed pixels, level 0 first.
    virtual TextureHandle CreateTexture(const TextureDesc &desc, const void *const *mipData) = 0;
    virtual void DestroyTexture(TextureHandle texture) = 0;
    // Whether a sampled texture in this format can be created and linearly filtered.
    // AssetManager uses it to choose between cooked BC and ASTC variants.
    virtual bool IsTextureFormatSupported(Format format) const = 0;

    virtual SamplerHandle CreateSampler(const SamplerDesc &desc) = 0;
    virtual void DestroySampler(SamplerHandle sampler) = 0;

    virtual ShaderModuleHandle CreateShaderModule(const ShaderModuleDesc &desc) = 0;
    virtual void DestroyShaderModule(ShaderModuleHandle module) = 0;

    virtual BindGroupLayoutHandle CreateBindGroupLayout(const BindGroupLayoutDesc &desc) = 0;
    virtual void DestroyBindGroupLayout(BindGroupLayoutHandle layout) = 0;

    virtual BindGroupHandle CreateBindGroup(const BindGroupDesc &desc) = 0;
    virtual void DestroyBindGroup(BindGroupHandle group) = 0;

    // Pipelines draw into the backbuffer (color + depth).
    virtual PipelineHandle CreatePipeline(const PipelineDesc &desc) = 0;
    // Destroyed with DestroyPipeline.
    virtual PipelineHandle CreateComputePipeline(const ComputePipelineDesc &desc) = 0;
    virtual void DestroyPipeline(PipelineHandle pipeline) = 0;

    // Waits until the previous use of this frame slot has finished on the GPU and the
    // next backbuffer image is available. Returns nullptr when there is nothing to draw
    // into (minimized window); call EndFrame either way.
    virtual CommandList *BeginFrame() = 0;
    // Submits and presents. With `capture`, also copies the backbuffer to it (waits for
    // the GPU to finish the frame).
    virtual void EndFrame(BackbufferCapture *capture = nullptr) = 0;
    virtual void WaitIdle() = 0;

    // Frames the CPU may record ahead of the GPU, and which of them is being recorded
    // now (0 .. GetFramesInFlight()-1). Per-frame CpuToGpu buffers are indexed by it.
    virtual uint32_t GetFramesInFlight() const = 0;
    virtual uint32_t GetFrameSlot() const = 0;
    virtual void GetBackbufferSize(uint32_t &width, uint32_t &height) const = 0;
    // Required alignment of dynamic offsets and of uniform buffer binding offsets.
    virtual uint32_t GetUniformBufferAlignment() const = 0;
    virtual std::string GetDescription() const = 0;
    // Bytes of GPU memory currently allocated for buffers and images (all heaps).
    virtual uint64_t GetAllocatedBytes() const = 0;
    // The depth format the device's swapchain depth buffer uses; renderer-created depth
    // targets should use the same one.
    virtual Format GetDepthFormat() const = 0;
    // A depth-only format that can be both a depth attachment and sampled (shadow maps).
    // Sample it with Nearest filtering: linear filtering of depth formats is optional.
    virtual Format GetSampledDepthFormat() const = 0;

    // Dear ImGui renderer integration, so code outside the backend never sees its API.
    // The engine owns the ImGui context and the platform (GLFW) backend. No-ops when the
    // engine is built without GENI_ENABLE_IMGUI.
    virtual bool InitImGui() = 0;
    virtual void ShutdownImGui() = 0;
    virtual void NewImGuiFrame() = 0;
    // Draws ImGui's current draw data inside the open render pass.
    virtual void RenderImGui(CommandList &cmd) = 0;
};

// Must run before glfwInit: points GLFW at the Vulkan loader and, in builds that know
// where the validation layers live, makes them findable without environment setup.
void PrepareWindowSystem();

// The window must have been created with GLFW_CLIENT_API = GLFW_NO_API.
// Returns nullptr and logs the reason on failure.
std::unique_ptr<Device> CreateDevice(GLFWwindow *window, const DeviceConfig &config);

// Validation errors reported by every device in this process so far.
uint64_t GetValidationErrorCount();

} // namespace Geni::rhi
