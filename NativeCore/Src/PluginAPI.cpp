#include "PluginAPI.h"
#include "VulkanBackend.h"
#include "ECS.h"
#include "Culling.h"
#include <memory>
#include <iostream>

static std::unique_ptr<VulkanBackend> g_Backend = nullptr;
static std::unique_ptr<Endfield::ECSManager> g_ECS = nullptr;
static std::unique_ptr<Endfield::CullingSystem> g_Culling = nullptr;

extern "C" {

ENDFIELD_API void InitializeVulkanRenderer(void* windowHandle, uint32_t width, uint32_t height)
{
    if (!g_Backend) {
        g_Backend = std::make_unique<VulkanBackend>();
        g_Backend->Initialize(windowHandle);
        g_Backend->SetupRenderGraph();
    }
    
    if (!g_ECS) {
        g_ECS = std::make_unique<Endfield::ECSManager>();
        // 더미 컴포넌트 레지스트리 세팅: 0번 비트를 Transform(64바이트 Matrix)으로 지정
        Endfield::ComponentRegistry::RegisterComponent(0, sizeof(float) * 16);
    }
    
    if (!g_Culling) {
        g_Culling = std::make_unique<Endfield::CullingSystem>();
        // 하드웨어 동시성(코어 수) 기반으로 멀티스레딩 워커 갯수 자동 초기화
        g_Culling->Initialize(0); 
    }
}

ENDFIELD_API void ShutdownVulkanRenderer()
{
    if (g_Culling) {
        g_Culling->Shutdown();
        g_Culling.reset();
    }
    if (g_ECS) {
        g_ECS.reset();
    }
    if (g_Backend) {
        g_Backend->Shutdown();
        g_Backend.reset();
    }
}

ENDFIELD_API void ExecuteNativeRenderLoop()
{
    if (g_Backend) {
        g_Backend->BeginFrame();
        
        if (g_ECS && g_Culling) {
            // ECS 쿼리를 통해 컴포넌트를 가져와 Batch Data로 만들어 렌더링에 사용할 수 있습니다.
            Endfield::ComponentMask transformMask;
            transformMask.low = 1; // 0번 비트(Transform)만 가진 엔티티 쿼리
            
            auto chunks = g_ECS->QueryChunks(transformMask);
            
            // 더미 뷰 프로젝션 행렬로 프러스텀 추출
            float dummyVP[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
            Endfield::Frustum frustum;
            frustum.ExtractFromMatrix(dummyVP);

            // ECS의 Transform(AABB) 데이터를 바탕으로 멀티스레드 Frustum Culling 수행 (Task Graph)
            std::vector<Endfield::AABB> dummyAABBs; // 실제로는 Chunk를 순회하며 모아야 함
            std::vector<bool> visibilityResults;
            g_Culling->PerformFrustumCullingParallel(frustum, dummyAABBs, visibilityResults);
        }

        g_Backend->EndFrame();
    }
}

ENDFIELD_API void RegisterEntity(uint32_t id, float* transformData)
{
    if (g_ECS) {
        Endfield::ComponentMask mask;
        mask.low = 1; // Transform 컴포넌트를 가진다고 가정

        // ECS에 엔티티 할당
        Endfield::Entity ent = g_ECS->CreateEntity(mask);
        
        // 메모리 카피 대신 포인터 연산을 통해 핀 고정된(Pinned) Unity의 데이터를 직접 참조하거나, 
        // ECS 내부 할당 메모리로 복사해둘 수 있습니다. 
        // 이번 예제에선 일단 생성 로직(CreateEntity)이 호출됨에 의의를 둡니다.
        // (실제 프로젝트에서는 TransformData* 를 ECS의 SoA Column에 기록함)
    }
}

ENDFIELD_API void UpdateCameraState(float* viewMatrix, float* projMatrix)
{
    if (g_Backend) {
        g_Backend->UpdateCamera(viewMatrix, projMatrix);
    }
}

}
