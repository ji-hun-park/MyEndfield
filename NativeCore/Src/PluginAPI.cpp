#include "PluginAPI.h"
#include "VulkanBackend.h"
#include <memory>

static std::unique_ptr<VulkanBackend> g_Backend = nullptr;

extern "C" {

ENDFIELD_API void InitializeVulkanRenderer(void* windowHandle, uint32_t width, uint32_t height)
{
    if (!g_Backend) {
        g_Backend = std::make_unique<VulkanBackend>();
        g_Backend->Initialize(windowHandle);
        g_Backend->SetupRenderGraph();
    }
}

ENDFIELD_API void ShutdownVulkanRenderer()
{
    if (g_Backend) {
        g_Backend->Shutdown();
        g_Backend.reset();
    }
}

ENDFIELD_API void ExecuteNativeRenderLoop()
{
    if (g_Backend) {
        g_Backend->BeginFrame();
        // Here we could call SubmitBatch with actual ECS data
        g_Backend->EndFrame();
    }
}

ENDFIELD_API void RegisterEntity(uint32_t id, float* transformData)
{
    // Not implemented yet
}

ENDFIELD_API void UpdateCameraState(float* viewMatrix, float* projMatrix)
{
    if (g_Backend) {
        g_Backend->UpdateCamera(viewMatrix, projMatrix);
    }
}

}
