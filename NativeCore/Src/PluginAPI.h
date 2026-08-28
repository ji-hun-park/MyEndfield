#pragma once
#include <cstdint>

#if defined(_MSC_VER)
#define ENDFIELD_API __declspec(dllexport)
#else
#define ENDFIELD_API __attribute__((visibility("default")))
#endif

// 유니티(IUnityInterface 등)에 의존하지 않는 순수 독립형 C API 노출
extern "C" {
    // 렌더러 초기화 (운영체제 Window Handle이나 Surface 포인터를 직접 받아 Vulkan 초기화)
    ENDFIELD_API void InitializeVulkanRenderer(void* windowHandle, uint32_t width, uint32_t height);
    
    // 렌더러 종료
    ENDFIELD_API void ShutdownVulkanRenderer();
    
    // 매 프레임 실행될 네이티브 렌더 루프 (유니티 렌더 스레드에 종속되지 않고 직접 호출)
    ENDFIELD_API void ExecuteNativeRenderLoop();
    
    // ECS 데이터 브릿지 (트랜스폼, 렌더링 데이터 등을 C++ 측으로 넘김)
    ENDFIELD_API void RegisterEntity(uint32_t id, float* transformData);
}
