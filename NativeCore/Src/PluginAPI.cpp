#include "PluginAPI.h"
#include "VulkanBackend.h"
#include "ECS.h"
#include "Culling.h"
#include "SceneLoader.h"
#include <memory>
#include <iostream>

static std::unique_ptr<Endfield::VulkanBackend> g_Backend = nullptr;
static std::unique_ptr<Endfield::ECSManager> g_ECS = nullptr;
static std::unique_ptr<Endfield::CullingSystem> g_Culling = nullptr;

// 글로벌 씬 AABB 및 인스턴스 데이터 보관 (임시)
static std::vector<Endfield::AABB> g_SceneAABBs;
static std::vector<Endfield::VulkanBackend::InstanceData> g_SceneInstances;

extern "C" {

ENDFIELD_API void InitializeVulkanRenderer(void* windowHandle, uint32_t width, uint32_t height)
{
    if (!g_Backend) {
        g_Backend = std::make_unique<Endfield::VulkanBackend>();
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
    g_ECS.reset();
    if (g_Backend) {
        g_Backend->Shutdown();
        g_Backend.reset();
    }
}

static float g_ViewMatrix[16];
static float g_ProjMatrix[16];

ENDFIELD_API void ExecuteNativeRenderLoop()
{
    if (g_Backend) {
        g_Backend->BeginFrame();
        
        if (g_ECS && g_Culling) {
            // ECS 쿼리를 통해 컴포넌트를 가져와 Batch Data로 만들어 렌더링에 사용할 수 있습니다.
            Endfield::ComponentMask transformMask;
            transformMask.low = 1; // 0번 비트(Transform)만 가진 엔티티 쿼리
            
            auto chunks = g_ECS->QueryChunks(transformMask);
            
            // 캐싱된 View, Proj 매트릭스로 프러스텀 추출 (단순 행렬곱 예시)
            float vp[16];
            for (int i = 0; i < 4; ++i) {
                for (int j = 0; j < 4; ++j) {
                    vp[i * 4 + j] = 0;
                    for (int k = 0; k < 4; ++k) {
                        vp[i * 4 + j] += g_ProjMatrix[i * 4 + k] * g_ViewMatrix[k * 4 + j];
                    }
                }
            }
            
            Endfield::Frustum frustum;
            frustum.ExtractFromMatrix(vp);

            // ECS의 Transform(AABB) 데이터를 바탕으로 멀티스레드 Frustum Culling 수행 (Task Graph)
            std::vector<bool> visibilityResults;
            g_Culling->PerformFrustumCullingParallel(frustum, g_SceneAABBs, visibilityResults);

            // Culling 결과를 바탕으로 가시성 있는 인스턴스만 추려냄
            std::vector<Endfield::VulkanBackend::InstanceData> visibleInstances;
            visibleInstances.reserve(g_SceneInstances.size());
            for (size_t i = 0; i < g_SceneInstances.size(); ++i) {
                if (i < visibilityResults.size() && visibilityResults[i]) {
                    // MVP 매트릭스 계산 (LocalToWorld * ViewProj)
                    Endfield::VulkanBackend::InstanceData inst = g_SceneInstances[i];
                    float localToWorld[16];
                    for (int k=0; k<16; ++k) localToWorld[k] = inst.mvpMatrix[k];
                    
                    for (int r = 0; r < 4; ++r) {
                        for (int c = 0; c < 4; ++c) {
                            inst.mvpMatrix[r * 4 + c] = 0;
                            for (int k = 0; k < 4; ++k) {
                                inst.mvpMatrix[r * 4 + c] += vp[r * 4 + k] * localToWorld[k * 4 + c];
                            }
                        }
                    }
                    visibleInstances.push_back(inst);
                }
            }

            // 그래픽스 커맨드 버퍼에 Draw Call 제출
            g_Backend->SubmitBatch(visibleInstances.data(), static_cast<int>(visibleInstances.size()));
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
    for (int i = 0; i < 16; ++i) {
        g_ViewMatrix[i] = viewMatrix[i];
        g_ProjMatrix[i] = projMatrix[i];
    }

    if (g_Backend) {
        g_Backend->UpdateCamera(viewMatrix, projMatrix);
    }
}


ENDFIELD_API void LoadNativeScene(const char* path)
{
    if (g_ECS && path != nullptr && g_Backend) {
        std::string filePath(path);
        std::vector<Endfield::SceneLoader::MeshData> meshes;
        if (Endfield::SceneLoader::LoadScene(filePath, *g_ECS, g_SceneAABBs, g_SceneInstances, meshes)) {
            for (size_t i = 0; i < meshes.size(); ++i) {
                // Cast SceneLoader::Vertex to VulkanBackend::Vertex
                const auto& mesh = meshes[i];
                std::vector<Endfield::VulkanBackend::Vertex> vkVertices;
                vkVertices.reserve(mesh.vertices.size());
                for (const auto& v : mesh.vertices) {
                    vkVertices.push_back({v.posX, v.posY, v.posZ, v.normX, v.normY, v.normZ, v.uvX, v.uvY});
                }
                g_Backend->UploadMesh(vkVertices, mesh.indices, static_cast<uint32_t>(i));
            }
        }
    }
}

}
