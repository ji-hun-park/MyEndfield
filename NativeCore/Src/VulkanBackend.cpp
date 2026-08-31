#include "VulkanBackend.h"
#include "RenderGraph.h"
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <set>
#include <string>
#include <vector>
#include <thread>

namespace Endfield {

static VulkanBackend::DebugLogFunc g_DebugCallback = nullptr;

void VulkanBackend::SetDebugCallback(DebugLogFunc callback) {
    g_DebugCallback = callback;
}

void VulkanBackend::LogToUnity(const std::string& message) {
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

    VulkanBackend::LogToUnity(prefix + pCallbackData->pMessage);
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
    CreateDepthResources();
    CreateRenderPass();
    CreateFramebuffers();
    CreateDescriptorResources();
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

    // Depth Attachment
    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = m_DepthFormat;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthAttachmentRef{};
    depthAttachmentRef.attachment = 1;
    depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    // 2. Subpass
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;
    subpass.pDepthStencilAttachment = &depthAttachmentRef;

    // 3. Subpass Dependency (Synchronization)
    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    std::vector<VkAttachmentDescription> attachments = {colorAttachment, depthAttachment};

    // 4. Create Render Pass
    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassInfo.pAttachments = attachments.data();
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
        std::vector<VkImageView> attachments = {
            m_SwapchainImageViews[i],
            m_DepthImageView // Connect Depth image view
        };

        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = m_RenderPass;
        framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        framebufferInfo.pAttachments = attachments.data();
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
        VulkanBackend::LogToUnity("[VulkanBackend ERROR] Failed to open file: " + filename);
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

    VkVertexInputBindingDescription bindingDescription{};
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(Vertex);
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::vector<VkVertexInputAttributeDescription> attributeDescriptions(3);
    attributeDescriptions[0].binding = 0;
    attributeDescriptions[0].location = 0;
    attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[0].offset = offsetof(Vertex, posX);

    attributeDescriptions[1].binding = 0;
    attributeDescriptions[1].location = 1;
    attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[1].offset = offsetof(Vertex, normX);

    attributeDescriptions[2].binding = 0;
    attributeDescriptions[2].location = 2;
    attributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
    attributeDescriptions[2].offset = offsetof(Vertex, uvX);

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

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

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(InstanceData);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &m_DescriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(m_Device, &pipelineLayoutInfo, nullptr, &m_PipelineLayout) != VK_SUCCESS) {
        LogToUnity("[VulkanBackend ERROR] Failed to create pipeline layout!");
    }

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
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

void VulkanBackend::CreateDescriptorResources()
{
    // Descriptor Set Layout
    VkDescriptorSetLayoutBinding uboLayoutBinding{};
    uboLayoutBinding.binding = 0;
    uboLayoutBinding.descriptorCount = 1;
    uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboLayoutBinding.pImmutableSamplers = nullptr;
    uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &uboLayoutBinding;

    if (vkCreateDescriptorSetLayout(m_Device, &layoutInfo, nullptr, &m_DescriptorSetLayout) != VK_SUCCESS) {
        LogToUnity("[VulkanBackend ERROR] Failed to create descriptor set layout!");
    }

    // Uniform Buffer
    CreateBuffer(sizeof(CameraUBO), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, m_CameraUniformBuffer, m_CameraUniformBufferMemory);
    vkMapMemory(m_Device, m_CameraUniformBufferMemory, 0, sizeof(CameraUBO), 0, &m_CameraUniformBufferMapped);

    // Descriptor Pool
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSize.descriptorCount = 1;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = 1;

    if (vkCreateDescriptorPool(m_Device, &poolInfo, nullptr, &m_DescriptorPool) != VK_SUCCESS) {
        LogToUnity("[VulkanBackend ERROR] Failed to create descriptor pool!");
    }

    // Descriptor Sets
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_DescriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_DescriptorSetLayout;

    if (vkAllocateDescriptorSets(m_Device, &allocInfo, &m_DescriptorSet0_Pass) != VK_SUCCESS) {
        LogToUnity("[VulkanBackend ERROR] Failed to allocate descriptor sets!");
    }

    VkDescriptorBufferInfo bufferInfoDesc{};
    bufferInfoDesc.buffer = m_CameraUniformBuffer;
    bufferInfoDesc.offset = 0;
    bufferInfoDesc.range = sizeof(CameraUBO);

    VkWriteDescriptorSet descriptorWrite{};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = m_DescriptorSet0_Pass;
    descriptorWrite.dstBinding = 0;
    descriptorWrite.dstArrayElement = 0;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pBufferInfo = &bufferInfoDesc;

    vkUpdateDescriptorSets(m_Device, 1, &descriptorWrite, 0, nullptr);
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
        
        // 13. Sync Objects
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

        // 12. Graphics Pipeline
        if (m_GraphicsPipeline) {
            vkDestroyPipeline(m_Device, m_GraphicsPipeline, nullptr);
            m_GraphicsPipeline = VK_NULL_HANDLE;
        }
        if (m_PipelineLayout) {
            vkDestroyPipelineLayout(m_Device, m_PipelineLayout, nullptr);
            m_PipelineLayout = VK_NULL_HANDLE;
        }

        // 11. Descriptor Resources
        if (m_DescriptorPool) {
            vkDestroyDescriptorPool(m_Device, m_DescriptorPool, nullptr);
            m_DescriptorPool = VK_NULL_HANDLE;
        }
        if (m_CameraUniformBuffer) {
            vkDestroyBuffer(m_Device, m_CameraUniformBuffer, nullptr);
            m_CameraUniformBuffer = VK_NULL_HANDLE;
        }
        if (m_CameraUniformBufferMemory) {
            vkFreeMemory(m_Device, m_CameraUniformBufferMemory, nullptr);
            m_CameraUniformBufferMemory = VK_NULL_HANDLE;
        }
        if (m_DescriptorSetLayout) {
            vkDestroyDescriptorSetLayout(m_Device, m_DescriptorSetLayout, nullptr);
            m_DescriptorSetLayout = VK_NULL_HANDLE;
        }
        
        // Runtime resources (Meshes)
        for (auto& mesh : m_Meshes) {
            if (mesh.vertexBuffer) {
                vkDestroyBuffer(m_Device, mesh.vertexBuffer, nullptr);
                vkFreeMemory(m_Device, mesh.vertexBufferMemory, nullptr);
            }
            if (mesh.indexBuffer) {
                vkDestroyBuffer(m_Device, mesh.indexBuffer, nullptr);
                vkFreeMemory(m_Device, mesh.indexBufferMemory, nullptr);
            }
        }
        m_Meshes.clear();

        // 10. Framebuffers
        for (auto framebuffer : m_SwapchainFramebuffers) {
            vkDestroyFramebuffer(m_Device, framebuffer, nullptr);
        }
        m_SwapchainFramebuffers.clear();

        // 9. Render Pass
        if (m_RenderPass) {
            vkDestroyRenderPass(m_Device, m_RenderPass, nullptr);
            m_RenderPass = VK_NULL_HANDLE;
        }

        // 8. Depth Resources
        if (m_DepthImageView) {
            vkDestroyImageView(m_Device, m_DepthImageView, nullptr);
            m_DepthImageView = VK_NULL_HANDLE;
        }
        if (m_DepthImage) {
            vkDestroyImage(m_Device, m_DepthImage, nullptr);
            m_DepthImage = VK_NULL_HANDLE;
        }
        if (m_DepthImageMemory) {
            vkFreeMemory(m_Device, m_DepthImageMemory, nullptr);
            m_DepthImageMemory = VK_NULL_HANDLE;
        }

        // 7. Swapchain
        for (auto imageView : m_SwapchainImageViews) {
            vkDestroyImageView(m_Device, imageView, nullptr);
        }
        m_SwapchainImageViews.clear();

        if (m_Swapchain) {
            vkDestroySwapchainKHR(m_Device, m_Swapchain, nullptr);
            m_Swapchain = VK_NULL_HANDLE;
        }

        // 6. Command Objects
        if (m_CommandPool) {
            vkDestroyCommandPool(m_Device, m_CommandPool, nullptr);
            m_CommandPool = VK_NULL_HANDLE;
        }
        
        // 5. Logical Device
        vkDestroyDevice(m_Device, nullptr);
        m_Device = VK_NULL_HANDLE;
    }

    // 4. Physical Device (No explicit destruction needed)

    // 3. Surface
    if (m_Surface) {
        vkDestroySurfaceKHR(m_Instance, m_Surface, nullptr);
        m_Surface = VK_NULL_HANDLE;
    }

    // 2. Debug Messenger
    if (m_DebugMessenger) {
        auto func = (PFN_vkDestroyDebugUtilsMessengerEXT) vkGetInstanceProcAddr(m_Instance, "vkDestroyDebugUtilsMessengerEXT");
        if (func != nullptr) {
            func(m_Instance, m_DebugMessenger, nullptr);
        }
        m_DebugMessenger = VK_NULL_HANDLE;
    }

    // 1. Instance
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

void VulkanBackend::UpdateCamera(float* viewMatrix, float* projMatrix)
{
    if (m_CameraUniformBufferMapped) {
        CameraUBO ubo{};
        memcpy(ubo.view, viewMatrix, sizeof(float) * 16);
        memcpy(ubo.proj, projMatrix, sizeof(float) * 16);
        memcpy(m_CameraUniformBufferMapped, &ubo, sizeof(CameraUBO));
    }
}

void VulkanBackend::CleanupSwapchain() {
    for (auto framebuffer : m_SwapchainFramebuffers) {
        vkDestroyFramebuffer(m_Device, framebuffer, nullptr);
    }
    m_SwapchainFramebuffers.clear();
    if (m_DepthImageView != VK_NULL_HANDLE) { vkDestroyImageView(m_Device, m_DepthImageView, nullptr); m_DepthImageView = VK_NULL_HANDLE; }
    if (m_DepthImage != VK_NULL_HANDLE) { vkDestroyImage(m_Device, m_DepthImage, nullptr); m_DepthImage = VK_NULL_HANDLE; }
    if (m_DepthImageMemory != VK_NULL_HANDLE) { vkFreeMemory(m_Device, m_DepthImageMemory, nullptr); m_DepthImageMemory = VK_NULL_HANDLE; }
    for (auto imageView : m_SwapchainImageViews) {
        vkDestroyImageView(m_Device, imageView, nullptr);
    }
    m_SwapchainImageViews.clear();
    if (m_Swapchain != VK_NULL_HANDLE) { vkDestroySwapchainKHR(m_Device, m_Swapchain, nullptr); m_Swapchain = VK_NULL_HANDLE; }
}

void VulkanBackend::RecreateSwapchain() {
    VkSurfaceCapabilitiesKHR capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_PhysicalDevice, m_Surface, &capabilities);
    if (capabilities.currentExtent.width == 0 || capabilities.currentExtent.height == 0) return;
    
    LogToUnity("[VulkanBackend] Recreating Swapchain...");
    vkDeviceWaitIdle(m_Device);
    CleanupSwapchain();
    CreateSwapchain();
    CreateDepthResources();
    CreateFramebuffers();
}

bool VulkanBackend::BeginFrame()
{
    if (m_Device == VK_NULL_HANDLE || m_CommandBuffer == VK_NULL_HANDLE || m_Swapchain == VK_NULL_HANDLE) return false;

    // Wait for the previous frame to finish
    vkWaitForFences(m_Device, 1, &m_InFlightFence, VK_TRUE, UINT64_MAX);
    vkResetFences(m_Device, 1, &m_InFlightFence);

    // Acquire next image from swapchain
    VkResult acquireResult = vkAcquireNextImageKHR(m_Device, m_Swapchain, UINT64_MAX, m_ImageAvailableSemaphore, VK_NULL_HANDLE, &m_CurrentImageIndex);
    if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR) {
        RecreateSwapchain();
        return false;
    }

    // Reset and begin command buffer
    vkResetCommandBuffer(m_CommandBuffer, 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    if (vkBeginCommandBuffer(m_CommandBuffer, &beginInfo) != VK_SUCCESS) {
        LogToUnity("[VulkanBackend ERROR] Failed to begin recording command buffer!");
        return false;
    }
    
    // Begin Render Pass
    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = m_RenderPass;
    renderPassInfo.framebuffer = m_SwapchainFramebuffers[m_CurrentImageIndex];
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = m_SwapchainExtent;

    VkClearValue clearValues[2]{};
    clearValues[0].color = {{0.1f, 0.1f, 0.15f, 1.0f}};
    clearValues[1].depthStencil = {1.0f, 0};

    renderPassInfo.clearValueCount = 2;
    renderPassInfo.pClearValues = clearValues;

    vkCmdBeginRenderPass(m_CommandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    // Bind Graphics Pipeline
    if (m_GraphicsPipeline != VK_NULL_HANDLE) {
        vkCmdBindPipeline(m_CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_GraphicsPipeline);
        if (m_DescriptorSet0_Pass != VK_NULL_HANDLE && m_PipelineLayout != VK_NULL_HANDLE) {
            vkCmdBindDescriptorSets(m_CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_PipelineLayout, 0, 1, &m_DescriptorSet0_Pass, 0, nullptr);
        }
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

        // Test Draw is removed. Actual drawing will happen in SubmitBatch.

    // Reset our redundant binding tracker for the new frame (for SubmitBatch)
    m_LastBoundMaterialSet = 0xFFFFFFFF;
    return true;
}

void VulkanBackend::SubmitBatch(const void* batchData, int instanceCount)
{
    // ... (Keep existing implementation or we can just leave it as is. 
    // The instructions say "BeginFrame() 직후... vkCmdDraw를 호출" so I added it to BeginFrame. 
    // SubmitBatch can stay for when we use actual mesh instances later).
    if (instanceCount == 0 || !batchData || m_CommandBuffer == VK_NULL_HANDLE) return;

    const InstanceData* instances = static_cast<const InstanceData*>(batchData);

    // --- [-1] 소프트웨어 오클루전 컬링 (Software Occlusion Culling) ---
    // 컬링을 가장 먼저 수행하여 화면에 보이지 않는 오브젝트를 제거합니다.
    
    // 임시로 오클루더 메쉬와 뷰프로젝션 행렬을 넘겨주는 형태 (실제 데이터는 외부에서 주입 필요)
    std::vector<OccluderMesh> dummyOccluders;
    m_CullingSystem.BatchOccluders(dummyOccluders, instances[0].mvpMatrix, m_SwapchainExtent.width, m_SwapchainExtent.height);
    m_CullingSystem.RasterizeTilesParallel(m_SwapchainExtent.width, m_SwapchainExtent.height);
     
    std::vector<bool> visibilityResults;
    AABB defaultLocalBounds = { {-1, -1, -1}, {1, 1, 1} };
    m_CullingSystem.PerformOcclusionTestParallel(
        instances[0].mvpMatrix, instanceCount, sizeof(InstanceData), 
        defaultLocalBounds, m_SwapchainExtent.width, m_SwapchainExtent.height, visibilityResults
    );

    // --- [0] 64비트 정렬 키 기반의 오브젝트 정렬 (Sorting) ---
    // Endfield 문서: "정렬 비교는 16바이트짜리 값에 대한 분기 비교이며, 표준 정렬(std::sort)을 그대로 사용합니다."
    m_SortedInstances.clear();
    m_SortedInstances.reserve(instanceCount);
    for (int i = 0; i < instanceCount; ++i) {
        // Occlusion Culling을 통과한(보이는) 인스턴스만 렌더 리스트에 등록
        if (i < visibilityResults.size() && visibilityResults[i]) {
            m_SortedInstances.push_back(instances[i]);
        }
    }
    
    std::sort(m_SortedInstances.begin(), m_SortedInstances.end(), [](const InstanceData& a, const InstanceData& b) {
        return a.sortKey < b.sortKey; // SortKey.h에 정의된 64비트 uint64_t(value) 기반 operator< 비교
    });
    
    // 이후 로직은 정렬된 배열을 기반으로 진행합니다.
    const InstanceData* sortedInstances = m_SortedInstances.data();

    // Endfield Architecture: Descriptor sets are separated by frequency of update
    // Set 0: Per Pass (Lighting, Camera, Shadows)
    // Set 1: Per Material (Textures, Constants)
    // Set 2: Per Draw (Object matrices via Dynamic Offset)

    // E.g., Bind Set 0 (Per Pass) once per batch/pass
    if (m_PipelineLayout != VK_NULL_HANDLE && m_DescriptorSet0_Pass != VK_NULL_HANDLE) {
        vkCmdBindDescriptorSets(m_CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, 
                                m_PipelineLayout, 0, 1, &m_DescriptorSet0_Pass, 0, nullptr);
    }

    // --- [1] 워커 스레드의 병렬 커맨드 빌드 단계 (Simulated) ---
    // 워커는 다른 워커가 어떤 머티리얼을 바인딩했는지 알 수 없으므로,
    // 일단 0x7F7F7F7F라는 플레이스홀더를 사용하여 디스크립터 바인딩 예약을 생성합니다.
    
    struct IntermediateDrawCmd {
        uint32_t descriptorSetPlaceholder; // 0x7F7F7F7F (더미 마커)
        uint32_t materialID;
        uint32_t meshId;
        uint32_t subMeshIdx;
        InstanceData data;
    };

    std::vector<IntermediateDrawCmd> intermediateCmds(instanceCount); // reserve 대신 미리 할당
    const uint32_t PLACEHOLDER_BINDING = 0x7F7F7F7F;

    unsigned int numThreads = std::thread::hardware_concurrency();
    if (numThreads == 0) numThreads = 4;
    if (numThreads > static_cast<unsigned int>(instanceCount)) {
        numThreads = static_cast<unsigned int>(instanceCount);
    }

    std::vector<std::thread> workers;
    int chunkSize = instanceCount / numThreads;

    for (unsigned int t = 0; t < numThreads; ++t) {
        int startIdx = t * chunkSize;
        int endIdx = (t == numThreads - 1) ? instanceCount : startIdx + chunkSize;

        workers.emplace_back([startIdx, endIdx, sortedInstances, &intermediateCmds, PLACEHOLDER_BINDING]() {
            for (int i = startIdx; i < endIdx; ++i) {
                const InstanceData& data = sortedInstances[i];
                IntermediateDrawCmd cmd;
                
                // 앞뒤 문맥(Context)을 모르는 워커 스레드의 행동: 무조건 플레이스홀더 기록
                cmd.descriptorSetPlaceholder = PLACEHOLDER_BINDING;
                cmd.materialID = data.sortKey.materialID;
                cmd.meshId = data.sortKey.pipelineID;
                cmd.subMeshIdx = data.subMeshIndex;
                cmd.data = data;
                
                // 락(Lock) 없이 각자 독립된 인덱스 공간에 기록
                intermediateCmds[i] = cmd;
            }
        });
    }

    // 워커 스레드가 커맨드 빌드를 모두 마칠 때까지 대기 (조인)
    for (auto& worker : workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }

    // --- [2] 최종 정리(Finalize) 단계 ---
    // 정렬(Sort)이 완료된 상태에서 순차적으로 커맨드를 순회하며 중복 바인딩을 제거(Skip)합니다.

    int instanceIndex = 0;
    for (const auto& cmd : intermediateCmds)
    {
        // 플레이스홀더 확인 및 지연 평가(Lazy Evaluation)
        if (cmd.descriptorSetPlaceholder == PLACEHOLDER_BINDING)
        {
            if (cmd.materialID != m_LastBoundMaterialSet) {
                // 이전 드로우 콜과 머티리얼이 다름 -> 중복 아님. 실제 바인딩 수행!
                
                m_LastBoundMaterialSet = cmd.materialID;
                
                // 실제 Set 1(머티리얼) 바인딩 수행 (m_MaterialSets가 세팅되어 있다고 가정)
                if (cmd.materialID < m_MaterialSets.size() && m_MaterialSets[cmd.materialID] != VK_NULL_HANDLE) {
                    vkCmdBindDescriptorSets(m_CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, 
                                            m_PipelineLayout, 1, 1, &m_MaterialSets[cmd.materialID], 0, nullptr);
                }
            } else {
                // 이전 드로우 콜과 머티리얼이 같음 -> 중복(Redundant) 바인딩!
                // 플레이스홀더 상태 그대로 스킵(Skip)하여 렌더링 스레드 및 GPU 리소스를 절약합니다.
            }
        }
        
        // Set 2: 드로우마다 바뀌는 오브젝트 데이터 (Dynamic Offset 적용)
        // Endfield 문서: "오브젝트별로 별도 셋을 만들지 않고, 전체를 위한 큰 버퍼 하나를 
        // 하나의 셋으로 만들어두고, 각 드로우는 그 버퍼 안에서 자신의 슬라이스로 향하는 다이나믹 오프셋만 이동시킵니다."
        if (m_PipelineLayout != VK_NULL_HANDLE && m_DescriptorSet2_Object != VK_NULL_HANDLE) {
            // 참고: 실제 Vulkan 구현에서는 디바이스의 minUniformBufferOffsetAlignment 값에 맞춰 
            // 오프셋 보정(Alignment)이 필요하지만, 여기서는 구조적 이해를 위해 기본 크기 단위 오프셋을 보여줍니다.
            uint32_t dynamicOffset = static_cast<uint32_t>(instanceIndex * sizeof(InstanceData));
            
            vkCmdBindDescriptorSets(
                m_CommandBuffer, 
                VK_PIPELINE_BIND_POINT_GRAPHICS, 
                m_PipelineLayout, 
                2, // Set 2: Object Data
                1, 
                &m_DescriptorSet2_Object, 
                1, 
                &dynamicOffset
            );
        }

        if (cmd.meshId < m_Meshes.size()) {
            MeshBuffer& mesh = m_Meshes[cmd.meshId];
            if (mesh.vertexBuffer && mesh.indexBuffer && cmd.subMeshIdx < mesh.subMeshes.size()) {
                VkBuffer vertexBuffers[] = {mesh.vertexBuffer};
                VkDeviceSize offsets[] = {0};
                vkCmdBindVertexBuffers(m_CommandBuffer, 0, 1, vertexBuffers, offsets);
                vkCmdBindIndexBuffer(m_CommandBuffer, mesh.indexBuffer, 0, VK_INDEX_TYPE_UINT32);

                const SubMeshBuffer& subMesh = mesh.subMeshes[cmd.subMeshIdx];
                vkCmdDrawIndexed(m_CommandBuffer, subMesh.indexCount, 1, subMesh.firstIndex, 0, 0);
            }
        }
        
        instanceIndex++;
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

VkFormat VulkanBackend::FindSupportedFormat(const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features) {
    for (VkFormat format : candidates) {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(m_PhysicalDevice, format, &props);

        if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features) {
            return format;
        } else if (tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & features) == features) {
            return format;
        }
    }
    LogToUnity("[VulkanBackend ERROR] Failed to find supported format!");
    return VK_FORMAT_D32_SFLOAT;
}

VkFormat VulkanBackend::FindDepthFormat() {
    return FindSupportedFormat(
        {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT},
        VK_IMAGE_TILING_OPTIMAL,
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT
    );
}

uint32_t VulkanBackend::FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(m_PhysicalDevice, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    LogToUnity("[VulkanBackend ERROR] Failed to find suitable memory type!");
    return 0;
}

void VulkanBackend::CreateDepthResources() {
    VkFormat depthFormat = FindDepthFormat();
    m_DepthFormat = depthFormat;

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = m_SwapchainExtent.width;
    imageInfo.extent.height = m_SwapchainExtent.height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = depthFormat;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(m_Device, &imageInfo, nullptr, &m_DepthImage) != VK_SUCCESS) {
        LogToUnity("[VulkanBackend ERROR] Failed to create depth image!");
        return;
    }

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(m_Device, m_DepthImage, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(m_Device, &allocInfo, nullptr, &m_DepthImageMemory) != VK_SUCCESS) {
        LogToUnity("[VulkanBackend ERROR] Failed to allocate depth image memory!");
        return;
    }

    vkBindImageMemory(m_Device, m_DepthImage, m_DepthImageMemory, 0);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_DepthImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = depthFormat;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(m_Device, &viewInfo, nullptr, &m_DepthImageView) != VK_SUCCESS) {
        LogToUnity("[VulkanBackend ERROR] Failed to create depth image view!");
    }
}

} // namespace Endfield

// ----- Buffer Helpers & Mesh Uploading -----
namespace Endfield {

void VulkanBackend::CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory) {
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(m_Device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
        LogToUnity("[VulkanBackend ERROR] Failed to create buffer!");
    }

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(m_Device, buffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits, properties);

    if (vkAllocateMemory(m_Device, &allocInfo, nullptr, &bufferMemory) != VK_SUCCESS) {
        LogToUnity("[VulkanBackend ERROR] Failed to allocate buffer memory!");
    }

    vkBindBufferMemory(m_Device, buffer, bufferMemory, 0);
}

void VulkanBackend::CopyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size) {
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = m_CommandPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(m_Device, &allocInfo, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    VkBufferCopy copyRegion{};
    copyRegion.size = size;
    vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);

    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vkQueueSubmit(m_GraphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(m_GraphicsQueue);

    vkFreeCommandBuffers(m_Device, m_CommandPool, 1, &commandBuffer);
}

void VulkanBackend::UploadMesh(const std::vector<Vertex>& vertices, const std::vector<std::vector<int32_t>>& subMeshIndices, uint32_t meshId) {
    if (meshId >= m_Meshes.size()) {
        m_Meshes.resize(meshId + 1);
    }
    MeshBuffer& mesh = m_Meshes[meshId];
    mesh.subMeshes.clear();
    mesh.subMeshes.reserve(subMeshIndices.size());

    std::vector<int32_t> combinedIndices;
    uint32_t totalIndices = 0;
    for (const auto& sm : subMeshIndices) {
        totalIndices += static_cast<uint32_t>(sm.size());
    }
    combinedIndices.reserve(totalIndices);

    for (const auto& sm : subMeshIndices) {
        SubMeshBuffer subMeshBuf;
        subMeshBuf.firstIndex = static_cast<uint32_t>(combinedIndices.size());
        subMeshBuf.indexCount = static_cast<uint32_t>(sm.size());
        mesh.subMeshes.push_back(subMeshBuf);
        
        combinedIndices.insert(combinedIndices.end(), sm.begin(), sm.end());
    }

    VkDeviceSize vertexBufferSize = sizeof(vertices[0]) * vertices.size();
    VkDeviceSize indexBufferSize = sizeof(combinedIndices[0]) * combinedIndices.size();

    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;

    // --- Vertex Buffer ---
    CreateBuffer(vertexBufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingBufferMemory);
    void* data;
    vkMapMemory(m_Device, stagingBufferMemory, 0, vertexBufferSize, 0, &data);
    memcpy(data, vertices.data(), (size_t)vertexBufferSize);
    vkUnmapMemory(m_Device, stagingBufferMemory);

    CreateBuffer(vertexBufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, mesh.vertexBuffer, mesh.vertexBufferMemory);
    CopyBuffer(stagingBuffer, mesh.vertexBuffer, vertexBufferSize);

    vkDestroyBuffer(m_Device, stagingBuffer, nullptr);
    vkFreeMemory(m_Device, stagingBufferMemory, nullptr);

    // --- Index Buffer ---
    CreateBuffer(indexBufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingBufferMemory);
    vkMapMemory(m_Device, stagingBufferMemory, 0, indexBufferSize, 0, &data);
    memcpy(data, combinedIndices.data(), (size_t)indexBufferSize);
    vkUnmapMemory(m_Device, stagingBufferMemory);

    CreateBuffer(indexBufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, mesh.indexBuffer, mesh.indexBufferMemory);
    CopyBuffer(stagingBuffer, mesh.indexBuffer, indexBufferSize);

    vkDestroyBuffer(m_Device, stagingBuffer, nullptr);
    vkFreeMemory(m_Device, stagingBufferMemory, nullptr);
}

} // namespace Endfield
