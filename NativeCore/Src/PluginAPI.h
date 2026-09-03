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
    
    // 유니티 렌더러에서 카메라 정보를 넘겨줌
    ENDFIELD_API void UpdateCameraState(float* viewMatrix, float* projMatrix);

    // 유니티 에디터 스크립트에서 익스포트한 씬 바이너리를 C++ 네이티브로 로드
    ENDFIELD_API void LoadNativeScene(const char* path);

    // 런타임에 FBX 인스턴스를 무작위 위치로 대량 복제 (성능 테스트용 스포너)
    ENDFIELD_API void SpawnNativeInstances(int count, float spread);

    // 네이티브 인스턴스들의 애니메이션 (일관된 움직임) 적용
    ENDFIELD_API void AnimateNativeInstances(float time, float deltaTime);
}
