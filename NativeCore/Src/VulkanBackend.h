#pragma once

#include <vector>
#include <string>

#if defined(_WIN32)
#define VK_USE_PLATFORM_WIN32_KHR
#endif
#include <vulkan/vulkan.h>
#include "RenderGraph.h"
#include "SortKey.h"

namespace Endfield {

class VulkanBackend
{
public:
    VulkanBackend();
    ~VulkanBackend();

    struct InstanceData {
        float mvpMatrix[16];
        SortKey sortKey;
    };

    typedef void(*DebugLogFunc)(const char*);
    static void SetDebugCallback(DebugLogFunc callback);

    void Initialize(void* windowHandle);
    void Shutdown();

    void BeginFrame();
    void SubmitBatch(const void* batchData, int instanceCount);
    void EndFrame();

    void SetupRenderGraph();

    void UpdateCamera(float* viewMatrix, float* projMatrix);

private:
    void CreateInstance();
    void SetupDebugMessenger();
    void CreateSurface(void* windowHandle);
    
    // Extracted Helpers for SelectPhysicalDevice
    int ScoreDeviceSuitability(VkPhysicalDevice device);
    void SelectPhysicalDevice();
    
    void CreateLogicalDevice();
    void CreateCommandObjects();
    
    // Extracted Helpers for CreateSwapchain
    VkSurfaceFormatKHR ChooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats);
    VkPresentModeKHR ChooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes);
    VkExtent2D ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities);
    void CreateSwapchainImageViews();
    void CreateSwapchain();
    
    void CreateRenderPass();
    void CreateFramebuffers();
    
    // Extracted Helpers for CreateGraphicsPipeline
    VkPipelineShaderStageCreateInfo CreateShaderStageInfo(VkShaderStageFlagBits stage, VkShaderModule module);
    VkPipelineVertexInputStateCreateInfo CreateVertexInputStateInfo();
    VkPipelineInputAssemblyStateCreateInfo CreateInputAssemblyStateInfo();
    VkPipelineRasterizationStateCreateInfo CreateRasterizationStateInfo();
    void CreateGraphicsPipeline();
    
    VkShaderModule CreateShaderModule(const std::vector<char>& code);
    void CreateSyncObjects();

    // Core Vulkan Handles
    VkInstance m_Instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT m_DebugMessenger = VK_NULL_HANDLE;
    VkSurfaceKHR m_Surface = VK_NULL_HANDLE;
    VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
    VkDevice m_Device = VK_NULL_HANDLE;
    VkQueue m_GraphicsQueue = VK_NULL_HANDLE;
    VkQueue m_PresentQueue = VK_NULL_HANDLE;
    uint32_t m_GraphicsQueueFamilyIndex = 0;
    uint32_t m_PresentQueueFamilyIndex = 0;

    VkCommandPool m_CommandPool = VK_NULL_HANDLE;
    VkCommandBuffer m_CommandBuffer = VK_NULL_HANDLE;
    
    VkSwapchainKHR m_Swapchain = VK_NULL_HANDLE;
    std::vector<VkImage> m_SwapchainImages;
    VkFormat m_SwapchainImageFormat;
    VkExtent2D m_SwapchainExtent;
    std::vector<VkImageView> m_SwapchainImageViews;

    VkRenderPass m_RenderPass = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> m_SwapchainFramebuffers;
    VkPipelineLayout m_PipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_GraphicsPipeline = VK_NULL_HANDLE;

    VkSemaphore m_ImageAvailableSemaphore = VK_NULL_HANDLE;
    VkSemaphore m_RenderFinishedSemaphore = VK_NULL_HANDLE;
    VkFence m_InFlightFence = VK_NULL_HANDLE;
    uint32_t m_CurrentImageIndex = 0;

    // Descriptor Sets (Placeholders for Endfield architecture)
    VkDescriptorSet m_DescriptorSet0_Pass = VK_NULL_HANDLE;
    VkDescriptorSet m_DescriptorSet2_Object = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> m_MaterialSets;
    uint32_t m_LastBoundMaterialSet = 0xFFFFFFFF;


    RenderGraph m_RenderGraph;

    // Depth buffer resources
    VkImage m_DepthImage = VK_NULL_HANDLE;
    VkDeviceMemory m_DepthImageMemory = VK_NULL_HANDLE;
    VkImageView m_DepthImageView = VK_NULL_HANDLE;
    VkFormat m_DepthFormat;

    // Depth buffer helper methods
    VkFormat FindSupportedFormat(const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features);
    VkFormat FindDepthFormat();
    void CreateDepthResources();
    uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
};

} // namespace Endfield
