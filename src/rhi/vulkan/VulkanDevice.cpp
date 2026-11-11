#include "VulkanDevice.h"

#include "config.h"
#include "core/Assert.h"
#include "core/Log.h"
#include "core/Profiler.h"

// After vulkan.h, so GLFW declares its Vulkan functions.
#include <GLFW/glfw3.h>

#if defined(GENI_ENABLE_IMGUI)
#include <backends/imgui_impl_vulkan.h>
#include <imgui.h>
#endif

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>

namespace Geni::rhi
{

namespace
{
std::atomic<uint64_t> g_validationErrors{0};

const char *ResultString(VkResult result)
{
    switch (result)
    {
    case VK_SUCCESS:
        return "VK_SUCCESS";
    case VK_NOT_READY:
        return "VK_NOT_READY";
    case VK_TIMEOUT:
        return "VK_TIMEOUT";
    case VK_SUBOPTIMAL_KHR:
        return "VK_SUBOPTIMAL_KHR";
    case VK_ERROR_OUT_OF_HOST_MEMORY:
        return "VK_ERROR_OUT_OF_HOST_MEMORY";
    case VK_ERROR_OUT_OF_DEVICE_MEMORY:
        return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
    case VK_ERROR_INITIALIZATION_FAILED:
        return "VK_ERROR_INITIALIZATION_FAILED";
    case VK_ERROR_DEVICE_LOST:
        return "VK_ERROR_DEVICE_LOST";
    case VK_ERROR_LAYER_NOT_PRESENT:
        return "VK_ERROR_LAYER_NOT_PRESENT";
    case VK_ERROR_EXTENSION_NOT_PRESENT:
        return "VK_ERROR_EXTENSION_NOT_PRESENT";
    case VK_ERROR_FEATURE_NOT_PRESENT:
        return "VK_ERROR_FEATURE_NOT_PRESENT";
    case VK_ERROR_INCOMPATIBLE_DRIVER:
        return "VK_ERROR_INCOMPATIBLE_DRIVER";
    case VK_ERROR_OUT_OF_POOL_MEMORY:
        return "VK_ERROR_OUT_OF_POOL_MEMORY";
    case VK_ERROR_FRAGMENTED_POOL:
        return "VK_ERROR_FRAGMENTED_POOL";
    case VK_ERROR_SURFACE_LOST_KHR:
        return "VK_ERROR_SURFACE_LOST_KHR";
    case VK_ERROR_OUT_OF_DATE_KHR:
        return "VK_ERROR_OUT_OF_DATE_KHR";
    case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR:
        return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
    default:
        return "VkResult (other)";
    }
}

#define GENI_VK_CHECK(expr)                                                                                            \
    do                                                                                                                 \
    {                                                                                                                  \
        const VkResult geniVkResult = (expr);                                                                          \
        if (geniVkResult != VK_SUCCESS)                                                                                \
        {                                                                                                              \
            GENI_LOG_ERROR("%s failed: %s (%d)", #expr, ResultString(geniVkResult), static_cast<int>(geniVkResult));   \
            return false;                                                                                              \
        }                                                                                                              \
    } while (0)

VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT,
    const VkDebugUtilsMessengerCallbackDataEXT *data, void *
)
{
    const char *message = data && data->pMessage ? data->pMessage : "";
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
    {
        ++g_validationErrors;
        GENI_LOG_ERROR("Vulkan validation: %s", message);
    }
    else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
    {
        GENI_LOG_WARN("Vulkan validation: %s", message);
    }
    return VK_FALSE;
}

VkFormat ToVk(Format format)
{
    switch (format)
    {
    case Format::RGBA8Unorm:
        return VK_FORMAT_R8G8B8A8_UNORM;
    case Format::BGRA8Unorm:
        return VK_FORMAT_B8G8R8A8_UNORM;
    case Format::RGBA16Float:
        return VK_FORMAT_R16G16B16A16_SFLOAT;
    case Format::BC1Unorm:
        return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
    case Format::BC3Unorm:
        return VK_FORMAT_BC3_UNORM_BLOCK;
    case Format::BC4Unorm:
        return VK_FORMAT_BC4_UNORM_BLOCK;
    case Format::BC5Unorm:
        return VK_FORMAT_BC5_UNORM_BLOCK;
    case Format::BC7Unorm:
        return VK_FORMAT_BC7_UNORM_BLOCK;
    case Format::ASTC4x4Unorm:
        return VK_FORMAT_ASTC_4x4_UNORM_BLOCK;
    case Format::RGBA8Srgb:
        return VK_FORMAT_R8G8B8A8_SRGB;
    case Format::BC7Srgb:
        return VK_FORMAT_BC7_SRGB_BLOCK;
    case Format::ASTC4x4Srgb:
        return VK_FORMAT_ASTC_4x4_SRGB_BLOCK;
    case Format::Depth32Float:
        return VK_FORMAT_D32_SFLOAT;
    case Format::Depth24UnormStencil8:
        return VK_FORMAT_D24_UNORM_S8_UINT;
    case Format::Depth16Unorm:
        return VK_FORMAT_D16_UNORM;
    case Format::Undefined:
        break;
    }
    return VK_FORMAT_UNDEFINED;
}

bool IsDepthFormat(Format format)
{
    return format == Format::Depth32Float || format == Format::Depth24UnormStencil8 || format == Format::Depth16Unorm;
}

VkImageAspectFlags ToAspect(Format format)
{
    if (format == Format::Depth24UnormStencil8)
    {
        return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    }
    return IsDepthFormat(format) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
}

VkFormat ToVk(VertexFormat format)
{
    switch (format)
    {
    case VertexFormat::Float1:
        return VK_FORMAT_R32_SFLOAT;
    case VertexFormat::Float2:
        return VK_FORMAT_R32G32_SFLOAT;
    case VertexFormat::Float3:
        return VK_FORMAT_R32G32B32_SFLOAT;
    case VertexFormat::Float4:
        return VK_FORMAT_R32G32B32A32_SFLOAT;
    }
    return VK_FORMAT_UNDEFINED;
}

VkFilter ToVk(Filter filter)
{
    return filter == Filter::Nearest ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
}

VkSamplerAddressMode ToVk(AddressMode mode)
{
    switch (mode)
    {
    case AddressMode::Repeat:
        return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    case AddressMode::MirroredRepeat:
        return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    case AddressMode::ClampToEdge:
        return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    }
    return VK_SAMPLER_ADDRESS_MODE_REPEAT;
}

VkShaderStageFlags ToVk(ShaderStage stages)
{
    VkShaderStageFlags flags = 0;
    if (HasFlag(stages, ShaderStage::Vertex))
    {
        flags |= VK_SHADER_STAGE_VERTEX_BIT;
    }
    if (HasFlag(stages, ShaderStage::Fragment))
    {
        flags |= VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    return flags;
}

VkDescriptorType ToVk(BindingType type)
{
    switch (type)
    {
    case BindingType::UniformBuffer:
        return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    case BindingType::DynamicUniformBuffer:
        return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    case BindingType::StorageBuffer:
        return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    case BindingType::SampledTexture:
        return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    }
    return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
}

VkCompareOp ToVk(CompareOp op)
{
    switch (op)
    {
    case CompareOp::Never:
        return VK_COMPARE_OP_NEVER;
    case CompareOp::Less:
        return VK_COMPARE_OP_LESS;
    case CompareOp::LessOrEqual:
        return VK_COMPARE_OP_LESS_OR_EQUAL;
    case CompareOp::Always:
        return VK_COMPARE_OP_ALWAYS;
    }
    return VK_COMPARE_OP_LESS;
}

VkAttachmentLoadOp ToVk(LoadOp op)
{
    switch (op)
    {
    case LoadOp::Load:
        return VK_ATTACHMENT_LOAD_OP_LOAD;
    case LoadOp::Clear:
        return VK_ATTACHMENT_LOAD_OP_CLEAR;
    case LoadOp::DontCare:
        return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    }
    return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
}

VkAttachmentStoreOp ToVk(StoreOp op)
{
    return op == StoreOp::Store ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE;
}

bool HasExtension(const std::vector<VkExtensionProperties> &available, const char *name)
{
    for (const auto &ext : available)
    {
        if (std::strcmp(ext.extensionName, name) == 0)
        {
            return true;
        }
    }
    return false;
}

} // namespace

uint64_t GetValidationErrorCount()
{
    return g_validationErrors.load();
}

void PrepareWindowSystem()
{
#if defined(GENI_VULKAN_LAYER_DIR)
    // Homebrew installs the validation layer where the loader does not look by default.
    // Point it there unless the environment already says where layers are.
    const char *layerDir = GENI_VULKAN_LAYER_DIR;
    if (layerDir[0] != '\0' && !std::getenv("VK_LAYER_PATH") && !std::getenv("VK_ADD_LAYER_PATH"))
    {
#if defined(_WIN32)
        _putenv_s("VK_ADD_LAYER_PATH", layerDir);
#else
        setenv("VK_ADD_LAYER_PATH", layerDir, 0);
#endif
    }
#endif

#if GLFW_VERSION_MAJOR > 3 || (GLFW_VERSION_MAJOR == 3 && GLFW_VERSION_MINOR >= 4)
    // Use the loader this program is linked against rather than letting GLFW search
    // the default library paths, which do not include Homebrew's.
    glfwInitVulkanLoader(vkGetInstanceProcAddr);
#endif
}

std::unique_ptr<Device> CreateDevice(GLFWwindow *window, const DeviceConfig &config)
{
    auto device = std::make_unique<vulkan::VulkanDevice>();
    if (!device->Init(window, config))
    {
        return nullptr;
    }
    return device;
}

namespace vulkan
{

// ---------------------------------------------------------------------------------------
// Initialization
// ---------------------------------------------------------------------------------------

bool VulkanDevice::Init(GLFWwindow *window, const DeviceConfig &config)
{
    m_window = window;
    m_vsync = config.vsync;

    if (!CreateInstance(config))
    {
        return false;
    }
    GENI_VK_CHECK(glfwCreateWindowSurface(m_instance, window, nullptr, &m_surface));
    if (!PickPhysicalDevice() || !CreateLogicalDevice() || !CreateAllocator() || !CreateFrames())
    {
        return false;
    }

    for (VkFormat candidate : {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D16_UNORM})
    {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(m_physicalDevice, candidate, &props);
        if (props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
        {
            m_depthFormat = candidate;
            break;
        }
    }
    if (m_depthFormat == VK_FORMAT_UNDEFINED)
    {
        GENI_LOG_ERROR("Vulkan: no supported depth format");
        return false;
    }
    // Shadow maps are rendered and then sampled, so they need a depth-only format with both
    // features. D16 has them on every Vulkan device; D32 is preferred for precision.
    for (VkFormat candidate : {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D16_UNORM})
    {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(m_physicalDevice, candidate, &props);
        const VkFormatFeatureFlags needed =
            VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
        if ((props.optimalTilingFeatures & needed) == needed)
        {
            m_sampledDepthFormat = candidate;
            break;
        }
    }
    if (m_sampledDepthFormat == VK_FORMAT_UNDEFINED)
    {
        GENI_LOG_ERROR("Vulkan: no depth format can be both rendered to and sampled");
        return false;
    }

    if (!CreateSwapchain())
    {
        return false;
    }

    GENI_LOG_INFO("%s", GetDescription().c_str());
    return true;
}

bool VulkanDevice::CreateInstance(const DeviceConfig &config)
{
    uint32_t apiVersion = VK_API_VERSION_1_0;
    vkEnumerateInstanceVersion(&apiVersion);
    if (apiVersion < VK_API_VERSION_1_1)
    {
        GENI_LOG_ERROR(
            "Vulkan 1.1 is required; the loader supports %u.%u", VK_API_VERSION_MAJOR(apiVersion),
            VK_API_VERSION_MINOR(apiVersion)
        );
        return false;
    }

    uint32_t glfwCount = 0;
    const char **glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwCount);
    if (!glfwExtensions)
    {
        GENI_LOG_ERROR("GLFW cannot create Vulkan surfaces on this system (no Vulkan loader or ICD found)");
        return false;
    }

    uint32_t count = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> available(count);
    vkEnumerateInstanceExtensionProperties(nullptr, &count, available.data());

    std::vector<const char *> extensions(glfwExtensions, glfwExtensions + glfwCount);
    VkInstanceCreateFlags flags = 0;
    // MoltenVK is a "portability" implementation and is only listed when asked for.
    if (HasExtension(available, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME))
    {
        extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
        flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    }

    std::vector<const char *> layers;
    bool debugUtils = HasExtension(available, VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    if (config.validation)
    {
        vkEnumerateInstanceLayerProperties(&count, nullptr);
        std::vector<VkLayerProperties> availableLayers(count);
        vkEnumerateInstanceLayerProperties(&count, availableLayers.data());
        const bool found = std::any_of(availableLayers.begin(), availableLayers.end(), [](const VkLayerProperties &l) {
            return std::strcmp(l.layerName, "VK_LAYER_KHRONOS_validation") == 0;
        });
        if (found && debugUtils)
        {
            layers.push_back("VK_LAYER_KHRONOS_validation");
        }
        else
        {
            GENI_LOG_WARN(
                "Vulkan validation was requested but VK_LAYER_KHRONOS_validation was not found; "
                "install the validation layers or point VK_ADD_LAYER_PATH at them"
            );
        }
    }
    if (debugUtils)
    {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    VkApplicationInfo appInfo{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    appInfo.pApplicationName = config.applicationName;
    appInfo.pEngineName = "GeniEngine";
    appInfo.apiVersion = VK_API_VERSION_1_1;

    VkDebugUtilsMessengerCreateInfoEXT messengerInfo{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
    messengerInfo.messageSeverity =
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    messengerInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    messengerInfo.pfnUserCallback = DebugCallback;

    VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    info.flags = flags;
    info.pApplicationInfo = &appInfo;
    info.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    info.ppEnabledExtensionNames = extensions.data();
    info.enabledLayerCount = static_cast<uint32_t>(layers.size());
    info.ppEnabledLayerNames = layers.data();
    // Also reports problems during vkCreateInstance / vkDestroyInstance.
    info.pNext = layers.empty() ? nullptr : &messengerInfo;

    GENI_VK_CHECK(vkCreateInstance(&info, nullptr, &m_instance));

    if (!layers.empty())
    {
        auto createMessenger = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(m_instance, "vkCreateDebugUtilsMessengerEXT")
        );
        if (createMessenger)
        {
            createMessenger(m_instance, &messengerInfo, nullptr, &m_messenger);
        }
        GENI_LOG_INFO("Vulkan validation layers enabled");
    }
    if (debugUtils)
    {
        m_setObjectName = reinterpret_cast<PFN_vkSetDebugUtilsObjectNameEXT>(
            vkGetInstanceProcAddr(m_instance, "vkSetDebugUtilsObjectNameEXT")
        );
    }
    return true;
}

bool VulkanDevice::PickPhysicalDevice()
{
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(m_instance, &count, nullptr);
    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(m_instance, &count, devices.data());

    int bestScore = -1;
    for (VkPhysicalDevice candidate : devices)
    {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(candidate, &props);
        if (props.apiVersion < VK_API_VERSION_1_1)
        {
            continue;
        }

        uint32_t extCount = 0;
        vkEnumerateDeviceExtensionProperties(candidate, nullptr, &extCount, nullptr);
        std::vector<VkExtensionProperties> extensions(extCount);
        vkEnumerateDeviceExtensionProperties(candidate, nullptr, &extCount, extensions.data());
        if (!HasExtension(extensions, VK_KHR_SWAPCHAIN_EXTENSION_NAME))
        {
            continue;
        }

        uint32_t familyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, nullptr);
        std::vector<VkQueueFamilyProperties> families(familyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, families.data());

        // One queue for graphics, transfers and presentation keeps synchronization simple.
        int family = -1;
        for (uint32_t i = 0; i < familyCount; ++i)
        {
            VkBool32 present = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(candidate, i, m_surface, &present);
            if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present)
            {
                family = static_cast<int>(i);
                break;
            }
        }
        if (family < 0)
        {
            continue;
        }

        int score = 0;
        if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
        {
            score = 3;
        }
        else if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU)
        {
            score = 2;
        }
        else if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU)
        {
            score = 1;
        }

        if (score > bestScore)
        {
            bestScore = score;
            m_physicalDevice = candidate;
            m_properties = props;
            m_queueFamily = static_cast<uint32_t>(family);
            m_hasPortabilitySubset = HasExtension(extensions, "VK_KHR_portability_subset");
        }
    }

    if (!m_physicalDevice)
    {
        GENI_LOG_ERROR("No Vulkan 1.1 device can present to this window (%u devices found)", count);
        return false;
    }
    return true;
}

bool VulkanDevice::CreateLogicalDevice()
{
    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queueInfo.queueFamilyIndex = m_queueFamily;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &priority;

    std::vector<const char *> extensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    if (m_hasPortabilitySubset)
    {
        // Required by the spec whenever the implementation exposes it (MoltenVK).
        extensions.push_back("VK_KHR_portability_subset");
    }

    VkPhysicalDeviceFeatures features{};
    VkDeviceCreateInfo info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    info.queueCreateInfoCount = 1;
    info.pQueueCreateInfos = &queueInfo;
    info.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    info.ppEnabledExtensionNames = extensions.data();
    info.pEnabledFeatures = &features;

    GENI_VK_CHECK(vkCreateDevice(m_physicalDevice, &info, nullptr, &m_device));
    vkGetDeviceQueue(m_device, m_queueFamily, 0, &m_queue);
    return true;
}

bool VulkanDevice::CreateAllocator()
{
    VmaAllocatorCreateInfo info{};
    info.physicalDevice = m_physicalDevice;
    info.device = m_device;
    info.instance = m_instance;
    info.vulkanApiVersion = VK_API_VERSION_1_1;
    GENI_VK_CHECK(vmaCreateAllocator(&info, &m_allocator));
    return true;
}

bool VulkanDevice::CreateFrames()
{
    for (auto &frame : m_frames)
    {
        VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        poolInfo.queueFamilyIndex = m_queueFamily;
        GENI_VK_CHECK(vkCreateCommandPool(m_device, &poolInfo, nullptr, &frame.commandPool));

        VkCommandBufferAllocateInfo allocInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        allocInfo.commandPool = frame.commandPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;
        GENI_VK_CHECK(vkAllocateCommandBuffers(m_device, &allocInfo, &frame.commandBuffer));

        VkSemaphoreCreateInfo semaphoreInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        GENI_VK_CHECK(vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &frame.imageAvailable));

        VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        GENI_VK_CHECK(vkCreateFence(m_device, &fenceInfo, nullptr, &frame.inFlight));
    }

    VkCommandPoolCreateInfo uploadPoolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    uploadPoolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    uploadPoolInfo.queueFamilyIndex = m_queueFamily;
    GENI_VK_CHECK(vkCreateCommandPool(m_device, &uploadPoolInfo, nullptr, &m_uploadPool));
    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    GENI_VK_CHECK(vkCreateFence(m_device, &fenceInfo, nullptr, &m_uploadFence));
    return true;
}

// ---------------------------------------------------------------------------------------
// Swapchain and render passes
// ---------------------------------------------------------------------------------------

bool VulkanDevice::CreateSwapchain()
{
    VkSurfaceCapabilitiesKHR caps;
    GENI_VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_physicalDevice, m_surface, &caps));

    uint32_t count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &count, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &count, formats.data());
    if (formats.empty())
    {
        GENI_LOG_ERROR("Vulkan: the surface reports no formats");
        return false;
    }

    // UNORM, not SRGB: shaders write display values directly, as the OpenGL renderer did.
    // Linear-space lighting with an sRGB backbuffer comes with HDR and tonemapping (M2).
    VkSurfaceFormatKHR chosen = formats[0];
    for (VkFormat preferred : {VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM})
    {
        auto it = std::find_if(formats.begin(), formats.end(), [&](const VkSurfaceFormatKHR &f) {
            return f.format == preferred && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        });
        if (it != formats.end())
        {
            chosen = *it;
            break;
        }
    }

    vkGetPhysicalDeviceSurfacePresentModesKHR(m_physicalDevice, m_surface, &count, nullptr);
    std::vector<VkPresentModeKHR> modes(count);
    vkGetPhysicalDeviceSurfacePresentModesKHR(m_physicalDevice, m_surface, &count, modes.data());
    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR; // always available
    if (!m_vsync)
    {
        for (VkPresentModeKHR preferred : {VK_PRESENT_MODE_IMMEDIATE_KHR, VK_PRESENT_MODE_MAILBOX_KHR})
        {
            if (std::find(modes.begin(), modes.end(), preferred) != modes.end())
            {
                presentMode = preferred;
                break;
            }
        }
    }

    int fbWidth = 0;
    int fbHeight = 0;
    glfwGetFramebufferSize(m_window, &fbWidth, &fbHeight);
    m_windowSize[0] = fbWidth;
    m_windowSize[1] = fbHeight;
    VkExtent2D extent = caps.currentExtent;
    if (extent.width == UINT32_MAX)
    {
        extent.width = std::clamp(static_cast<uint32_t>(fbWidth), caps.minImageExtent.width, caps.maxImageExtent.width);
        extent.height =
            std::clamp(static_cast<uint32_t>(fbHeight), caps.minImageExtent.height, caps.maxImageExtent.height);
    }
    if (extent.width == 0 || extent.height == 0)
    {
        return false;
    }

    uint32_t imageCount = std::max(caps.minImageCount + 1, 2u);
    if (caps.maxImageCount > 0)
    {
        imageCount = std::min(imageCount, caps.maxImageCount);
    }

    VkCompositeAlphaFlagBitsKHR compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    if (!(caps.supportedCompositeAlpha & compositeAlpha))
    {
        for (VkCompositeAlphaFlagBitsKHR candidate :
             {VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR, VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
              VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR})
        {
            if (caps.supportedCompositeAlpha & candidate)
            {
                compositeAlpha = candidate;
                break;
            }
        }
    }

    m_swapchainCanCapture = (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0;

    VkSwapchainCreateInfoKHR info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    info.surface = m_surface;
    info.minImageCount = imageCount;
    info.imageFormat = chosen.format;
    info.imageColorSpace = chosen.colorSpace;
    info.imageExtent = extent;
    info.imageArrayLayers = 1;
    info.imageUsage =
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | (m_swapchainCanCapture ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT : 0);
    info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.preTransform = caps.currentTransform;
    info.compositeAlpha = compositeAlpha;
    info.presentMode = presentMode;
    info.clipped = VK_TRUE;
    GENI_VK_CHECK(vkCreateSwapchainKHR(m_device, &info, nullptr, &m_swapchain));

    if (m_swapchainFormat != VK_FORMAT_UNDEFINED && m_swapchainFormat != chosen.format)
    {
        // Render passes (and pipelines made with them) depend on the format.
        GENI_LOG_WARN("Vulkan: swapchain format changed; render passes are recreated");
        for (auto &entry : m_renderPasses)
        {
            vkDestroyRenderPass(m_device, entry.second, nullptr);
        }
        m_renderPasses.clear();
    }
    m_swapchainFormat = chosen.format;
    m_extent = extent;
    m_minImageCount = imageCount;

    vkGetSwapchainImagesKHR(m_device, m_swapchain, &count, nullptr);
    m_swapchainImages.resize(count);
    vkGetSwapchainImagesKHR(m_device, m_swapchain, &count, m_swapchainImages.data());

    for (VkImage image : m_swapchainImages)
    {
        VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        viewInfo.image = image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = m_swapchainFormat;
        viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VkImageView view = VK_NULL_HANDLE;
        GENI_VK_CHECK(vkCreateImageView(m_device, &viewInfo, nullptr, &view));
        m_swapchainViews.push_back(view);

        VkSemaphoreCreateInfo semaphoreInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VkSemaphore semaphore = VK_NULL_HANDLE;
        GENI_VK_CHECK(vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &semaphore));
        m_renderFinished.push_back(semaphore);
    }

    // Depth buffer, shared by all frames: the render pass dependency orders its reuse.
    VkImageCreateInfo depthInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    depthInfo.imageType = VK_IMAGE_TYPE_2D;
    depthInfo.format = m_depthFormat;
    depthInfo.extent = {extent.width, extent.height, 1};
    depthInfo.mipLevels = 1;
    depthInfo.arrayLayers = 1;
    depthInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    depthInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    depthInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    VmaAllocationCreateInfo depthAlloc{};
    depthAlloc.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    depthAlloc.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
    GENI_VK_CHECK(vmaCreateImage(m_allocator, &depthInfo, &depthAlloc, &m_depthImage, &m_depthAllocation, nullptr));

    VkImageViewCreateInfo depthViewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    depthViewInfo.image = m_depthImage;
    depthViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    depthViewInfo.format = m_depthFormat;
    depthViewInfo.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
    GENI_VK_CHECK(vkCreateImageView(m_device, &depthViewInfo, nullptr, &m_depthView));

    // Framebuffers are created on demand for whichever pass shape is used (with or
    // without depth): see GetFramebuffer.

#if defined(GENI_ENABLE_IMGUI)
    if (m_imguiInitialized)
    {
        ImGui_ImplVulkan_SetMinImageCount(std::max(2u, m_minImageCount));
    }
#endif
    return true;
}

void VulkanDevice::DestroySwapchain()
{
    for (auto &entry : m_framebuffers)
    {
        vkDestroyFramebuffer(m_device, entry.second, nullptr);
    }
    m_framebuffers.clear();
    for (VkImageView view : m_swapchainViews)
    {
        vkDestroyImageView(m_device, view, nullptr);
    }
    m_swapchainViews.clear();
    for (VkSemaphore semaphore : m_renderFinished)
    {
        vkDestroySemaphore(m_device, semaphore, nullptr);
    }
    m_renderFinished.clear();
    m_swapchainImages.clear();

    if (m_depthView)
    {
        vkDestroyImageView(m_device, m_depthView, nullptr);
        m_depthView = VK_NULL_HANDLE;
    }
    if (m_depthImage)
    {
        vmaDestroyImage(m_allocator, m_depthImage, m_depthAllocation);
        m_depthImage = VK_NULL_HANDLE;
        m_depthAllocation = nullptr;
    }
    if (m_swapchain)
    {
        vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
        m_swapchain = VK_NULL_HANDLE;
    }
}

bool VulkanDevice::RecreateSwapchain()
{
    vkDeviceWaitIdle(m_device);
    DestroySwapchain();
    return CreateSwapchain();
}

VulkanDevice::RenderPassKey VulkanDevice::MakeBackbufferKey(bool withDepth, LoadOp colorLoad, StoreOp colorStore) const
{
    RenderPassKey key;
    key.hasColor = true;
    key.colorFormat = m_swapchainFormat;
    key.colorSamples = 1;
    key.hasDepth = withDepth;
    key.depthFormat = withDepth ? m_depthFormat : VK_FORMAT_UNDEFINED;
    key.depthSamples = 1;
    key.colorLoad = colorLoad;
    key.colorStore = colorStore;
    key.depthLoad = LoadOp::Clear;
    key.depthStore = StoreOp::DontCare;
    key.colorFinal = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    key.depthFinal = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    key.firstInFrame = true;
    key.backbuffer = true;
    return key;
}

VkRenderPass VulkanDevice::GetBackbufferRenderPass(bool withDepth)
{
    return GetRenderPass(MakeBackbufferKey(withDepth, LoadOp::Clear, StoreOp::Store));
}

VkRenderPass VulkanDevice::GetCompatiblePassForPipeline(const PipelineTargets &targets)
{
    if (targets.backbuffer)
    {
        return GetBackbufferRenderPass(targets.depthFormat != Format::Undefined);
    }

    RenderPassKey key;
    key.colorSamples = std::max(1u, targets.sampleCount);
    key.depthSamples = key.colorSamples;
    key.hasColor = !targets.colorFormats.empty();
    key.hasDepth = targets.depthFormat != Format::Undefined;
    if (key.hasColor)
    {
        key.colorFormat = ToVk(targets.colorFormats[0]);
        key.resolveFormat = key.colorFormat;
        key.hasResolve = key.colorSamples > 1;
    }
    if (key.hasDepth)
    {
        key.depthFormat = ToVk(targets.depthFormat);
    }
    key.colorLoad = LoadOp::Clear;
    key.colorStore = StoreOp::Store;
    key.depthLoad = LoadOp::Clear;
    key.depthStore = StoreOp::Store;
    key.colorFinal =
        key.hasResolve ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    // Shadow maps are sampled after the pass; the MSAA depth buffer is not.
    key.depthFinal =
        key.hasColor ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    key.firstInFrame = true;
    return GetRenderPass(key);
}

VkFramebuffer VulkanDevice::GetFramebuffer(
    VkRenderPass pass, VkImageView color, VkImageView depth, VkImageView resolve, VkExtent2D extent
)
{
    FramebufferKey key{pass, color, depth, resolve};
    auto it = m_framebuffers.find(key);
    if (it != m_framebuffers.end())
    {
        return it->second;
    }

    VkImageView attachments[3];
    uint32_t count = 0;
    if (color)
    {
        attachments[count++] = color;
    }
    if (depth)
    {
        attachments[count++] = depth;
    }
    if (resolve)
    {
        attachments[count++] = resolve;
    }

    VkFramebufferCreateInfo info{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    info.renderPass = pass;
    info.attachmentCount = count;
    info.pAttachments = attachments;
    info.width = extent.width;
    info.height = extent.height;
    info.layers = 1;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    const VkResult result = vkCreateFramebuffer(m_device, &info, nullptr, &framebuffer);
    if (result != VK_SUCCESS)
    {
        GENI_LOG_ERROR("vkCreateFramebuffer failed: %s", ResultString(result));
        return VK_NULL_HANDLE;
    }
    m_framebuffers[key] = framebuffer;
    return framebuffer;
}

void VulkanDevice::DestroyFramebuffersUsing(VkImageView view)
{
    for (auto it = m_framebuffers.begin(); it != m_framebuffers.end();)
    {
        const FramebufferKey &key = it->first;
        if (key.color == view || key.depth == view || key.resolve == view)
        {
            vkDestroyFramebuffer(m_device, it->second, nullptr);
            it = m_framebuffers.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

VkRenderPass VulkanDevice::GetRenderPass(const RenderPassKey &key)
{
    auto it = m_renderPasses.find(key);
    if (it != m_renderPasses.end())
    {
        return it->second;
    }

    std::vector<VkAttachmentDescription> attachments;
    VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depthRef{0, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkAttachmentReference resolveRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    uint32_t depthIndex = 0;
    uint32_t resolveIndex = 0;

    if (key.hasColor)
    {
        VkAttachmentDescription color{};
        color.format = key.colorFormat;
        color.samples = static_cast<VkSampleCountFlagBits>(key.colorSamples);
        color.loadOp = ToVk(key.colorLoad);
        color.storeOp = ToVk(key.colorStore);
        color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        color.initialLayout = key.firstInFrame                ? VK_IMAGE_LAYOUT_UNDEFINED
                              : key.backbuffer                ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
                              : key.colorLoad == LoadOp::Load ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
                                                              : VK_IMAGE_LAYOUT_UNDEFINED;
        color.finalLayout = key.colorFinal;
        attachments.push_back(color);
        colorRef.attachment = 0;
    }
    if (key.hasDepth)
    {
        VkAttachmentDescription depth{};
        depth.format = key.depthFormat;
        depth.samples = static_cast<VkSampleCountFlagBits>(key.depthSamples);
        depth.loadOp = ToVk(key.depthLoad);
        depth.storeOp = ToVk(key.depthStore);
        depth.stencilLoadOp = key.depthFormat == VK_FORMAT_D24_UNORM_S8_UINT && key.depthLoad == LoadOp::Clear
                                  ? VK_ATTACHMENT_LOAD_OP_CLEAR
                                  : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depth.initialLayout = key.depthLoad == LoadOp::Load ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
                                                            : VK_IMAGE_LAYOUT_UNDEFINED;
        depth.finalLayout = key.depthFinal;
        depthIndex = static_cast<uint32_t>(attachments.size());
        attachments.push_back(depth);
        depthRef.attachment = depthIndex;
    }
    if (key.hasResolve)
    {
        VkAttachmentDescription resolve{};
        resolve.format = key.resolveFormat;
        resolve.samples = VK_SAMPLE_COUNT_1_BIT;
        resolve.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        resolve.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        resolve.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        resolve.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        resolve.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        resolve.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        resolveIndex = static_cast<uint32_t>(attachments.size());
        resolveRef.attachment = resolveIndex;
        attachments.push_back(resolve);
    }

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    if (key.hasColor)
    {
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorRef;
        if (key.hasResolve)
        {
            subpass.pResolveAttachments = &resolveRef;
        }
    }
    if (key.hasDepth)
    {
        subpass.pDepthStencilAttachment = &depthRef;
    }

    // Waits for earlier passes (shadow map reads, resolve reads) before writing the
    // attachments, and makes the final layouts visible to later fragment shader reads.
    VkSubpassDependency dependencies[2]{};
    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                   VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                                   VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[0].srcAccessMask =
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
    dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                   VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                                   VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                    VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                                    VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    VkRenderPassCreateInfo info{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    info.attachmentCount = static_cast<uint32_t>(attachments.size());
    info.pAttachments = attachments.data();
    info.subpassCount = 1;
    info.pSubpasses = &subpass;
    info.dependencyCount = 2;
    info.pDependencies = dependencies;

    VkRenderPass renderPass = VK_NULL_HANDLE;
    const VkResult result = vkCreateRenderPass(m_device, &info, nullptr, &renderPass);
    if (result != VK_SUCCESS)
    {
        GENI_LOG_ERROR("vkCreateRenderPass failed: %s", ResultString(result));
        return VK_NULL_HANDLE;
    }
    m_renderPasses[key] = renderPass;
    return renderPass;
}

// ---------------------------------------------------------------------------------------
// Shutdown and deferred destruction
// ---------------------------------------------------------------------------------------

VulkanDevice::~VulkanDevice()
{
    if (m_device)
    {
        vkDeviceWaitIdle(m_device);
        ShutdownImGui();
        for (const PendingUpload &upload : m_pendingUploads)
        {
            vmaDestroyBuffer(m_allocator, upload.staging, upload.stagingAllocation);
        }
        m_pendingUploads.clear();
        RunDeferred(true);
        DestroyAllLiveObjects();

        for (VkDescriptorPool pool : m_descriptorPools)
        {
            vkDestroyDescriptorPool(m_device, pool, nullptr);
        }
        DestroySwapchain();
        for (auto &entry : m_renderPasses)
        {
            vkDestroyRenderPass(m_device, entry.second, nullptr);
        }
        for (auto &frame : m_frames)
        {
            vkDestroyCommandPool(m_device, frame.commandPool, nullptr);
            vkDestroySemaphore(m_device, frame.imageAvailable, nullptr);
            vkDestroyFence(m_device, frame.inFlight, nullptr);
        }
        vkDestroyCommandPool(m_device, m_uploadPool, nullptr);
        vkDestroyFence(m_device, m_uploadFence, nullptr);
        if (m_allocator)
        {
            vmaDestroyAllocator(m_allocator);
        }
        vkDestroyDevice(m_device, nullptr);
    }
    if (m_surface)
    {
        vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
    }
    if (m_messenger)
    {
        auto destroyMessenger = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(m_instance, "vkDestroyDebugUtilsMessengerEXT")
        );
        if (destroyMessenger)
        {
            destroyMessenger(m_instance, m_messenger, nullptr);
        }
    }
    if (m_instance)
    {
        vkDestroyInstance(m_instance, nullptr);
    }
}

void VulkanDevice::DestroyAllLiveObjects()
{
    const size_t live = m_buffers.LiveCount() + m_textures.LiveCount() + m_samplers.LiveCount() +
                        m_shaderModules.LiveCount() + m_bindGroupLayouts.LiveCount() + m_bindGroups.LiveCount() +
                        m_pipelines.LiveCount();
    if (live > 0)
    {
        GENI_LOG_WARN(
            "Vulkan device shut down with %zu objects still alive (buffers %zu, textures %zu, pipelines "
            "%zu); destroying them",
            live, m_buffers.LiveCount(), m_textures.LiveCount(), m_pipelines.LiveCount()
        );
    }

    m_pipelines.ForEach([&](PipelineRecord &r) {
        vkDestroyPipeline(m_device, r.pipeline, nullptr);
        vkDestroyPipelineLayout(m_device, r.layout, nullptr);
    });
    m_bindGroups.ForEach([&](BindGroupRecord &) {}); // freed with their pools
    m_bindGroupLayouts.ForEach([&](BindGroupLayoutRecord &r) {
        vkDestroyDescriptorSetLayout(m_device, r.layout, nullptr);
    });
    m_shaderModules.ForEach([&](ShaderModuleRecord &r) { vkDestroyShaderModule(m_device, r.module, nullptr); });
    m_samplers.ForEach([&](SamplerRecord &r) { vkDestroySampler(m_device, r.sampler, nullptr); });
    m_textures.ForEach([&](TextureRecord &r) {
        vkDestroyImageView(m_device, r.view, nullptr);
        vmaDestroyImage(m_allocator, r.image, r.allocation);
    });
    m_buffers.ForEach([&](BufferRecord &r) { vmaDestroyBuffer(m_allocator, r.buffer, r.allocation); });

    m_pipelines.Clear();
    m_bindGroups.Clear();
    m_bindGroupLayouts.Clear();
    m_shaderModules.Clear();
    m_samplers.Clear();
    m_textures.Clear();
    m_buffers.Clear();
}

void VulkanDevice::Defer(std::function<void()> destroy)
{
    // The object may be used by the frame being recorded (m_frameNumber) or, between
    // frames, by the one just submitted; it is safe once frame m_frameNumber completes.
    m_pendingDestroys.push_back({m_frameNumber, std::move(destroy)});
}

void VulkanDevice::RunDeferred(bool all)
{
    // Called right after waiting for this slot's fence: every frame up to
    // m_frameNumber - FramesInFlight has completed.
    size_t kept = 0;
    for (size_t i = 0; i < m_pendingDestroys.size(); ++i)
    {
        auto &entry = m_pendingDestroys[i];
        if (all || entry.frameNumber + FramesInFlight <= m_frameNumber)
        {
            entry.destroy();
        }
        else
        {
            // Self-move-assignment would empty the std::function.
            if (kept != i)
            {
                m_pendingDestroys[kept] = std::move(entry);
            }
            ++kept;
        }
    }
    m_pendingDestroys.resize(kept);
}

void VulkanDevice::ImmediateSubmit(const std::function<void(VkCommandBuffer)> &record)
{
    VkCommandBufferAllocateInfo allocInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocInfo.commandPool = m_uploadPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(m_device, &allocInfo, &cmd);

    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);
    record(cmd);
    vkEndCommandBuffer(cmd);

    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    vkQueueSubmit(m_queue, 1, &submit, m_uploadFence);
    vkWaitForFences(m_device, 1, &m_uploadFence, VK_TRUE, UINT64_MAX);
    vkResetFences(m_device, 1, &m_uploadFence);
    vkFreeCommandBuffers(m_device, m_uploadPool, 1, &cmd);
}

void VulkanDevice::SetDebugName(VkObjectType type, uint64_t object, const char *name)
{
    if (!m_setObjectName || !name || !object)
    {
        return;
    }
    VkDebugUtilsObjectNameInfoEXT info{VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT};
    info.objectType = type;
    info.objectHandle = object;
    info.pObjectName = name;
    m_setObjectName(m_device, &info);
}

void VulkanDevice::WaitIdle()
{
    if (m_device)
    {
        vkDeviceWaitIdle(m_device);
    }
}

// ---------------------------------------------------------------------------------------
// Resources
// ---------------------------------------------------------------------------------------

BufferHandle VulkanDevice::CreateBuffer(const BufferDesc &desc, const void *initialData)
{
    GENI_ASSERT(desc.size > 0);

    VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    info.size = desc.size;
    // Every buffer can be a copy source or destination: staging, uploads and read-back.
    info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if (HasFlag(desc.usage, BufferUsage::Vertex))
    {
        info.usage |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    }
    if (HasFlag(desc.usage, BufferUsage::Index))
    {
        info.usage |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    }
    if (HasFlag(desc.usage, BufferUsage::Uniform))
    {
        info.usage |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    }
    if (HasFlag(desc.usage, BufferUsage::Storage))
    {
        info.usage |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    }

    VmaAllocationCreateInfo allocInfo{};
    if (desc.memory == MemoryUsage::CpuToGpu)
    {
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        // Coherent, so writes through the mapped pointer need no flush.
        allocInfo.requiredFlags = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    }
    else
    {
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    }

    BufferRecord record;
    record.size = desc.size;
    VmaAllocationInfo result{};
    const VkResult vr = vmaCreateBuffer(m_allocator, &info, &allocInfo, &record.buffer, &record.allocation, &result);
    if (vr != VK_SUCCESS)
    {
        GENI_LOG_ERROR(
            "Vulkan: cannot create a %llu-byte buffer: %s", static_cast<unsigned long long>(desc.size), ResultString(vr)
        );
        return {};
    }
    record.mapped = desc.memory == MemoryUsage::CpuToGpu ? result.pMappedData : nullptr;
    SetDebugName(VK_OBJECT_TYPE_BUFFER, reinterpret_cast<uint64_t>(record.buffer), desc.debugName);

    BufferHandle handle = m_buffers.Add(record);
    if (initialData)
    {
        if (record.mapped)
        {
            std::memcpy(record.mapped, initialData, desc.size);
        }
        else if (!m_frameActive)
        {
            // Between frames: stage now, copy at the start of the next frame.
            VkBufferCreateInfo stagingInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
            stagingInfo.size = desc.size;
            stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
            VmaAllocationCreateInfo stagingAlloc{};
            stagingAlloc.usage = VMA_MEMORY_USAGE_AUTO;
            stagingAlloc.flags =
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
            stagingAlloc.requiredFlags = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
            PendingUpload upload;
            VmaAllocationInfo stagingResult{};
            if (vmaCreateBuffer(
                    m_allocator, &stagingInfo, &stagingAlloc, &upload.staging, &upload.stagingAllocation, &stagingResult
                ) != VK_SUCCESS)
            {
                GENI_LOG_ERROR(
                    "Vulkan: cannot create a staging buffer for a %llu-byte upload",
                    static_cast<unsigned long long>(desc.size)
                );
                return handle;
            }
            std::memcpy(stagingResult.pMappedData, initialData, desc.size);
            upload.destination = record.buffer;
            upload.size = desc.size;
            m_pendingUploads.push_back(upload);
        }
        else
        {
            // Inside a frame the copy must happen before this frame's draws: submit it now.
            // A new buffer is not in use yet, so no need to wait for the GPU.
            BufferDesc stagingDesc{desc.size, BufferUsage::Vertex, MemoryUsage::CpuToGpu, "staging"};
            BufferHandle staging = CreateBuffer(stagingDesc, initialData);
            VkBuffer src = m_buffers.Get(staging)->buffer;
            ImmediateSubmit([&](VkCommandBuffer cmd) {
                VkBufferCopy region{0, 0, desc.size};
                vkCmdCopyBuffer(cmd, src, record.buffer, 1, &region);
            });
            BufferRecord stagingRecord;
            m_buffers.Remove(staging, stagingRecord);
            vmaDestroyBuffer(m_allocator, stagingRecord.buffer, stagingRecord.allocation);
        }
    }
    return handle;
}

void VulkanDevice::RecordPendingUploads(VkCommandBuffer cmd)
{
    if (m_pendingUploads.empty())
    {
        return;
    }
    for (const PendingUpload &upload : m_pendingUploads)
    {
        VkBufferCopy region{0, 0, upload.size};
        vkCmdCopyBuffer(cmd, upload.staging, upload.destination, 1, &region);
    }
    VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT |
                            VK_ACCESS_UNIFORM_READ_BIT | VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(
        cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 1, &barrier, 0, nullptr, 0, nullptr
    );
    for (const PendingUpload &upload : m_pendingUploads)
    {
        Defer([this, upload]() { vmaDestroyBuffer(m_allocator, upload.staging, upload.stagingAllocation); });
    }
    m_pendingUploads.clear();
}

void VulkanDevice::FlushPendingUploads()
{
    if (m_pendingUploads.empty())
    {
        return;
    }
    ImmediateSubmit([&](VkCommandBuffer cmd) { RecordPendingUploads(cmd); });
}

void VulkanDevice::DestroyBuffer(BufferHandle buffer)
{
    BufferRecord record;
    if (!m_buffers.Remove(buffer, record))
    {
        return;
    }
    // A buffer destroyed before its first frame never needs its upload.
    for (size_t i = 0; i < m_pendingUploads.size();)
    {
        if (m_pendingUploads[i].destination == record.buffer)
        {
            vmaDestroyBuffer(m_allocator, m_pendingUploads[i].staging, m_pendingUploads[i].stagingAllocation);
            m_pendingUploads[i] = m_pendingUploads.back();
            m_pendingUploads.pop_back();
        }
        else
        {
            ++i;
        }
    }
    Defer([this, record]() { vmaDestroyBuffer(m_allocator, record.buffer, record.allocation); });
}

void VulkanDevice::UpdateBuffer(BufferHandle buffer, uint64_t offset, const void *data, uint64_t size)
{
    BufferRecord *record = m_buffers.Get(buffer);
    if (!record || size == 0)
    {
        return;
    }
    GENI_ASSERT(offset + size <= record->size);

    // A pending first upload must not land after (and over) this update.
    FlushPendingUploads();
    // The buffer may be read by frames still in flight.
    vkQueueWaitIdle(m_queue);

    if (record->mapped)
    {
        std::memcpy(static_cast<uint8_t *>(record->mapped) + offset, data, size);
        return;
    }

    BufferDesc stagingDesc{size, BufferUsage::Vertex, MemoryUsage::CpuToGpu, "staging"};
    BufferHandle staging = CreateBuffer(stagingDesc, data);
    VkBuffer src = m_buffers.Get(staging)->buffer;
    VkBuffer dst = m_buffers.Get(buffer)->buffer;
    ImmediateSubmit([&](VkCommandBuffer cmd) {
        VkBufferCopy region{0, offset, size};
        vkCmdCopyBuffer(cmd, src, dst, 1, &region);
    });
    BufferRecord stagingRecord;
    m_buffers.Remove(staging, stagingRecord);
    vmaDestroyBuffer(m_allocator, stagingRecord.buffer, stagingRecord.allocation);
}

void *VulkanDevice::MapBuffer(BufferHandle buffer)
{
    BufferRecord *record = m_buffers.Get(buffer);
    return record ? record->mapped : nullptr;
}

TextureHandle VulkanDevice::CreateTexture(const TextureDesc &desc, const void *const *mipData)
{
    const VkFormat format = ToVk(desc.format);
    if (format == VK_FORMAT_UNDEFINED)
    {
        GENI_LOG_ERROR("Vulkan: unsupported texture format %u", static_cast<unsigned>(desc.format));
        return {};
    }
    const bool isDepth = IsDepthFormat(desc.format);
    if (isDepth && !HasFlag(desc.usage, TextureUsage::DepthStencil))
    {
        GENI_LOG_ERROR("Vulkan: a depth texture must have TextureUsage::DepthStencil");
        return {};
    }
    if (desc.format == Format::Depth24UnormStencil8 && HasFlag(desc.usage, TextureUsage::Sampled))
    {
        // A sampled view may have only one aspect; use Device::GetSampledDepthFormat().
        GENI_LOG_ERROR("Vulkan: a sampled depth texture needs a depth-only format");
        return {};
    }
    if (!HasFlag(desc.usage, TextureUsage::Sampled) && !HasFlag(desc.usage, TextureUsage::RenderTarget) &&
        !HasFlag(desc.usage, TextureUsage::DepthStencil))
    {
        GENI_LOG_ERROR("Vulkan: texture '%s' has no usage", desc.debugName ? desc.debugName : "");
        return {};
    }
    const uint32_t mipLevels =
        HasFlag(desc.usage, TextureUsage::Sampled) && !isDepth ? std::max(1u, desc.mipLevels) : 1u;
    const uint32_t sampleCount = std::max(1u, desc.sampleCount);

    // Bytes per pixel for the staging upload (targets are never uploaded).
    const bool upload = mipData && !isDepth && HasFlag(desc.usage, TextureUsage::Sampled);
    const bool compressed = IsCompressedFormat(desc.format);
    if (compressed && HasFlag(desc.usage, TextureUsage::RenderTarget))
    {
        GENI_LOG_ERROR(
            "Vulkan: compressed texture '%s' cannot be a render target", desc.debugName ? desc.debugName : ""
        );
        return {};
    }

    VkImageUsageFlags usage = 0;
    if (HasFlag(desc.usage, TextureUsage::Sampled))
    {
        usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
    }
    if (HasFlag(desc.usage, TextureUsage::RenderTarget))
    {
        usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    }
    if (HasFlag(desc.usage, TextureUsage::DepthStencil))
    {
        usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    }
    if (upload)
    {
        usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    }

    VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = format;
    info.extent = {std::max(1u, desc.width), std::max(1u, desc.height), 1};
    info.mipLevels = mipLevels;
    info.arrayLayers = 1;
    info.samples = static_cast<VkSampleCountFlagBits>(sampleCount);
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = usage;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

    TextureRecord record;
    record.format = desc.format;
    record.usage = desc.usage;
    record.sampleCount = sampleCount;
    record.width = info.extent.width;
    record.height = info.extent.height;
    record.aspect = ToAspect(desc.format);
    VkResult vr = vmaCreateImage(m_allocator, &info, &allocInfo, &record.image, &record.allocation, nullptr);
    if (vr != VK_SUCCESS)
    {
        GENI_LOG_ERROR("Vulkan: cannot create a %ux%u texture: %s", desc.width, desc.height, ResultString(vr));
        return {};
    }
    SetDebugName(VK_OBJECT_TYPE_IMAGE, reinterpret_cast<uint64_t>(record.image), desc.debugName);

    // Every level in one staging buffer.
    std::vector<VkBufferImageCopy> regions;
    uint64_t totalSize = 0;
    if (upload)
    {
        for (uint32_t level = 0; level < mipLevels; ++level)
        {
            const uint32_t w = std::max(1u, desc.width >> level);
            const uint32_t h = std::max(1u, desc.height >> level);
            VkBufferImageCopy region{};
            region.bufferOffset = totalSize;
            region.imageSubresource = {record.aspect, level, 0, 1};
            region.imageExtent = {w, h, 1};
            regions.push_back(region);
            const uint64_t levelSize =
                desc.mipByteSizes.size() > level ? desc.mipByteSizes[level] : TextureLevelSize(desc.format, w, h);
            totalSize += levelSize;
        }
    }

    BufferHandle staging;
    if (upload)
    {
        staging = CreateBuffer({totalSize, BufferUsage::Vertex, MemoryUsage::CpuToGpu, "texture staging"}, nullptr);
        auto *dst = static_cast<uint8_t *>(MapBuffer(staging));
        for (uint32_t level = 0; level < mipLevels; ++level)
        {
            const uint32_t w = std::max(1u, desc.width >> level);
            const uint32_t h = std::max(1u, desc.height >> level);
            const uint64_t size =
                desc.mipByteSizes.size() > level ? desc.mipByteSizes[level] : TextureLevelSize(desc.format, w, h);
            if (mipData[level])
            {
                std::memcpy(dst + regions[level].bufferOffset, mipData[level], size);
            }
        }
    }
    VkBuffer stagingBuffer = staging.IsValid() ? m_buffers.Get(staging)->buffer : VK_NULL_HANDLE;

    ImmediateSubmit([&](VkCommandBuffer cmd) {
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = record.image;
        barrier.subresourceRange = {record.aspect, 0, mipLevels, 0, 1};

        if (upload)
        {
            barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            vkCmdPipelineBarrier(
                cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                &barrier
            );

            vkCmdCopyBufferToImage(
                cmd, stagingBuffer, record.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                static_cast<uint32_t>(regions.size()), regions.data()
            );

            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        }
        else
        {
            barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            barrier.srcAccessMask = 0;
        }

        // Sampled targets (shadow maps, the resolved HDR image) start readable so a
        // descriptor set created before their first render pass is never in a stale layout.
        if (HasFlag(desc.usage, TextureUsage::Sampled))
        {
            barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(
                cmd, upload ? VK_PIPELINE_STAGE_TRANSFER_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier
            );
        }
    });

    if (staging.IsValid())
    {
        BufferRecord stagingRecord;
        m_buffers.Remove(staging, stagingRecord);
        vmaDestroyBuffer(m_allocator, stagingRecord.buffer, stagingRecord.allocation);
    }

    VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    viewInfo.image = record.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange = {record.aspect, 0, mipLevels, 0, 1};
    vr = vkCreateImageView(m_device, &viewInfo, nullptr, &record.view);
    if (vr != VK_SUCCESS)
    {
        GENI_LOG_ERROR("vkCreateImageView failed: %s", ResultString(vr));
        vmaDestroyImage(m_allocator, record.image, record.allocation);
        return {};
    }
    return m_textures.Add(record);
}

void VulkanDevice::DestroyTexture(TextureHandle texture)
{
    TextureRecord record;
    if (!m_textures.Remove(texture, record))
    {
        return;
    }
    // Framebuffers referring to the view cannot outlive it; both are destroyed once the
    // GPU is done with the frames that may still use them.
    Defer([this, record]() {
        DestroyFramebuffersUsing(record.view);
        vkDestroyImageView(m_device, record.view, nullptr);
        vmaDestroyImage(m_allocator, record.image, record.allocation);
    });
}

bool VulkanDevice::IsTextureFormatSupported(Format format) const
{
    const VkFormat vkFormat = ToVk(format);
    if (vkFormat == VK_FORMAT_UNDEFINED || m_physicalDevice == VK_NULL_HANDLE)
    {
        return false;
    }
    VkFormatProperties properties{};
    vkGetPhysicalDeviceFormatProperties(m_physicalDevice, vkFormat, &properties);
    const VkFormatFeatureFlags needed = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
                                        VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT |
                                        VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
    return (properties.optimalTilingFeatures & needed) == needed;
}

SamplerHandle VulkanDevice::CreateSampler(const SamplerDesc &desc)
{
    VkSamplerCreateInfo info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    info.magFilter = ToVk(desc.magFilter);
    info.minFilter = ToVk(desc.minFilter);
    info.mipmapMode =
        desc.mipmapFilter == Filter::Nearest ? VK_SAMPLER_MIPMAP_MODE_NEAREST : VK_SAMPLER_MIPMAP_MODE_LINEAR;
    info.addressModeU = ToVk(desc.addressU);
    info.addressModeV = ToVk(desc.addressV);
    info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    info.maxLod = VK_LOD_CLAMP_NONE;

    SamplerRecord record;
    const VkResult vr = vkCreateSampler(m_device, &info, nullptr, &record.sampler);
    if (vr != VK_SUCCESS)
    {
        GENI_LOG_ERROR("vkCreateSampler failed: %s", ResultString(vr));
        return {};
    }
    return m_samplers.Add(record);
}

void VulkanDevice::DestroySampler(SamplerHandle sampler)
{
    SamplerRecord record;
    if (!m_samplers.Remove(sampler, record))
    {
        return;
    }
    Defer([this, record]() { vkDestroySampler(m_device, record.sampler, nullptr); });
}

ShaderModuleHandle VulkanDevice::CreateShaderModule(const ShaderModuleDesc &desc)
{
    if (!desc.code || desc.codeSize == 0 || desc.codeSize % 4 != 0)
    {
        GENI_LOG_ERROR("Vulkan: shader module '%s' has no valid SPIR-V", desc.debugName ? desc.debugName : "");
        return {};
    }
    VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    info.codeSize = desc.codeSize;
    info.pCode = desc.code;

    ShaderModuleRecord record;
    record.stage = desc.stage;
    const VkResult vr = vkCreateShaderModule(m_device, &info, nullptr, &record.module);
    if (vr != VK_SUCCESS)
    {
        GENI_LOG_ERROR(
            "vkCreateShaderModule failed for '%s': %s", desc.debugName ? desc.debugName : "", ResultString(vr)
        );
        return {};
    }
    SetDebugName(VK_OBJECT_TYPE_SHADER_MODULE, reinterpret_cast<uint64_t>(record.module), desc.debugName);
    return m_shaderModules.Add(record);
}

void VulkanDevice::DestroyShaderModule(ShaderModuleHandle module)
{
    ShaderModuleRecord record;
    if (!m_shaderModules.Remove(module, record))
    {
        return;
    }
    // Only needed during pipeline creation, so no need to wait for the GPU.
    vkDestroyShaderModule(m_device, record.module, nullptr);
}

BindGroupLayoutHandle VulkanDevice::CreateBindGroupLayout(const BindGroupLayoutDesc &desc)
{
    std::vector<VkDescriptorSetLayoutBinding> bindings;
    for (const auto &entry : desc.entries)
    {
        VkDescriptorSetLayoutBinding binding{};
        binding.binding = entry.binding;
        binding.descriptorType = ToVk(entry.type);
        binding.descriptorCount = 1;
        binding.stageFlags = ToVk(entry.visibility);
        bindings.push_back(binding);
    }

    VkDescriptorSetLayoutCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    info.bindingCount = static_cast<uint32_t>(bindings.size());
    info.pBindings = bindings.data();

    BindGroupLayoutRecord record;
    record.entries = desc.entries;
    const VkResult vr = vkCreateDescriptorSetLayout(m_device, &info, nullptr, &record.layout);
    if (vr != VK_SUCCESS)
    {
        GENI_LOG_ERROR("vkCreateDescriptorSetLayout failed: %s", ResultString(vr));
        return {};
    }
    return m_bindGroupLayouts.Add(record);
}

void VulkanDevice::DestroyBindGroupLayout(BindGroupLayoutHandle layout)
{
    BindGroupLayoutRecord record;
    if (!m_bindGroupLayouts.Remove(layout, record))
    {
        return;
    }
    Defer([this, record]() { vkDestroyDescriptorSetLayout(m_device, record.layout, nullptr); });
}

BindGroupHandle VulkanDevice::CreateBindGroup(const BindGroupDesc &desc)
{
    BindGroupLayoutRecord *layout = m_bindGroupLayouts.Get(desc.layout);
    if (!layout)
    {
        return {};
    }

    auto tryAllocate = [&](VkDescriptorPool pool, VkDescriptorSet &set) {
        VkDescriptorSetAllocateInfo allocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocInfo.descriptorPool = pool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &layout->layout;
        return vkAllocateDescriptorSets(m_device, &allocInfo, &set);
    };

    BindGroupRecord record;
    VkResult vr = VK_ERROR_OUT_OF_POOL_MEMORY;
    if (!m_descriptorPools.empty())
    {
        vr = tryAllocate(m_descriptorPools.back(), record.set);
    }
    if (vr == VK_ERROR_OUT_OF_POOL_MEMORY || vr == VK_ERROR_FRAGMENTED_POOL)
    {
        VkDescriptorPoolSize sizes[] = {
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1024},
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 256},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 32},
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1024},
        };
        VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        poolInfo.maxSets = 1024;
        poolInfo.poolSizeCount = 4;
        poolInfo.pPoolSizes = sizes;
        VkDescriptorPool pool = VK_NULL_HANDLE;
        vr = vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &pool);
        if (vr != VK_SUCCESS)
        {
            GENI_LOG_ERROR("vkCreateDescriptorPool failed: %s", ResultString(vr));
            return {};
        }
        m_descriptorPools.push_back(pool);
        vr = tryAllocate(pool, record.set);
    }
    if (vr != VK_SUCCESS)
    {
        GENI_LOG_ERROR("vkAllocateDescriptorSets failed: %s", ResultString(vr));
        return {};
    }
    record.pool = m_descriptorPools.back();

    std::vector<VkWriteDescriptorSet> writes;
    std::vector<VkDescriptorBufferInfo> bufferInfos;
    std::vector<VkDescriptorImageInfo> imageInfos;
    bufferInfos.reserve(desc.entries.size());
    imageInfos.reserve(desc.entries.size());

    for (const auto &entry : desc.entries)
    {
        auto layoutEntry =
            std::find_if(layout->entries.begin(), layout->entries.end(), [&](const BindGroupLayoutEntry &e) {
                return e.binding == entry.binding;
            });
        if (layoutEntry == layout->entries.end())
        {
            GENI_LOG_ERROR("Vulkan: bind group entry %u is not in its layout", entry.binding);
            continue;
        }

        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = record.set;
        write.dstBinding = entry.binding;
        write.descriptorCount = 1;
        write.descriptorType = ToVk(layoutEntry->type);

        if (layoutEntry->type == BindingType::SampledTexture)
        {
            TextureRecord *texture = m_textures.Get(entry.texture);
            SamplerRecord *sampler = m_samplers.Get(entry.sampler);
            if (!texture || !sampler)
            {
                continue;
            }
            imageInfos.push_back({sampler->sampler, texture->view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
            write.pImageInfo = &imageInfos.back();
        }
        else
        {
            BufferRecord *buffer = m_buffers.Get(entry.buffer);
            if (!buffer)
            {
                continue;
            }
            bufferInfos.push_back({buffer->buffer, entry.offset, entry.size ? entry.size : VK_WHOLE_SIZE});
            write.pBufferInfo = &bufferInfos.back();
        }
        writes.push_back(write);
    }
    vkUpdateDescriptorSets(m_device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    return m_bindGroups.Add(record);
}

void VulkanDevice::DestroyBindGroup(BindGroupHandle group)
{
    BindGroupRecord record;
    if (!m_bindGroups.Remove(group, record))
    {
        return;
    }
    Defer([this, record]() { vkFreeDescriptorSets(m_device, record.pool, 1, &record.set); });
}

PipelineHandle VulkanDevice::CreatePipeline(const PipelineDesc &desc)
{
    ShaderModuleRecord *vs = m_shaderModules.Get(desc.vertexShader);
    ShaderModuleRecord *fs = m_shaderModules.Get(desc.fragmentShader);
    if (!vs || !fs)
    {
        return {};
    }

    std::vector<VkDescriptorSetLayout> setLayouts;
    for (BindGroupLayoutHandle handle : desc.bindGroupLayouts)
    {
        BindGroupLayoutRecord *layout = m_bindGroupLayouts.Get(handle);
        if (!layout)
        {
            return {};
        }
        setLayouts.push_back(layout->layout);
    }

    PipelineRecord record;
    VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
    layoutInfo.pSetLayouts = setLayouts.data();
    VkResult vr = vkCreatePipelineLayout(m_device, &layoutInfo, nullptr, &record.layout);
    if (vr != VK_SUCCESS)
    {
        GENI_LOG_ERROR("vkCreatePipelineLayout failed: %s", ResultString(vr));
        return {};
    }

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vs->module;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fs->module;
    stages[1].pName = "main";

    std::vector<VkVertexInputBindingDescription> bindings;
    for (const auto &buffer : desc.vertexLayout.buffers)
    {
        bindings.push_back(
            {buffer.binding, buffer.stride,
             buffer.stepMode == VertexStepMode::Instance ? VK_VERTEX_INPUT_RATE_INSTANCE : VK_VERTEX_INPUT_RATE_VERTEX}
        );
    }
    std::vector<VkVertexInputAttributeDescription> attributes;
    for (const auto &attribute : desc.vertexLayout.attributes)
    {
        attributes.push_back({attribute.location, attribute.buffer, ToVk(attribute.format), attribute.offset});
    }
    VkPipelineVertexInputStateCreateInfo vertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertexInput.vertexBindingDescriptionCount = static_cast<uint32_t>(bindings.size());
    vertexInput.pVertexBindingDescriptions = bindings.data();
    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size());
    vertexInput.pVertexAttributeDescriptions = attributes.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    inputAssembly.topology = desc.topology == PrimitiveTopology::LineList ? VK_PRIMITIVE_TOPOLOGY_LINE_LIST
                                                                          : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = desc.cullMode == CullMode::Back    ? VK_CULL_MODE_BACK_BIT
                      : desc.cullMode == CullMode::Front ? VK_CULL_MODE_FRONT_BIT
                                                         : VK_CULL_MODE_NONE;
    // The viewport is flipped (negative height) so clip-space Y points up; with the flip,
    // counter-clockwise in clip space stays counter-clockwise here.
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1.0f;
    raster.depthBiasEnable = (desc.depthBiasConstant != 0.0f || desc.depthBiasSlope != 0.0f) ? VK_TRUE : VK_FALSE;
    raster.depthBiasConstantFactor = desc.depthBiasConstant;
    raster.depthBiasSlopeFactor = desc.depthBiasSlope;

    VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisample.rasterizationSamples = static_cast<VkSampleCountFlagBits>(std::max(1u, desc.targets.sampleCount));

    VkPipelineDepthStencilStateCreateInfo depth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depth.depthTestEnable = desc.depthTest ? VK_TRUE : VK_FALSE;
    depth.depthWriteEnable = desc.depthWrite ? VK_TRUE : VK_FALSE;
    depth.depthCompareOp = ToVk(desc.depthCompare);

    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    if (desc.blend == BlendMode::Alpha)
    {
        blendAttachment.blendEnable = VK_TRUE;
        blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
        blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
    }
    VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    // A depth-only pipeline has no colour attachment to blend into.
    const bool hasColorTarget = desc.targets.backbuffer || !desc.targets.colorFormats.empty();
    if (hasColorTarget)
    {
        blend.attachmentCount = 1;
        blend.pAttachments = &blendAttachment;
    }

    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dynamicStates;

    VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    info.stageCount = 2;
    info.pStages = stages;
    info.pVertexInputState = &vertexInput;
    info.pInputAssemblyState = &inputAssembly;
    info.pViewportState = &viewport;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &multisample;
    info.pDepthStencilState = &depth;
    info.pColorBlendState = &blend;
    info.pDynamicState = &dynamic;
    info.layout = record.layout;
    info.renderPass = GetCompatiblePassForPipeline(desc.targets);
    info.subpass = 0;

    vr = vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &info, nullptr, &record.pipeline);
    if (vr != VK_SUCCESS)
    {
        GENI_LOG_ERROR(
            "vkCreateGraphicsPipelines failed for '%s': %s", desc.debugName ? desc.debugName : "", ResultString(vr)
        );
        vkDestroyPipelineLayout(m_device, record.layout, nullptr);
        return {};
    }
    SetDebugName(VK_OBJECT_TYPE_PIPELINE, reinterpret_cast<uint64_t>(record.pipeline), desc.debugName);
    return m_pipelines.Add(record);
}

void VulkanDevice::DestroyPipeline(PipelineHandle pipeline)
{
    PipelineRecord record;
    if (!m_pipelines.Remove(pipeline, record))
    {
        return;
    }
    Defer([this, record]() {
        vkDestroyPipeline(m_device, record.pipeline, nullptr);
        vkDestroyPipelineLayout(m_device, record.layout, nullptr);
    });
}

// ---------------------------------------------------------------------------------------
// Frames
// ---------------------------------------------------------------------------------------

CommandList *VulkanDevice::BeginFrame()
{
    GENI_PROFILE_SCOPE("VulkanDevice::BeginFrame");
    GENI_ASSERT_MSG(!m_frameActive, "BeginFrame called twice without EndFrame");
    m_frameActive = true;
    m_frameHasImage = false;
    m_frameSlot = static_cast<uint32_t>(m_frameNumber % FramesInFlight);
    Frame &frame = m_frames[m_frameSlot];

    {
        GENI_PROFILE_SCOPE("WaitForFrameFence");
        vkWaitForFences(m_device, 1, &frame.inFlight, VK_TRUE, UINT64_MAX);
    }
    RunDeferred(false);

    int fbWidth = 0;
    int fbHeight = 0;
    glfwGetFramebufferSize(m_window, &fbWidth, &fbHeight);
    if (fbWidth <= 0 || fbHeight <= 0)
    {
        return nullptr; // minimized
    }

    // Resized since the swapchain was made (comparing against the window, not the
    // surface extent, which may legitimately differ from it).
    if (!m_swapchain || fbWidth != m_windowSize[0] || fbHeight != m_windowSize[1])
    {
        if (!RecreateSwapchain())
        {
            return nullptr;
        }
    }

    VkResult result =
        vkAcquireNextImageKHR(m_device, m_swapchain, UINT64_MAX, frame.imageAvailable, VK_NULL_HANDLE, &m_imageIndex);
    if (result == VK_ERROR_OUT_OF_DATE_KHR)
    {
        if (!RecreateSwapchain())
        {
            return nullptr;
        }
        result = vkAcquireNextImageKHR(
            m_device, m_swapchain, UINT64_MAX, frame.imageAvailable, VK_NULL_HANDLE, &m_imageIndex
        );
    }
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
    {
        GENI_LOG_ERROR("vkAcquireNextImageKHR failed: %s", ResultString(result));
        return nullptr;
    }

    vkResetFences(m_device, 1, &frame.inFlight);
    vkResetCommandPool(m_device, frame.commandPool, 0);
    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(frame.commandBuffer, &beginInfo);

    RecordPendingUploads(frame.commandBuffer);

    m_commandList.m_device = this;
    m_commandList.m_cmd = frame.commandBuffer;
    m_commandList.m_layout = VK_NULL_HANDLE;
    m_commandList.m_inRenderPass = false;
    m_commandList.m_backbufferWritten = false;
    m_frameHasImage = true;
    return &m_commandList;
}

void VulkanDevice::EndFrame(BackbufferCapture *capture)
{
    GENI_PROFILE_SCOPE("VulkanDevice::EndFrame");
    GENI_ASSERT_MSG(m_frameActive, "EndFrame without BeginFrame");
    m_frameActive = false;

    if (!m_frameHasImage)
    {
        ++m_frameNumber;
        return;
    }

    Frame &frame = m_frames[m_frameSlot];
    VkCommandBuffer cmd = frame.commandBuffer;

    if (m_commandList.m_inRenderPass)
    {
        GENI_ASSERT_MSG(false, "EndFrame with a render pass still open");
        m_commandList.EndRenderPass();
    }
    if (!m_commandList.m_backbufferWritten)
    {
        // Presenting needs the image in PRESENT_SRC layout, which a render pass provides.
        RenderPassDesc clear;
        m_commandList.BeginRenderPass(clear);
        m_commandList.EndRenderPass();
    }

    BufferRecord readback;
    const bool capturing = capture && m_swapchainCanCapture;
    if (capture && !m_swapchainCanCapture)
    {
        GENI_LOG_ERROR("Screenshot skipped: this swapchain cannot be read back (no TRANSFER_SRC usage)");
    }
    if (capturing)
    {
        VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        info.size = uint64_t(m_extent.width) * m_extent.height * 4;
        info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        VmaAllocationInfo result{};
        vmaCreateBuffer(m_allocator, &info, &allocInfo, &readback.buffer, &readback.allocation, &result);
        readback.mapped = result.pMappedData;
        readback.size = info.size;

        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = m_swapchainImages[m_imageIndex];
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        barrier.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier(
            cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
            nullptr, 1, &barrier
        );

        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {m_extent.width, m_extent.height, 1};
        vkCmdCopyImageToBuffer(
            cmd, m_swapchainImages[m_imageIndex], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback.buffer, 1, &region
        );

        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = 0;
        vkCmdPipelineBarrier(
            cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1,
            &barrier
        );

        VkBufferMemoryBarrier hostBarrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
        hostBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        hostBarrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        hostBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        hostBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        hostBarrier.buffer = readback.buffer;
        hostBarrier.size = VK_WHOLE_SIZE;
        vkCmdPipelineBarrier(
            cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr, 1, &hostBarrier, 0, nullptr
        );
    }

    vkEndCommandBuffer(cmd);

    const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSemaphore renderFinished = m_renderFinished[m_imageIndex];
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = &frame.imageAvailable;
    submit.pWaitDstStageMask = &waitStage;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &renderFinished;
    VkResult result = vkQueueSubmit(m_queue, 1, &submit, frame.inFlight);
    if (result != VK_SUCCESS)
    {
        GENI_LOG_ERROR("vkQueueSubmit failed: %s", ResultString(result));
    }

    VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &renderFinished;
    present.swapchainCount = 1;
    present.pSwapchains = &m_swapchain;
    present.pImageIndices = &m_imageIndex;
    {
        GENI_PROFILE_SCOPE("vkQueuePresentKHR");
        result = vkQueuePresentKHR(m_queue, &present);
    }

    if (capturing)
    {
        vkWaitForFences(m_device, 1, &frame.inFlight, VK_TRUE, UINT64_MAX);
        vmaInvalidateAllocation(m_allocator, readback.allocation, 0, VK_WHOLE_SIZE);
        capture->width = static_cast<int>(m_extent.width);
        capture->height = static_cast<int>(m_extent.height);
        capture->rgb.resize(size_t(m_extent.width) * m_extent.height * 3);
        const auto *src = static_cast<const uint8_t *>(readback.mapped);
        const bool bgra = m_swapchainFormat == VK_FORMAT_B8G8R8A8_UNORM || m_swapchainFormat == VK_FORMAT_B8G8R8A8_SRGB;
        for (size_t p = 0; p < size_t(m_extent.width) * m_extent.height; ++p)
        {
            capture->rgb[p * 3 + 0] = src[p * 4 + (bgra ? 2 : 0)];
            capture->rgb[p * 3 + 1] = src[p * 4 + 1];
            capture->rgb[p * 3 + 2] = src[p * 4 + (bgra ? 0 : 2)];
        }
        vmaDestroyBuffer(m_allocator, readback.buffer, readback.allocation);
    }

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
    {
        RecreateSwapchain();
    }
    else if (result != VK_SUCCESS)
    {
        GENI_LOG_ERROR("vkQueuePresentKHR failed: %s", ResultString(result));
    }

    ++m_frameNumber;
}

void VulkanDevice::GetBackbufferSize(uint32_t &width, uint32_t &height) const
{
    width = m_extent.width;
    height = m_extent.height;
}

uint32_t VulkanDevice::GetUniformBufferAlignment() const
{
    return static_cast<uint32_t>(m_properties.limits.minUniformBufferOffsetAlignment);
}

uint64_t VulkanDevice::GetAllocatedBytes() const
{
    if (!m_allocator)
    {
        return 0;
    }
    VkPhysicalDeviceMemoryProperties properties{};
    vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &properties);
    std::vector<VmaBudget> budgets(properties.memoryHeapCount);
    vmaGetHeapBudgets(m_allocator, budgets.data());
    uint64_t total = 0;
    for (const auto &budget : budgets)
    {
        total += budget.statistics.allocationBytes;
    }
    return total;
}

std::string VulkanDevice::GetDescription() const
{
    char text[512];
    std::snprintf(
        text, sizeof(text), "Vulkan %u.%u.%u on %s (backbuffer %ux%u, %zu images, %s)",
        VK_API_VERSION_MAJOR(m_properties.apiVersion), VK_API_VERSION_MINOR(m_properties.apiVersion),
        VK_API_VERSION_PATCH(m_properties.apiVersion), m_properties.deviceName, m_extent.width, m_extent.height,
        m_swapchainImages.size(), m_vsync ? "vsync" : "no vsync"
    );
    return text;
}

// ---------------------------------------------------------------------------------------
// Dear ImGui
// ---------------------------------------------------------------------------------------

#if defined(GENI_ENABLE_IMGUI)

static void CheckImGuiResult(VkResult result)
{
    if (result != VK_SUCCESS)
    {
        GENI_LOG_ERROR("ImGui Vulkan backend: %s", ResultString(result));
    }
}

bool VulkanDevice::InitImGui()
{
    if (m_imguiInitialized)
    {
        return true;
    }
    ImGui_ImplVulkan_InitInfo info{};
    info.ApiVersion = VK_API_VERSION_1_1;
    info.Instance = m_instance;
    info.PhysicalDevice = m_physicalDevice;
    info.Device = m_device;
    info.QueueFamily = m_queueFamily;
    info.Queue = m_queue;
    info.DescriptorPoolSize = 64;
    info.MinImageCount = std::max(2u, m_minImageCount);
    info.ImageCount = std::max(info.MinImageCount, static_cast<uint32_t>(m_swapchainImages.size()));
    info.PipelineInfoMain.RenderPass = GetBackbufferRenderPass(false);
    info.PipelineInfoMain.Subpass = 0;
    info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    info.CheckVkResultFn = CheckImGuiResult;
    m_imguiInitialized = ImGui_ImplVulkan_Init(&info);
    return m_imguiInitialized;
}

void VulkanDevice::ShutdownImGui()
{
    if (!m_imguiInitialized)
    {
        return;
    }
    vkDeviceWaitIdle(m_device);
    ImGui_ImplVulkan_Shutdown();
    m_imguiInitialized = false;
}

void VulkanDevice::NewImGuiFrame()
{
    if (m_imguiInitialized)
    {
        ImGui_ImplVulkan_NewFrame();
    }
}

void VulkanDevice::RenderImGui(CommandList &cmd)
{
    if (!m_imguiInitialized)
    {
        return;
    }
    auto &vulkanCmd = static_cast<VulkanCommandList &>(cmd);
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), vulkanCmd.m_cmd);
    // ImGui bound its own pipeline; later SetBindGroup calls need a SetPipeline first.
    vulkanCmd.m_layout = VK_NULL_HANDLE;
}

#else

bool VulkanDevice::InitImGui()
{
    return false;
}
void VulkanDevice::ShutdownImGui()
{
}
void VulkanDevice::NewImGuiFrame()
{
}
void VulkanDevice::RenderImGui(CommandList &)
{
}

#endif

// ---------------------------------------------------------------------------------------
// Command list
// ---------------------------------------------------------------------------------------

void VulkanCommandList::BeginRenderPass(const RenderPassDesc &desc)
{
    GENI_ASSERT_MSG(!m_inRenderPass, "BeginRenderPass inside a render pass");
    VulkanDevice *device = m_device;
    VulkanDevice::RenderPassKey key;

    VkImageView colorView = VK_NULL_HANDLE;
    VkImageView depthView = VK_NULL_HANDLE;
    VkImageView resolveView = VK_NULL_HANDLE;
    VkExtent2D extent{};

    const bool backbuffer = !desc.colorTarget.IsValid() && !desc.depthTarget.IsValid();
    if (backbuffer)
    {
        const bool first = !m_backbufferWritten;
        key = device->MakeBackbufferKey(desc.backbufferDepth, desc.colorLoad, desc.colorStore);
        // A freshly acquired backbuffer has no contents to load.
        if (first && key.colorLoad == LoadOp::Load)
        {
            key.colorLoad = LoadOp::DontCare;
        }
        key.depthLoad = desc.depthLoad;
        key.depthStore = desc.depthStore;
        key.firstInFrame = first;
        colorView = device->m_swapchainViews[device->m_imageIndex];
        depthView = desc.backbufferDepth ? device->m_depthView : VK_NULL_HANDLE;
        extent = device->m_extent;
        m_backbufferWritten = true;
    }
    else
    {
        TextureRecord *color = desc.colorTarget.IsValid() ? device->m_textures.Get(desc.colorTarget) : nullptr;
        TextureRecord *depth = desc.depthTarget.IsValid() ? device->m_textures.Get(desc.depthTarget) : nullptr;
        TextureRecord *resolve = desc.resolveTarget.IsValid() ? device->m_textures.Get(desc.resolveTarget) : nullptr;
        if (!color && !depth)
        {
            return;
        }

        key.hasColor = color != nullptr;
        key.hasDepth = depth != nullptr;
        key.hasResolve = color && resolve;
        key.colorSamples = color ? color->sampleCount : 1;
        key.depthSamples = depth ? depth->sampleCount : 1;
        key.colorFormat = color ? ToVk(color->format) : VK_FORMAT_UNDEFINED;
        key.depthFormat = depth ? ToVk(depth->format) : VK_FORMAT_UNDEFINED;
        key.resolveFormat = resolve ? ToVk(resolve->format) : VK_FORMAT_UNDEFINED;
        key.colorLoad = desc.colorLoad;
        key.colorStore = desc.colorStore;
        key.depthLoad = desc.depthLoad;
        key.depthStore = desc.depthStore;
        key.colorFinal = !color ? VK_IMAGE_LAYOUT_UNDEFINED
                         : key.hasResolve || !HasFlag(color->usage, TextureUsage::Sampled)
                             ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
                             : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        key.depthFinal = depth && HasFlag(depth->usage, TextureUsage::Sampled)
                             ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                             : VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        key.firstInFrame = false;
        key.backbuffer = false;

        colorView = color ? color->view : VK_NULL_HANDLE;
        depthView = depth ? depth->view : VK_NULL_HANDLE;
        resolveView = resolve ? resolve->view : VK_NULL_HANDLE;
        extent = {color ? color->width : depth->width, color ? color->height : depth->height};
    }

    VkRenderPass renderPass = device->GetRenderPass(key);
    VkFramebuffer framebuffer = device->GetFramebuffer(renderPass, colorView, depthView, resolveView, extent);

    // Clear values are indexed by attachment, and a depth-only pass has the depth
    // attachment first: build the array in attachment order.
    std::vector<VkClearValue> clears;
    if (key.hasColor)
    {
        VkClearValue color{};
        color.color = {{desc.clearColor[0], desc.clearColor[1], desc.clearColor[2], desc.clearColor[3]}};
        clears.push_back(color);
    }
    if (key.hasDepth)
    {
        VkClearValue depth{};
        depth.depthStencil = {desc.clearDepth, 0};
        clears.push_back(depth);
    }
    if (key.hasResolve)
    {
        clears.push_back(VkClearValue{});
    }

    VkRenderPassBeginInfo info{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    info.renderPass = renderPass;
    info.framebuffer = framebuffer;
    info.renderArea = {{0, 0}, extent};
    info.clearValueCount = static_cast<uint32_t>(clears.size());
    info.pClearValues = clears.data();
    vkCmdBeginRenderPass(m_cmd, &info, VK_SUBPASS_CONTENTS_INLINE);
    m_inRenderPass = true;

    SetViewport(0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height));
}

void VulkanCommandList::EndRenderPass()
{
    GENI_ASSERT(m_inRenderPass);
    vkCmdEndRenderPass(m_cmd);
    m_inRenderPass = false;
}

void VulkanCommandList::SetViewport(float x, float y, float width, float height)
{
    // Negative height flips Y so that clip space points up, as in OpenGL and WebGPU.
    VkViewport viewport{x, y + height, width, -height, 0.0f, 1.0f};
    vkCmdSetViewport(m_cmd, 0, 1, &viewport);
    VkRect2D scissor{
        {static_cast<int32_t>(x), static_cast<int32_t>(y)},
        {static_cast<uint32_t>(width), static_cast<uint32_t>(height)}
    };
    vkCmdSetScissor(m_cmd, 0, 1, &scissor);
}

void VulkanCommandList::SetPipeline(PipelineHandle pipeline)
{
    PipelineRecord *record = m_device->m_pipelines.Get(pipeline);
    if (!record)
    {
        return;
    }
    vkCmdBindPipeline(m_cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, record->pipeline);
    m_layout = record->layout;
}

void VulkanCommandList::SetBindGroup(
    uint32_t set, BindGroupHandle group, const uint32_t *dynamicOffsets, uint32_t dynamicOffsetCount
)
{
    GENI_ASSERT_MSG(m_layout != VK_NULL_HANDLE, "SetBindGroup before SetPipeline");
    BindGroupRecord *record = m_device->m_bindGroups.Get(group);
    if (!record || !m_layout)
    {
        return;
    }
    vkCmdBindDescriptorSets(
        m_cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_layout, set, 1, &record->set, dynamicOffsetCount, dynamicOffsets
    );
}

void VulkanCommandList::SetVertexBuffer(uint32_t binding, BufferHandle buffer, uint64_t offset)
{
    BufferRecord *record = m_device->m_buffers.Get(buffer);
    if (!record)
    {
        return;
    }
    VkDeviceSize vkOffset = offset;
    vkCmdBindVertexBuffers(m_cmd, binding, 1, &record->buffer, &vkOffset);
}

void VulkanCommandList::SetIndexBuffer(BufferHandle buffer, IndexFormat format, uint64_t offset)
{
    BufferRecord *record = m_device->m_buffers.Get(buffer);
    if (!record)
    {
        return;
    }
    vkCmdBindIndexBuffer(
        m_cmd, record->buffer, offset, format == IndexFormat::Uint16 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32
    );
}

void VulkanCommandList::Draw(uint32_t vertexCount, uint32_t firstVertex)
{
    vkCmdDraw(m_cmd, vertexCount, 1, firstVertex, 0);
}

void VulkanCommandList::DrawIndexed(uint32_t indexCount, uint32_t firstIndex, int32_t vertexOffset)
{
    vkCmdDrawIndexed(m_cmd, indexCount, 1, firstIndex, vertexOffset, 0);
}

void VulkanCommandList::DrawIndexedInstanced(
    uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance
)
{
    vkCmdDrawIndexed(m_cmd, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
}

void VulkanCommandList::DrawInstanced(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstInstance)
{
    vkCmdDraw(m_cmd, vertexCount, instanceCount, 0, firstInstance);
}

} // namespace vulkan
} // namespace Geni::rhi
