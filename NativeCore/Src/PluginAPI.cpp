#include "PluginAPI.h"
#include "VulkanBackend.h"
#include "ECS.h"
#include "Culling.h"
#include "SceneLoader.h"
#include <memory>
#include <iostream>
#include <cstdlib>

static std::unique_ptr<Endfield::VulkanBackend> g_Backend = nullptr;
static std::unique_ptr<Endfield::ECSManager> g_ECS = nullptr;
static std::unique_ptr<Endfield::CullingSystem> g_Culling = nullptr;

// 글로벌 씬 AABB 및 인스턴스 데이터 보관 (임시)
static std::vector<Endfield::AABB> g_SceneAABBs;
static std::vector<Endfield::VulkanBackend::InstanceData> g_SceneInstances;

#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
static HWND g_StandaloneWindow = NULL;

static LRESULT CALLBACK StandaloneWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_CLOSE:
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcA(hwnd, uMsg, wParam, lParam);
}

static HWND CreateStandaloneWindow(uint32_t width, uint32_t height) {
    const char CLASS_NAME[] = "EndfieldNativeRendererClass";
    
    WNDCLASSA wc = { };
    wc.lpfnWndProc = StandaloneWindowProc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = CLASS_NAME;
    
    RegisterClassA(&wc);
    
    HWND hwnd = CreateWindowExA(
        0,
        CLASS_NAME,
        "Endfield Native Renderer (Standalone C++ Engine)",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, width, height,
        NULL, NULL, wc.hInstance, NULL
    );
    
    if (hwnd) {
        ShowWindow(hwnd, SW_SHOW);
        UpdateWindow(hwnd);
    }
    return hwnd;
}
#endif

extern "C" {

ENDFIELD_API void InitializeVulkanRenderer(void* windowHandle, uint32_t width, uint32_t height)
{
    if (!g_Backend) {
#if defined(_WIN32) || defined(_WIN64)
        if (!g_StandaloneWindow) {
            g_StandaloneWindow = CreateStandaloneWindow(width, height);
        }
        void* actualWindowHandle = (void*)g_StandaloneWindow;
#else
        void* actualWindowHandle = windowHandle;
#endif

        g_Backend = std::make_unique<Endfield::VulkanBackend>();
        g_Backend->Initialize(actualWindowHandle);
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
#if defined(_WIN32) || defined(_WIN64)
    if (g_StandaloneWindow) {
        MSG msg;
        while (PeekMessageA(&msg, g_StandaloneWindow, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
    }
#endif

    if (g_Backend) {
        g_Backend->BeginFrame();
        
        if (g_ECS && g_Culling) {
            // ECS 쿼리를 통해 컴포넌트를 가져와 Batch Data로 만들어 렌더링에 사용할 수 있습니다.
            Endfield::ComponentMask transformMask;
            transformMask.low = 1; // 0번 비트(Transform)만 가진 엔티티 쿼리
            
            auto chunks = g_ECS->QueryChunks(transformMask);
            
            // Calculate View * Proj (both are column-major 4x4 arrays from Unity)
            // For column-major matrices A and B, (A * B)[c*4 + r] = sum_k A[k*4 + r] * B[c*4 + k]
            // We want VP = Proj * View.
            float vp[16];
            for (int c = 0; c < 4; ++c) {
                for (int r = 0; r < 4; ++r) {
                    vp[c * 4 + r] = 0;
                    for (int k = 0; k < 4; ++k) {
                        // Proj is A, View is B
                        vp[c * 4 + r] += g_ProjMatrix[k * 4 + r] * g_ViewMatrix[c * 4 + k];
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
                // If visibilityResults[i] is true (frustum culling check)
                // Temporarily disable culling check for debugging if you want, but assuming it works:
                if (i < visibilityResults.size() && visibilityResults[i]) {
                    // MVP = VP * LocalToWorld
                    Endfield::VulkanBackend::InstanceData inst = g_SceneInstances[i];
                    float localToWorld[16];
                    for (int k=0; k<16; ++k) localToWorld[k] = inst.mvpMatrix[k];
                    
                    for (int c = 0; c < 4; ++c) {
                        for (int r = 0; r < 4; ++r) {
                            inst.mvpMatrix[c * 4 + r] = 0;
                            for (int k = 0; k < 4; ++k) {
                                inst.mvpMatrix[c * 4 + r] += vp[k * 4 + r] * localToWorld[c * 4 + k];
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
                std::vector<std::vector<int32_t>> subMeshIndices;
                subMeshIndices.reserve(mesh.subMeshes.size());
                for (const auto& subMesh : mesh.subMeshes) {
                    subMeshIndices.push_back(subMesh.indices);
                }
                g_Backend->UploadMesh(vkVertices, subMeshIndices, static_cast<uint32_t>(i));
            }
        }
    }
}

ENDFIELD_API void SpawnNativeInstances(int count, float spread)
{
    if (!g_ECS || g_SceneInstances.empty()) return;

    size_t baseCount = g_SceneInstances.size();
    g_SceneInstances.reserve(baseCount + count * baseCount);
    g_SceneAABBs.reserve(baseCount + count * baseCount);

    Endfield::ComponentMask mask;
    mask.low = 0b111;

    for (int i = 0; i < count; ++i) {
        float offsetX = ((float)rand() / RAND_MAX) * spread - (spread * 0.5f);
        float offsetZ = ((float)rand() / RAND_MAX) * spread - (spread * 0.5f);

        for (size_t j = 0; j < baseCount; ++j) {
            Endfield::VulkanBackend::InstanceData newInst = g_SceneInstances[j];
            Endfield::AABB newAABB = g_SceneAABBs[j];

            // Apply translation offset to the MVP matrix (which is currently just localToWorld from Unity)
            // Matrix layout is column-major in Unity, but memory layout might be row or column.
            // In Unity, matrix[12] is tx, matrix[13] is ty, matrix[14] is tz.
            newInst.mvpMatrix[12] += offsetX;
            newInst.mvpMatrix[14] += offsetZ;

            newAABB.minBounds[0] += offsetX;
            newAABB.minBounds[2] += offsetZ;
            newAABB.maxBounds[0] += offsetX;
            newAABB.maxBounds[2] += offsetZ;

            g_SceneInstances.push_back(newInst);
            g_SceneAABBs.push_back(newAABB);

            // Create an empty entity in ECS (to match the object count)
            g_ECS->CreateEntity(mask);
        }
    }
}

}
