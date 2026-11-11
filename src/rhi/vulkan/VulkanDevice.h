#pragma once

// Vulkan implementation of the rendering interface. This folder is the only place in the
// engine that includes Vulkan, VMA or the ImGui Vulkan backend.

#include "rhi/Device.h"
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

#include <array>
#include <functional>
#include <map>
#include <tuple>
#include <vector>

namespace Geni::rhi::vulkan
{

struct BufferRecord
{
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = nullptr;
    void *mapped = nullptr;
    uint64_t size = 0;
};

struct TextureRecord
{
    VkImage image = VK_NULL_HANDLE;
    VmaAllocation allocation = nullptr;
    VkImageView view = VK_NULL_HANDLE;
    VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    Format format = Format::RGBA8Unorm;
    TextureUsage usage = TextureUsage::Sampled;
    uint32_t sampleCount = 1;
    uint32_t width = 0;
    uint32_t height = 0;
};

struct SamplerRecord
{
    VkSampler sampler = VK_NULL_HANDLE;
};

struct ShaderModuleRecord
{
    VkShaderModule module = VK_NULL_HANDLE;
    ShaderStage stage = ShaderStage::Vertex;
};

struct BindGroupLayoutRecord
{
    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    std::vector<BindGroupLayoutEntry> entries;
};

struct BindGroupRecord
{
    VkDescriptorSet set = VK_NULL_HANDLE;
    VkDescriptorPool pool = VK_NULL_HANDLE;
};

struct PipelineRecord
{
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
};

class VulkanDevice;

class VulkanCommandList final : public CommandList
{
  public:
    void BeginRenderPass(const RenderPassDesc &desc) override;
    void EndRenderPass() override;
    bool IsInRenderPass() const override
    {
        return m_inRenderPass;
    }
    void SetViewport(float x, float y, float width, float height) override;
    void SetPipeline(PipelineHandle pipeline) override;
    void SetBindGroup(
        uint32_t set, BindGroupHandle group, const uint32_t *dynamicOffsets, uint32_t dynamicOffsetCount
    ) override;
    void SetVertexBuffer(uint32_t binding, BufferHandle buffer, uint64_t offset) override;
    void SetIndexBuffer(BufferHandle buffer, IndexFormat format, uint64_t offset) override;
    void Draw(uint32_t vertexCount, uint32_t firstVertex) override;
    void DrawIndexed(uint32_t indexCount, uint32_t firstIndex, int32_t vertexOffset) override;
    void DrawIndexedInstanced(
        uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance
    ) override;
    void DrawInstanced(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstInstance) override;

  private:
    friend class VulkanDevice;
    VulkanDevice *m_device = nullptr;
    VkCommandBuffer m_cmd = VK_NULL_HANDLE;
    VkPipelineLayout m_layout = VK_NULL_HANDLE;
    bool m_inRenderPass = false;
    // True once any render pass has run this frame (the backbuffer image is then in
    // PRESENT_SRC layout).
    bool m_backbufferWritten = false;
};

class VulkanDevice final : public Device
{
  public:
    VulkanDevice() = default;
    ~VulkanDevice() override;

    bool Init(GLFWwindow *window, const DeviceConfig &config);

    BufferHandle CreateBuffer(const BufferDesc &desc, const void *initialData) override;
    void DestroyBuffer(BufferHandle buffer) override;
    void UpdateBuffer(BufferHandle buffer, uint64_t offset, const void *data, uint64_t size) override;
    void *MapBuffer(BufferHandle buffer) override;

    TextureHandle CreateTexture(const TextureDesc &desc, const void *const *mipData) override;
    void DestroyTexture(TextureHandle texture) override;
    bool IsTextureFormatSupported(Format format) const override;

    SamplerHandle CreateSampler(const SamplerDesc &desc) override;
    void DestroySampler(SamplerHandle sampler) override;

    ShaderModuleHandle CreateShaderModule(const ShaderModuleDesc &desc) override;
    void DestroyShaderModule(ShaderModuleHandle module) override;

    BindGroupLayoutHandle CreateBindGroupLayout(const BindGroupLayoutDesc &desc) override;
    void DestroyBindGroupLayout(BindGroupLayoutHandle layout) override;

    BindGroupHandle CreateBindGroup(const BindGroupDesc &desc) override;
    void DestroyBindGroup(BindGroupHandle group) override;

    PipelineHandle CreatePipeline(const PipelineDesc &desc) override;
    void DestroyPipeline(PipelineHandle pipeline) override;

    CommandList *BeginFrame() override;
    void EndFrame(BackbufferCapture *capture) override;
    void WaitIdle() override;

    uint32_t GetFramesInFlight() const override
    {
        return FramesInFlight;
    }
    uint32_t GetFrameSlot() const override
    {
        return m_frameSlot;
    }
    void GetBackbufferSize(uint32_t &width, uint32_t &height) const override;
    uint32_t GetUniformBufferAlignment() const override;
    std::string GetDescription() const override;
    uint64_t GetAllocatedBytes() const override;
    Format GetDepthFormat() const override
    {
        return FromVkDepth(m_depthFormat);
    }
    Format GetSampledDepthFormat() const override
    {
        return FromVkDepth(m_sampledDepthFormat);
    }

    bool InitImGui() override;
    void ShutdownImGui() override;
    void NewImGuiFrame() override;
    void RenderImGui(CommandList &cmd) override;

  private:
    friend class VulkanCommandList;

    static constexpr uint32_t FramesInFlight = 2;

    struct RenderPassKey
    {
        VkFormat colorFormat = VK_FORMAT_UNDEFINED;
        VkFormat depthFormat = VK_FORMAT_UNDEFINED;
        VkFormat resolveFormat = VK_FORMAT_UNDEFINED;
        uint32_t colorSamples = 1;
        uint32_t depthSamples = 1;
        bool hasColor = false;
        bool hasDepth = false;
        bool hasResolve = false;
        LoadOp colorLoad = LoadOp::Clear;
        StoreOp colorStore = StoreOp::Store;
        LoadOp depthLoad = LoadOp::Clear;
        StoreOp depthStore = StoreOp::DontCare;
        VkImageLayout colorFinal = VK_IMAGE_LAYOUT_UNDEFINED;
        VkImageLayout depthFinal = VK_IMAGE_LAYOUT_UNDEFINED;
        bool firstInFrame = true;
        bool backbuffer = false;

        bool operator<(const RenderPassKey &o) const
        {
            return std::tie(
                       colorFormat, depthFormat, resolveFormat, colorSamples, depthSamples, hasColor, hasDepth,
                       hasResolve, colorLoad, colorStore, depthLoad, depthStore, colorFinal, depthFinal, firstInFrame,
                       backbuffer
                   ) <
                   std::tie(
                       o.colorFormat, o.depthFormat, o.resolveFormat, o.colorSamples, o.depthSamples, o.hasColor,
                       o.hasDepth, o.hasResolve, o.colorLoad, o.colorStore, o.depthLoad, o.depthStore, o.colorFinal,
                       o.depthFinal, o.firstInFrame, o.backbuffer
                   );
        }
    };

    struct FramebufferKey
    {
        VkRenderPass pass = VK_NULL_HANDLE;
        VkImageView color = VK_NULL_HANDLE;
        VkImageView depth = VK_NULL_HANDLE;
        VkImageView resolve = VK_NULL_HANDLE;

        bool operator<(const FramebufferKey &o) const
        {
            return std::tie(pass, color, depth, resolve) < std::tie(o.pass, o.color, o.depth, o.resolve);
        }
    };

    struct Frame
    {
        VkCommandPool commandPool = VK_NULL_HANDLE;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkSemaphore imageAvailable = VK_NULL_HANDLE;
        VkFence inFlight = VK_NULL_HANDLE;
    };

    struct PendingDestroy
    {
        uint64_t frameNumber;
        std::function<void()> destroy;
    };

    bool CreateInstance(const DeviceConfig &config);
    bool PickPhysicalDevice();
    bool CreateLogicalDevice();
    bool CreateAllocator();
    bool CreateFrames();
    bool CreateSwapchain();
    void DestroySwapchain();
    bool RecreateSwapchain();
    VkRenderPass GetRenderPass(const RenderPassKey &key);
    VkRenderPass GetCompatiblePassForPipeline(const PipelineTargets &targets);
    VkRenderPass GetBackbufferRenderPass(bool withDepth);
    VkFramebuffer GetFramebuffer(
        VkRenderPass pass, VkImageView color, VkImageView depth, VkImageView resolve, VkExtent2D extent
    );
    void DestroyFramebuffersUsing(VkImageView view);
    RenderPassKey MakeBackbufferKey(bool withDepth, LoadOp colorLoad, StoreOp colorStore) const;

    void Defer(std::function<void()> destroy);
    void RunDeferred(bool all);
    void ImmediateSubmit(const std::function<void(VkCommandBuffer)> &record);
    void SetDebugName(VkObjectType type, uint64_t object, const char *name);
    void DestroyAllLiveObjects();

  private:
    GLFWwindow *m_window = nullptr;
    bool m_vsync = true;

    VkInstance m_instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT m_messenger = VK_NULL_HANDLE;
    PFN_vkSetDebugUtilsObjectNameEXT m_setObjectName = nullptr;
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties m_properties{};
    bool m_hasPortabilitySubset = false;
    uint32_t m_queueFamily = 0;
    VkDevice m_device = VK_NULL_HANDLE;
    VkQueue m_queue = VK_NULL_HANDLE;
    VmaAllocator m_allocator = nullptr;

    VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
    VkFormat m_swapchainFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D m_extent{};
    int m_windowSize[2] = {0, 0}; // GLFW framebuffer size the swapchain was made for
    bool m_swapchainCanCapture = false;
    std::vector<VkImage> m_swapchainImages;
    std::vector<VkImageView> m_swapchainViews;
    std::vector<VkSemaphore> m_renderFinished; // one per swapchain image
    uint32_t m_minImageCount = 2;
    VkFormat m_depthFormat = VK_FORMAT_UNDEFINED;
    // Depth-only format that can also be sampled (shadow maps).
    VkFormat m_sampledDepthFormat = VK_FORMAT_UNDEFINED;
    static Format FromVkDepth(VkFormat format)
    {
        switch (format)
        {
        case VK_FORMAT_D24_UNORM_S8_UINT:
            return Format::Depth24UnormStencil8;
        case VK_FORMAT_D16_UNORM:
            return Format::Depth16Unorm;
        default:
            return Format::Depth32Float;
        }
    }
    VkImage m_depthImage = VK_NULL_HANDLE;
    VmaAllocation m_depthAllocation = nullptr;
    VkImageView m_depthView = VK_NULL_HANDLE;
    std::map<RenderPassKey, VkRenderPass> m_renderPasses;
    std::map<FramebufferKey, VkFramebuffer> m_framebuffers;

    std::array<Frame, FramesInFlight> m_frames{};
    uint32_t m_frameSlot = 0;
    uint64_t m_frameNumber = 0; // frames begun so far; the one being recorded is m_frameNumber
    uint32_t m_imageIndex = 0;
    bool m_frameActive = false;
    bool m_frameHasImage = false;
    VulkanCommandList m_commandList;

    VkCommandPool m_uploadPool = VK_NULL_HANDLE;
    VkFence m_uploadFence = VK_NULL_HANDLE;

    // Initial data for GPU-only buffers created between frames: copied at the start of the
    // next frame's command buffer instead of a submit-and-wait per buffer, so streaming
    // meshes in costs no CPU stall (docs/design/0007).
    struct PendingUpload
    {
        VkBuffer staging = VK_NULL_HANDLE;
        VmaAllocation stagingAllocation = VK_NULL_HANDLE;
        VkBuffer destination = VK_NULL_HANDLE;
        VkDeviceSize size = 0;
    };
    std::vector<PendingUpload> m_pendingUploads;
    // Records the pending copies into `cmd`, followed by a barrier for every later read.
    void RecordPendingUploads(VkCommandBuffer cmd);
    // Submits the pending copies now and waits (before an operation that needs them done).
    void FlushPendingUploads();

    std::vector<VkDescriptorPool> m_descriptorPools;
    std::vector<PendingDestroy> m_pendingDestroys;

    HandlePool<BufferRecord, BufferTag> m_buffers;
    HandlePool<TextureRecord, TextureTag> m_textures;
    HandlePool<SamplerRecord, SamplerTag> m_samplers;
    HandlePool<ShaderModuleRecord, ShaderModuleTag> m_shaderModules;
    HandlePool<BindGroupLayoutRecord, BindGroupLayoutTag> m_bindGroupLayouts;
    HandlePool<BindGroupRecord, BindGroupTag> m_bindGroups;
    HandlePool<PipelineRecord, PipelineTag> m_pipelines;

    bool m_imguiInitialized = false;
};

} // namespace Geni::rhi::vulkan
