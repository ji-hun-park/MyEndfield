#include "VulkanBackend.h"
#include "RenderGraph.h"
#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <cstdint>
#include <fstream>

static VulkanBackend::DebugLogFunc g_DebugCallback = nullptr;

void VulkanBackend::SetDebugCallback(DebugLogFunc callback) {
    g_DebugCallback = callback;
}

static void LogToUnity(const std::string& message) {
    if (g_DebugCallback) {
        g_DebugCallback(message.c_str());
    } else {
        std::cout << message << std::endl;
    }
}

static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData) 
{
    std::string prefix = "[Vulkan] ";
    if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) prefix = "[Vulkan ERROR] ";
    else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) prefix = "[Vulkan WARNING] ";

    LogToUnity(prefix + pCallbackData->pMessage);
    return VK_FALSE;
}

VulkanBackend::VulkanBackend()
{
}

VulkanBackend::~VulkanBackend()
{
}

void VulkanBackend::Initialize(void* windowHandle)
{
    CreateInstance();
    SetupDebugMessenger();
    CreateSurface(windowHandle);
    SelectPhysicalDevice();
    CreateLogicalDevice();
    CreateCommandObjects();
    CreateSwapchain();
    CreateRenderPass();
    CreateFramebuffers();
    CreateGraphicsPipeline();
    CreateSyncObjects();

    LogToUnity("[VulkanBackend] Successfully Initialized Native Vulkan Backend.");
}

void VulkanBackend::CreateSurface(void* windowHandle)
{
#if defined(_WIN32)
    if (!windowHandle || m_Instance == VK_NULL_HANDLE) return;

    VkWin32SurfaceCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    createInfo.hwnd = (HWND)windowHandle;
    createInfo.hinstance = GetModuleHandle(nullptr);

    if (vkCreateWin32SurfaceKHR(m_Instance, &createInfo, nullptr, &m_Surface) != VK_SUCCESS) {
        LogToUnity("[VulkanBackend ERROR] Failed to create Win32 Surface!");
    } else {
        LogToUnity("[VulkanBackend] Win32 Surface successfully created.");
    }
#endif
}

void VulkanBackend::CreateInstance()
{
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Mini Endfield Native Renderer";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "Endfield Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;

    // Enable Validation Layers
    const char* validationLayers[] = { "VK_LAYER_KHRONOS_validation" };
    createInfo.enabledLayerCount = 1;
    createInfo.ppEnabledLayerNames = validationLayers;

    // Enable Debug Utils Extension
    const char* extensions[] = { VK_EXT_DEBUG_UTILS_EXTENSION_NAME };
    createInfo.enabledExtensionCount = 1;
    createInfo.ppEnabledExtensionNames = extensions;

    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
    debugCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    debugCreateInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    debugCreateInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    debugCreateInfo.pfnUserCallback = DebugCallback;
    
    createInfo.pNext = (VkDebugUtilsMessengerCreateInfoEXT*)&debugCreateInfo;

    if (vkCreateInstance(&createInfo, nullptr, &m_Instance) != VK_SUCCESS) {
        LogToUnity("[VulkanBackend] Failed to create Vulkan instance!");
    }
}

void VulkanBackend::SetupDebugMessenger()
{
    if (m_Instance == VK_NULL_HANDLE) return;

    VkDebugUtilsMessengerCreateInfoEXT createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    createInfo.pfnUserCallback = DebugCallback;

    auto func = (PFN_vkCreateDebugUtilsMessengerEXT) vkGetInstanceProcAddr(m_Instance, "vkCreateDebugUtilsMessengerEXT");
    if (func != nullptr) {
        func(m_Instance, &createInfo, nullptr, &m_DebugMessenger);
        LogToUnity("[VulkanBackend] Debug Messenger successfully created.");
    } else {
        LogToUnity("[VulkanBackend ERROR] Failed to load vkCreateDebugUtilsMessengerEXT function.");
    }
}

int VulkanBackend::ScoreDeviceSuitability(VkPhysicalDevice device)
{
    int score = 0;
    VkPhysicalDeviceProperties deviceProperties;
    vkGetPhysicalDeviceProperties(device, &deviceProperties);
    
    if (deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
        score += 5000;
    }

    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

    bool hasGraphicsQueue = false;
    bool hasPresentQueue = false;

    for (uint32_t i = 0; i < queueFamilyCount; i++) {
        if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            hasGraphicsQueue = true;
        }

        if (m_Surface != VK_NULL_HANDLE) {
            VkBool32 presentSupport = false;
            vkGetPhysicalDeviceSurfaceSupportKHR(device, i, m_Surface, &presentSupport);
            if (presentSupport) {
                hasPresentQueue = true;
            }
        } else {
            hasPresentQueue = true;
        }
    }

    if (hasGraphicsQueue) score += 500;
    if (hasPresentQueue) score += 500;

    uint32_t extensionCount;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);
    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());

    bool swapchainSupported = false;
    for (const auto& extension : availableExtensions) {
        if (std::string(extension.extensionName) == VK_KHR_SWAPCHAIN_EXTENSION_NAME) {
            swapchainSupported = true;
            break;
        }
    }
    
    if (swapchainSupported) {
        score += 500;
    }

    LogToUnity("[VulkanBackend] GPU Found: " + std::string(deviceProperties.deviceName) + " (Score: " + std::to_string(score) + ")");
    return score;
}

void VulkanBackend::SelectPhysicalDevice()
{
    if (m_Instance == VK_NULL_HANDLE) return;

    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(m_Instance, &deviceCount, nullptr);
    if (deviceCount == 0) {
        LogToUnity("[VulkanBackend ERROR] Failed to find GPUs with Vulkan support!");
        return;
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(m_Instance, &deviceCount, devices.data());

    VkPhysicalDevice bestDevice = VK_NULL_HANDLE;
    int highestScore = -1;

    for (const auto& device : devices) {
        int score = ScoreDeviceSuitability(device);
        if (score > highestScore) {
            highestScore = score;
            bestDevice = device;
        }
    }

    if (bestDevice != VK_NULL_HANDLE && highestScore > 0) {
        m_PhysicalDevice = bestDevice;
        
        VkPhysicalDeviceProperties bestProps;
        vkGetPhysicalDeviceProperties(m_PhysicalDevice, &bestProps);
        LogToUnity("[VulkanBackend] Selected Best GPU: " + std::string(bestProps.deviceName) + " (Score: " + std::to_string(highestScore) + ")");
    } else {
        LogToUnity("[VulkanBackend ERROR] Failed to find a suitable GPU!");
    }
}

void VulkanBackend::CreateLogicalDevice()
{
    if (m_PhysicalDevice == VK_NULL_HANDLE) return;

    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(m_PhysicalDevice, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(m_PhysicalDevice, &queueFamilyCount, queueFamilies.data());

    bool foundGraphics = false;
    bool foundPresent = false;

    for (uint32_t i = 0; i < queueFamilyCount; i++) {
        if (!foundGraphics && (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
            m_GraphicsQueueFamilyIndex = i;
            foundGraphics = true;
        }

        if (!foundPresent && m_Surface != VK_NULL_HANDLE) {
            VkBool32 presentSupport = false;
            vkGetPhysicalDeviceSurfaceSupportKHR(m_PhysicalDevice, i, m_Surface, &presentSupport);
            if (presentSupport) {
                m_PresentQueueFamilyIndex = i;
                foundPresent = true;
            }
        }
    }

    if (m_Surface == VK_NULL_HANDLE) {
        // If no surface is provided, fallback to matching graphics queue for present queue
        m_PresentQueueFamilyIndex = m_GraphicsQueueFamilyIndex;
    }

    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    float queuePriority = 1.0f;
    
    // Create unique queue families list
    std::vector<uint32_t> uniqueQueueFamilies = { m_GraphicsQueueFamilyIndex };
    if (m_GraphicsQueueFamilyIndex != m_PresentQueueFamilyIndex) {
        uniqueQueueFamilies.push_back(m_PresentQueueFamilyIndex);
    }

    for (uint32_t queueFamily : uniqueQueueFamilies) {
        VkDeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = queueFamily;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;
        queueCreateInfos.push_back(queueCreateInfo);
    }

    VkDeviceCreateInfo deviceCreateInfo{};
    deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceCreateInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
    deviceCreateInfo.pQueueCreateInfos = queueCreateInfos.data();

    // Enable Device Extensions (e.g., Swapchain)
    const char* deviceExtensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    deviceCreateInfo.enabledExtensionCount = 1;
    deviceCreateInfo.ppEnabledExtensionNames = deviceExtensions;

    if (vkCreateDevice(m_PhysicalDevice, &deviceCreateInfo, nullptr, &m_Device) != VK_SUCCESS) {
        LogToUnity("[VulkanBackend ERROR] Failed to create logical device!");
        return;
    }

    vkGetDeviceQueue(m_Device, m_GraphicsQueueFamilyIndex, 0, &m_GraphicsQueue);
    vkGetDeviceQueue(m_Device, m_PresentQueueFamilyIndex, 0, &m_PresentQueue);
}

void VulkanBackend::CreateCommandObjects()
{
    if (m_Device == VK_NULL_HANDLE || m_PhysicalDevice == VK_NULL_HANDLE) return;

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = m_GraphicsQueueFamilyIndex;

    if (vkCreateCommandPool(m_Device, &poolInfo, nullptr, &m_CommandPool) != VK_SUCCESS) {
        LogToUnity("[VulkanBackend ERROR] Failed to create command pool!");
        return;
    }

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = m_CommandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    if (vkAllocateCommandBuffers(m_Device, &allocInfo, &m_CommandBuffer) != VK_SUCCESS) {
        LogToUnity("[VulkanBackend ERROR] Failed to allocate command buffers!");
    }
}

void VulkanBackend::CreateSwapchain()
{
VkSurfaceFormatKHR VulkanBackend::ChooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats) {
    for (const auto& availableFormat : availableFormats) {
        if (availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB && availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return availableFormat;
        }
    }
    return availableFormats[0];
}

VkPresentModeKHR VulkanBackend::ChooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes) {
    for (const auto& availablePresentMode : availablePresentModes) {
        if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
            return availablePresentMode;
        }
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D VulkanBackend::ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities) {
    if (capabilities.currentExtent.width != UINT32_MAX) {
        return capabilities.currentExtent;
    } else {
        VkExtent2D extent = { 1280, 720 }; 
        extent.width = std::clamp(extent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
        extent.height = std::clamp(extent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
        return extent;
    }
}

void VulkanBackend::CreateSwapchainImageViews() {
    m_SwapchainImageViews.resize(m_SwapchainImages.size());
    for (size_t i = 0; i < m_SwapchainImages.size(); i++) {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = m_SwapchainImages[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = m_SwapchainImageFormat;
        viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(m_Device, &viewInfo, nullptr, &m_SwapchainImageViews[i]) != VK_SUCCESS) {
            LogToUnity("[VulkanBackend ERROR] Failed to create image views!");
        }
    }
}

void VulkanBackend::CreateSwapchain()
{
    if (m_Device == VK_NULL_HANDLE || m_PhysicalDevice == VK_NULL_HANDLE || m_Surface == VK_NULL_HANDLE) {
        return;
    }

    VkSurfaceCapabilitiesKHR capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_PhysicalDevice, m_Surface, &capabilities);

    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_PhysicalDevice, m_Surface, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    if (formatCount != 0) {
        vkGetPhysicalDeviceSurfaceFormatsKHR(m_PhysicalDevice, m_Surface, &formatCount, formats.data());
    }

    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(m_PhysicalDevice, m_Surface, &presentModeCount, nullptr);
    std::vector<VkPresentModeKHR> presentModes(presentModeCount);
    if (presentModeCount != 0) {
        vkGetPhysicalDeviceSurfacePresentModesKHR(m_PhysicalDevice, m_Surface, &presentModeCount, presentModes.data());
    }

    if (formats.empty() || presentModes.empty()) {
        LogToUnity("[VulkanBackend ERROR] Inadequate swapchain support!");
        return;
    }

    VkSurfaceFormatKHR surfaceFormat = ChooseSwapSurfaceFormat(formats);
    VkPresentModeKHR presentMode = ChooseSwapPresentMode(presentModes);
    VkExtent2D extent = ChooseSwapExtent(capabilities);

    uint32_t imageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount) {
        imageCount = capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = m_Surface;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    uint32_t queueFamilyIndices[] = { m_GraphicsQueueFamilyIndex, m_PresentQueueFamilyIndex };
    if (m_GraphicsQueueFamilyIndex != m_PresentQueueFamilyIndex) {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
    } else {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    createInfo.preTransform = capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = VK_NULL_HANDLE;

    if (vkCreateSwapchainKHR(m_Device, &createInfo, nullptr, &m_Swapchain) != VK_SUCCESS) {
        LogToUnity("[VulkanBackend ERROR] Failed to create swapchain!");
        return;
    }

    vkGetSwapchainImagesKHR(m_Device, m_Swapchain, &imageCount, nullptr);
    m_SwapchainImages.resize(imageCount);
    vkGetSwapchainImagesKHR(m_Device, m_Swapchain, &imageCount, m_SwapchainImages.data());

    m_SwapchainImageFormat = surfaceFormat.format;
    m_SwapchainExtent = extent;

    CreateSwapchainImageViews();

    LogToUnity("[VulkanBackend] Swapchain and Image Views successfully created. (Count: " + std::to_string(imageCount) + ")");
}

void VulkanBackend::CreateRenderPass()
{
    if (m_Device == VK_NULL_HANDLE) return;

    // 1. Color Attachment (Swapchain)
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = m_SwapchainImageFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR; // Ready for display

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    // 2. Subpass
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;

    // 3. Subpass Dependency (Synchronization)
    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    // 4. Create Render Pass
    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &colorAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    if (vkCreateRenderPass(m_Device, &renderPassInfo, nullptr, &m_RenderPass) != VK_SUCCESS) {
        LogToUnity("[VulkanBackend ERROR] Failed to create render pass!");
    } else {
        LogToUnity("[VulkanBackend] Render Pass successfully created.");
    }
}

void VulkanBackend::CreateFramebuffers()
{
    if (m_Device == VK_NULL_HANDLE || m_RenderPass == VK_NULL_HANDLE) return;

    m_SwapchainFramebuffers.resize(m_SwapchainImageViews.size());

    for (size_t i = 0; i < m_SwapchainImageViews.size(); i++) {
        VkImageView attachments[] = {
            m_SwapchainImageViews[i] // Connect Swapchain image view to the color attachment
        };

        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = m_RenderPass;
        framebufferInfo.attachmentCount = 1;
        framebufferInfo.pAttachments = attachments;
        framebufferInfo.width = m_SwapchainExtent.width;
        framebufferInfo.height = m_SwapchainExtent.height;
        framebufferInfo.layers = 1;

        if (vkCreateFramebuffer(m_Device, &framebufferInfo, nullptr, &m_SwapchainFramebuffers[i]) != VK_SUCCESS) {
            LogToUnity("[VulkanBackend ERROR] Failed to create framebuffer " + std::to_string(i) + "!");
        }
    }

    LogToUnity("[VulkanBackend] Framebuffers successfully created.");
}

static std::vector<char> ReadFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        LogToUnity("[VulkanBackend ERROR] Failed to open file: " + filename);
        return {};
    }
    size_t fileSize = (size_t) file.tellg();
    std::vector<char> buffer(fileSize);
    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();
    return buffer;
}

VkShaderModule VulkanBackend::CreateShaderModule(const std::vector<char>& code) {
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule shaderModule;
    if (vkCreateShaderModule(m_Device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        LogToUnity("[VulkanBackend ERROR] Failed to create shader module!");
        return VK_NULL_HANDLE;
    }
    return shaderModule;
}

VkPipelineShaderStageCreateInfo VulkanBackend::CreateShaderStageInfo(VkShaderStageFlagBits stage, VkShaderModule module) {
    VkPipelineShaderStageCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    info.stage = stage;
    info.module = module;
    info.pName = "main";
    return info;
}

VkPipelineVertexInputStateCreateInfo VulkanBackend::CreateVertexInputStateInfo() {
    VkPipelineVertexInputStateCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    info.vertexBindingDescriptionCount = 0;
    info.vertexAttributeDescriptionCount = 0;
    return info;
}

VkPipelineInputAssemblyStateCreateInfo VulkanBackend::CreateInputAssemblyStateInfo() {
    VkPipelineInputAssemblyStateCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    info.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    info.primitiveRestartEnable = VK_FALSE;
    return info;
}

VkPipelineRasterizationStateCreateInfo VulkanBackend::CreateRasterizationStateInfo() {
    VkPipelineRasterizationStateCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    info.depthClampEnable = VK_FALSE;
    info.rasterizerDiscardEnable = VK_FALSE;
    info.polygonMode = VK_POLYGON_MODE_FILL;
    info.lineWidth = 1.0f;
    info.cullMode = VK_CULL_MODE_BACK_BIT;
    info.frontFace = VK_FRONT_FACE_CLOCKWISE;
    info.depthBiasEnable = VK_FALSE;
    return info;
}

void VulkanBackend::CreateGraphicsPipeline()
{
    if (m_Device == VK_NULL_HANDLE || m_RenderPass == VK_NULL_HANDLE) return;

    auto vertShaderCode = ReadFile("vert.spv");
    auto fragShaderCode = ReadFile("frag.spv");
    
    if (vertShaderCode.empty() || fragShaderCode.empty()) {
        LogToUnity("[VulkanBackend WARNING] Shaders not found. Skipping pipeline creation.");
        return;
    }

    VkShaderModule vertShaderModule = CreateShaderModule(vertShaderCode);
    VkShaderModule fragShaderModule = CreateShaderModule(fragShaderCode);

    VkPipelineShaderStageCreateInfo shaderStages[] = {
        CreateShaderStageInfo(VK_SHADER_STAGE_VERTEX_BIT, vertShaderModule),
        CreateShaderStageInfo(VK_SHADER_STAGE_FRAGMENT_BIT, fragShaderModule)
    };

    VkPipelineVertexInputStateCreateInfo vertexInputInfo = CreateVertexInputStateInfo();
    VkPipelineInputAssemblyStateCreateInfo inputAssembly = CreateInputAssemblyStateInfo();
    
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer = CreateRasterizationStateInfo();

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    std::vector<VkDynamicState> dynamicStates = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 0;
    pipelineLayoutInfo.pushConstantRangeCount = 0;

    if (vkCreatePipelineLayout(m_Device, &pipelineLayoutInfo, nullptr, &m_PipelineLayout) != VK_SUCCESS) {
        LogToUnity("[VulkanBackend ERROR] Failed to create pipeline layout!");
    }

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = m_PipelineLayout;
    pipelineInfo.renderPass = m_RenderPass;
    pipelineInfo.subpass = 0;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

    if (vkCreateGraphicsPipelines(m_Device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_GraphicsPipeline) != VK_SUCCESS) {
        LogToUnity("[VulkanBackend ERROR] Failed to create graphics pipeline!");
    } else {
        LogToUnity("[VulkanBackend] Graphics Pipeline successfully created.");
    }

    vkDestroyShaderModule(m_Device, fragShaderModule, nullptr);
    vkDestroyShaderModule(m_Device, vertShaderModule, nullptr);
}

void VulkanBackend::CreateSyncObjects()
{
    if (m_Device == VK_NULL_HANDLE) return;

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT; // Initialize as signaled

    if (vkCreateSemaphore(m_Device, &semaphoreInfo, nullptr, &m_ImageAvailableSemaphore) != VK_SUCCESS ||
        vkCreateSemaphore(m_Device, &semaphoreInfo, nullptr, &m_RenderFinishedSemaphore) != VK_SUCCESS ||
        vkCreateFence(m_Device, &fenceInfo, nullptr, &m_InFlightFence) != VK_SUCCESS) {
        LogToUnity("[VulkanBackend ERROR] Failed to create synchronization objects!");
    } else {
        LogToUnity("[VulkanBackend] Synchronization objects created successfully.");
    }
}

void VulkanBackend::Shutdown()
{
    // Wait for the logical device to finish operations before cleaning up
    if (m_Device) {
        vkDeviceWaitIdle(m_Device);
        
        if (m_ImageAvailableSemaphore) {
            vkDestroySemaphore(m_Device, m_ImageAvailableSemaphore, nullptr);
            m_ImageAvailableSemaphore = VK_NULL_HANDLE;
        }
        if (m_RenderFinishedSemaphore) {
            vkDestroySemaphore(m_Device, m_RenderFinishedSemaphore, nullptr);
            m_RenderFinishedSemaphore = VK_NULL_HANDLE;
        }
        if (m_InFlightFence) {
            vkDestroyFence(m_Device, m_InFlightFence, nullptr);
            m_InFlightFence = VK_NULL_HANDLE;
        }
        
        for (auto framebuffer : m_SwapchainFramebuffers) {
            vkDestroyFramebuffer(m_Device, framebuffer, nullptr);
        }
        m_SwapchainFramebuffers.clear();

        if (m_GraphicsPipeline) {
            vkDestroyPipeline(m_Device, m_GraphicsPipeline, nullptr);
            m_GraphicsPipeline = VK_NULL_HANDLE;
        }

        if (m_PipelineLayout) {
            vkDestroyPipelineLayout(m_Device, m_PipelineLayout, nullptr);
            m_PipelineLayout = VK_NULL_HANDLE;
        }

        if (m_RenderPass) {
            vkDestroyRenderPass(m_Device, m_RenderPass, nullptr);
            m_RenderPass = VK_NULL_HANDLE;
        }

        for (auto imageView : m_SwapchainImageViews) {
            vkDestroyImageView(m_Device, imageView, nullptr);
        }
        m_SwapchainImageViews.clear();

        if (m_Swapchain) {
            vkDestroySwapchainKHR(m_Device, m_Swapchain, nullptr);
            m_Swapchain = VK_NULL_HANDLE;
        }

        if (m_CommandPool) {
            vkDestroyCommandPool(m_Device, m_CommandPool, nullptr);
            m_CommandPool = VK_NULL_HANDLE;
        }
        vkDestroyDevice(m_Device, nullptr);
        m_Device = VK_NULL_HANDLE;
    }

    if (m_DebugMessenger) {
        auto func = (PFN_vkDestroyDebugUtilsMessengerEXT) vkGetInstanceProcAddr(m_Instance, "vkDestroyDebugUtilsMessengerEXT");
        if (func != nullptr) {
            func(m_Instance, m_DebugMessenger, nullptr);
        }
        m_DebugMessenger = VK_NULL_HANDLE;
    }

    if (m_Surface) {
        vkDestroySurfaceKHR(m_Instance, m_Surface, nullptr);
        m_Surface = VK_NULL_HANDLE;
    }

    if (m_Instance) {
        vkDestroyInstance(m_Instance, nullptr);
        m_Instance = VK_NULL_HANDLE;
    }

    LogToUnity("[VulkanBackend] Shut down native Vulkan backend.");
}

void VulkanBackend::SetupRenderGraph()
{
    // 1. Declare resources
    m_RenderGraph.AddResource("GBufferColor", true, AccessTag::None);
    m_RenderGraph.AddResource("GBufferDepth", true, AccessTag::None);
    m_RenderGraph.AddResource("ShadowMap", false, AccessTag::None);

    // 2. Declare passes and their resource accesses
    m_RenderGraph.AddPass("ShadowPass");
    m_RenderGraph.DeclarePassAccess("ShadowPass", "ShadowMap", AccessTag::DepthStencilWrite);

    m_RenderGraph.AddPass("OpaquePass");
    m_RenderGraph.DeclarePassAccess("OpaquePass", "GBufferColor", AccessTag::ColorAttachmentWrite);
    m_RenderGraph.DeclarePassAccess("OpaquePass", "GBufferDepth", AccessTag::DepthStencilWrite);
    m_RenderGraph.DeclarePassAccess("OpaquePass", "ShadowMap", AccessTag::ShaderRead); // Needs transition!

    m_RenderGraph.AddPass("LightingPass");
    m_RenderGraph.DeclarePassAccess("LightingPass", "GBufferColor", AccessTag::ShaderRead); // Needs transition!
    m_RenderGraph.DeclarePassAccess("LightingPass", "GBufferDepth", AccessTag::ShaderRead); // Needs transition!

    // 3. Compile the graph to merge barriers
    m_RenderGraph.CompileGraph();

    // The merged barriers would now be used to generate explicit vkCmdPipelineBarrier calls
    // exactly at the boundaries between passes.
    
    std::cout << "[VulkanBackend] Render Graph setup complete. Barriers merged at compile time.\n";
}

void VulkanBackend::BeginFrame()
{
    if (m_Device == VK_NULL_HANDLE || m_CommandBuffer == VK_NULL_HANDLE || m_Swapchain == VK_NULL_HANDLE) return;

    // Wait for the previous frame to finish
    vkWaitForFences(m_Device, 1, &m_InFlightFence, VK_TRUE, UINT64_MAX);
    vkResetFences(m_Device, 1, &m_InFlightFence);

    // Acquire next image from swapchain
    vkAcquireNextImageKHR(m_Device, m_Swapchain, UINT64_MAX, m_ImageAvailableSemaphore, VK_NULL_HANDLE, &m_CurrentImageIndex);

    // Reset and begin command buffer
    vkResetCommandBuffer(m_CommandBuffer, 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    if (vkBeginCommandBuffer(m_CommandBuffer, &beginInfo) != VK_SUCCESS) {
        LogToUnity("[VulkanBackend ERROR] Failed to begin recording command buffer!");
        return;
    }
    
    // Begin Render Pass
    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = m_RenderPass;
    renderPassInfo.framebuffer = m_SwapchainFramebuffers[m_CurrentImageIndex];
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = m_SwapchainExtent;

    VkClearValue clearColor = {{{0.1f, 0.1f, 0.15f, 1.0f}}}; // Dark blue-ish gray
    renderPassInfo.clearValueCount = 1;
    renderPassInfo.pClearValues = &clearColor;

    vkCmdBeginRenderPass(m_CommandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    // Bind Graphics Pipeline
    if (m_GraphicsPipeline != VK_NULL_HANDLE) {
        vkCmdBindPipeline(m_CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_GraphicsPipeline);
    }

    // Set Dynamic States (Viewport & Scissor)
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float) m_SwapchainExtent.width;
    viewport.height = (float) m_SwapchainExtent.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(m_CommandBuffer, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = m_SwapchainExtent;
    vkCmdSetScissor(m_CommandBuffer, 0, 1, &scissor);

    // Test Draw (Draw a single hardcoded triangle without vertex buffers)
    // The user requested to call vkCmdDraw right after telling the resolution
    vkCmdDraw(m_CommandBuffer, 3, 1, 0, 0);

    // Reset our redundant binding tracker for the new frame (for SubmitBatch)
    m_LastBoundMaterialSet = 0xFFFFFFFF;
}

void VulkanBackend::SubmitBatch(const void* batchData, int instanceCount)
{
    // ... (Keep existing implementation or we can just leave it as is. 
    // The instructions say "BeginFrame() 직후... vkCmdDraw를 호출" so I added it to BeginFrame. 
    // SubmitBatch can stay for when we use actual mesh instances later).
    if (instanceCount == 0 || !batchData || m_CommandBuffer == VK_NULL_HANDLE) return;

    const InstanceData* instances = static_cast<const InstanceData*>(batchData);

    // Endfield Architecture: Descriptor sets are separated by frequency of update
    // Set 0: Per Pass (Lighting, Camera, Shadows)
    // Set 1: Per Material (Textures, Constants)
    // Set 2: Per Draw (Object matrices via Dynamic Offset)

    // E.g., Bind Set 0 (Per Pass) once per batch/pass
    if (m_PipelineLayout != VK_NULL_HANDLE && m_DescriptorSet0_Pass != VK_NULL_HANDLE) {
        vkCmdBindDescriptorSets(m_CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, 
                                m_PipelineLayout, 0, 1, &m_DescriptorSet0_Pass, 0, nullptr);
    }

    for (int i = 0; i < instanceCount; ++i)
    {
        const InstanceData& data = instances[i];
        
        // Decode the 64-bit sort key to get material ID
        // Format [63:48 Pass] [47:32 Pipeline] [31:16 Material] [15:0 Depth]
        uint32_t materialID = (data.sortKey >> 16) & 0xFFFF;
        
        // Redundant binding optimization (Placeholder value 0x7F7F7F7F logic)
        // If the parallel worker encountered this same material earlier, we don't bind again.
        if (materialID != m_LastBoundMaterialSet && materialID != 0x7F7F7F7F)
        {
            // Bind Set 1 (Material)
            if (m_PipelineLayout != VK_NULL_HANDLE && materialID < m_MaterialSets.size()) {
                VkDescriptorSet materialSet = m_MaterialSets[materialID];
                vkCmdBindDescriptorSets(m_CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, 
                                        m_PipelineLayout, 1, 1, &materialSet, 0, nullptr);
            }
            m_LastBoundMaterialSet = materialID;
        }

        // Set 2 (Object Data): Dynamic Offset based on instance index
        uint32_t dynamicOffset = i * sizeof(InstanceData);
        
        if (m_PipelineLayout != VK_NULL_HANDLE && m_DescriptorSet2_Object != VK_NULL_HANDLE) {
            vkCmdBindDescriptorSets(m_CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, 
                                    m_PipelineLayout, 2, 1, &m_DescriptorSet2_Object, 1, &dynamicOffset);
        }

        // Execute Draw Call
        // Assuming indexCount is managed externally or stored in the material/mesh data.
        // For demonstration, we use a mock indexCount of 36 (e.g., a cube).
        uint32_t indexCount = 36;
        vkCmdDrawIndexed(m_CommandBuffer, indexCount, 1, 0, 0, 0);
    }
}

void VulkanBackend::EndFrame()
{
    if (m_CommandBuffer == VK_NULL_HANDLE || m_Device == VK_NULL_HANDLE) return;

    // End Render Pass
    vkCmdEndRenderPass(m_CommandBuffer);

    if (vkEndCommandBuffer(m_CommandBuffer) != VK_SUCCESS) {
        LogToUnity("[VulkanBackend ERROR] Failed to record command buffer!");
        return;
    }
    
    // Submit command buffer
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    VkSemaphore waitSemaphores[] = {m_ImageAvailableSemaphore};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;

    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &m_CommandBuffer;
    
    VkSemaphore signalSemaphores[] = {m_RenderFinishedSemaphore};
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    if (vkQueueSubmit(m_GraphicsQueue, 1, &submitInfo, m_InFlightFence) != VK_SUCCESS) {
        LogToUnity("[VulkanBackend ERROR] Failed to submit draw command buffer!");
        return;
    }
    
    // Present
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;

    VkSwapchainKHR swapchains[] = {m_Swapchain};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapchains;
    presentInfo.pImageIndices = &m_CurrentImageIndex;
    presentInfo.pResults = nullptr;

    vkQueuePresentKHR(m_PresentQueue, &presentInfo);
}
