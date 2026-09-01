#include "PluginAPI.h"
#include "VulkanBackend.h"
#include "ECS.h"
#include "Culling.h"
#include "SceneLoader.h"
#include <memory>
#include <iostream>
#include <cstdlib>
#include <mutex>

static std::unique_ptr<Endfield::VulkanBackend> g_Backend = nullptr;
static std::unique_ptr<Endfield::ECSManager> g_ECS = nullptr;
static std::unique_ptr<Endfield::CullingSystem> g_Culling = nullptr;
static std::mutex g_NativeMutex;

// 멀티스레딩(메인 스레드 vs 렌더 스레드) 충돌 방지용 뮤텍스

#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#include <thread>
#include <atomic>
#include <condition_variable>

static HWND g_StandaloneWindow = NULL;
static std::thread g_WindowThread;
static std::atomic<bool> g_WindowThreadRunning{false};
static std::condition_variable g_WindowCV;
static std::mutex g_WindowInitMutex;

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
    std::cout << "[PluginAPI] Attempting to create standalone window with size: " << width << "x" << height << std::endl;
    const char CLASS_NAME[] = "EndfieldNativeRendererClass";
    
    WNDCLASSA wc = { };
    wc.lpfnWndProc = StandaloneWindowProc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = CLASS_NAME;
    
    ATOM atom = RegisterClassA(&wc);
    if (!atom && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return NULL;
    }
    
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
        SetForegroundWindow(hwnd);
        BringWindowToTop(hwnd);
        std::cout << "[PluginAPI] Standalone window created successfully. HWND: " << hwnd << std::endl;
    } else {
        std::cout << "[PluginAPI ERROR] Failed to create standalone window! Error: " << GetLastError() << std::endl;
    }
    return hwnd;
}

static void WindowThreadFunc(uint32_t width, uint32_t height) {
    g_StandaloneWindow = CreateStandaloneWindow(width, height);
    
    {
        std::lock_guard<std::mutex> lock(g_WindowInitMutex);
        g_WindowThreadRunning = true;
    }
    g_WindowCV.notify_one();

    if (g_StandaloneWindow) {
        MSG msg;
        while (GetMessageA(&msg, NULL, 0, 0) > 0) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
    }
    g_WindowThreadRunning = false;
}
#endif

extern "C" {

ENDFIELD_API void InitializeVulkanRenderer(void* windowHandle, uint32_t width, uint32_t height)
{
    if (!g_Backend) {
#if defined(_WIN32) || defined(_WIN64)
        if (!g_StandaloneWindow) {
            std::unique_lock<std::mutex> lock(g_WindowInitMutex);
            g_WindowThread = std::thread(WindowThreadFunc, width, height);
            g_WindowCV.wait(lock, []{ return g_WindowThreadRunning.load(); });
        }
        void* actualWindowHandle = (void*)g_StandaloneWindow;
#else
        void* actualWindowHandle = windowHandle;
#endif

        g_Backend = std::make_unique<Endfield::VulkanBackend>();
        g_Backend->Initialize(actualWindowHandle);
        g_Backend->SetupRenderGraph();
        g_ECS = std::make_unique<Endfield::ECSManager>();
        
    }
    
    if (!g_ECS) {
        g_ECS = std::make_unique<Endfield::ECSManager>();
        // Bit 0: Pinned Pointer (float*) for Unity bridging
        Endfield::ComponentRegistry::RegisterComponent(0, sizeof(float*));
        // Bit 1: TransformComponent (64 bytes)
        Endfield::ComponentRegistry::RegisterComponent(1, sizeof(Endfield::TransformComponent));
        // Bit 2: BoundsComponent (24 bytes)
        Endfield::ComponentRegistry::RegisterComponent(2, sizeof(Endfield::BoundsComponent));
        // Bit 3: MeshComponent (12 bytes)
        Endfield::ComponentRegistry::RegisterComponent(3, sizeof(Endfield::MeshComponent));
    }
    
    if (!g_Culling) {
        g_Culling = std::make_unique<Endfield::CullingSystem>();
        // 하드웨어 동시성(코어 수) 기반으로 멀티스레딩 워커 갯수 자동 초기화
        g_Culling->Initialize(0); 
    }
}

ENDFIELD_API void RegisterDebugCallback(Endfield::VulkanBackend::DebugLogFunc callback)
{
    Endfield::VulkanBackend::SetDebugCallback(callback);
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

#if defined(_WIN32) || defined(_WIN64)
    if (g_StandaloneWindow) {
        PostMessageA(g_StandaloneWindow, WM_CLOSE, 0, 0);
        if (g_WindowThread.joinable()) {
            g_WindowThread.join();
        }
        g_StandaloneWindow = NULL;
    }
#endif
}

static float g_ViewMatrix[16];
static float g_ProjMatrix[16];

ENDFIELD_API void ExecuteNativeRenderLoop()
{
    std::lock_guard<std::mutex> lock(g_NativeMutex);
if (g_Backend) {
        if (g_Backend->BeginFrame()) {
        
        if (g_ECS && g_Culling) {
            Endfield::ComponentMask mask;
            mask.low = 0b1110; // Bit 1(Transform), 2(Bounds), 3(Mesh)
            auto chunks = g_ECS->QueryChunks(mask);
            
            // ViewProj Matrix 계산
            float vp[16];
            for (int c = 0; c < 4; ++c) {
                for (int r = 0; r < 4; ++r) {
                    vp[c * 4 + r] = 0;
                    for (int k = 0; k < 4; ++k) {
                        vp[c * 4 + r] += g_ProjMatrix[k * 4 + r] * g_ViewMatrix[c * 4 + k];
                    }
                }
            }
            
            Endfield::Frustum frustum;
            frustum.ExtractFromMatrix(vp);

            // ECS의 SoA 청크 메모리를 직접 참조하여 컬링 및 인스턴스 배열 조립
            std::vector<Endfield::VulkanBackend::InstanceData> visibleInstances;
            
            for (auto chunk : chunks) {
                auto* transforms = g_ECS->GetComponentArray<Endfield::TransformComponent>(chunk, 1);
                auto* bounds = g_ECS->GetComponentArray<Endfield::BoundsComponent>(chunk, 2);
                auto* meshes = g_ECS->GetComponentArray<Endfield::MeshComponent>(chunk, 3);
                
                if (!transforms || !bounds || !meshes) continue;

                // 1. Chunk 단위 병렬 Frustum Culling (배열 형태이므로 캐시 친화적)
                // 현재는 싱글 스레드 루프지만, 각 청크를 잡 시스템 워커에 할당하여 병렬 처리 가능
                for (uint32_t i = 0; i < chunk->entityCount; ++i) {
                    Endfield::AABB aabb = { 
                        {bounds[i].minBounds[0], bounds[i].minBounds[1], bounds[i].minBounds[2]},
                        {bounds[i].maxBounds[0], bounds[i].maxBounds[1], bounds[i].maxBounds[2]}
                    };
                    if (frustum.Intersects(aabb)) {
                        Endfield::VulkanBackend::InstanceData inst;
                        for (int k = 0; k < 16; ++k) {
                            inst.mvpMatrix[k] = transforms[i].localToWorld[k];
                        }
                        inst.sortKey.value = 0;
                        inst.sortKey.materialID = static_cast<uint16_t>(meshes[i].materialId);
                        inst.sortKey.pipelineID = static_cast<uint16_t>(meshes[i].meshId);
                        inst.sortKey.depth = 0;
                        inst.subMeshIndex = static_cast<uint32_t>(meshes[i].subMeshIndex);
                        
                        visibleInstances.push_back(inst);
                    }
                }
            }

            if (!visibleInstances.empty()) {
                // 그래픽스 커맨드 버퍼에 Draw Call 제출
                g_Backend->SubmitBatch(visibleInstances.data(), static_cast<int>(visibleInstances.size()));
            }
        } // close if (g_ECS && g_Culling)

        g_Backend->EndFrame();
        }
    }
}

ENDFIELD_API void RegisterEntity(uint32_t id, float* transformData)
{
    if (g_ECS) {
        Endfield::ComponentMask mask;
        mask.low = 1; // Transform 컴포넌트(Bit 0)를 가짐

        // ECS에 엔티티 할당
        Endfield::Entity ent = g_ECS->CreateEntity(mask);
        
        // Pinned된 Unity의 transformData 포인터를 직접 ECS 컴포넌트에 저장합니다.
        g_ECS->SetComponentData<float*>(ent, 0, transformData);
        
        std::cout << "[PluginAPI] Registered Entity ID " << id << " with Transform Data ptr: " << transformData << std::endl;
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
    std::lock_guard<std::mutex> lock(g_NativeMutex);
if (g_ECS && path != nullptr && g_Backend) {
        std::string filePath(path);
        std::vector<Endfield::SceneLoader::MeshData> meshes;
        if (Endfield::SceneLoader::LoadScene(filePath, *g_ECS, meshes)) {
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
    std::lock_guard<std::mutex> lock(g_NativeMutex);
    if (!g_ECS) return;

    Endfield::ComponentMask mask;
    mask.low = 0b1110; // Bit 1(Transform), 2(Bounds), 3(Mesh)
    auto chunks = g_ECS->QueryChunks(mask);
    
    // 복제할 원본 데이터를 임시로 저장
    struct TemplateData {
        Endfield::TransformComponent t;
        Endfield::BoundsComponent b;
        Endfield::MeshComponent m;
    };
    std::vector<TemplateData> templates;

    for (auto chunk : chunks) {
        auto* transforms = g_ECS->GetComponentArray<Endfield::TransformComponent>(chunk, 1);
        auto* bounds = g_ECS->GetComponentArray<Endfield::BoundsComponent>(chunk, 2);
        auto* meshes = g_ECS->GetComponentArray<Endfield::MeshComponent>(chunk, 3);
        
        if (transforms && bounds && meshes) {
            for (uint32_t i = 0; i < chunk->entityCount; ++i) {
                templates.push_back({transforms[i], bounds[i], meshes[i]});
            }
        }
    }

    if (templates.empty()) return;

    for (int i = 0; i < count; ++i) {
        float offsetX = ((float)rand() / RAND_MAX) * spread - (spread * 0.5f);
        float offsetZ = ((float)rand() / RAND_MAX) * spread - (spread * 0.5f);

        for (auto& tpl : templates) {
            Endfield::TransformComponent newT = tpl.t;
            Endfield::BoundsComponent newB = tpl.b;
            
            // Unity column-major matrix
            newT.localToWorld[12] += offsetX;
            newT.localToWorld[14] += offsetZ;

            newB.minBounds[0] += offsetX;
            newB.minBounds[2] += offsetZ;
            newB.maxBounds[0] += offsetX;
            newB.maxBounds[2] += offsetZ;

            Endfield::Entity ent = g_ECS->CreateEntity(mask);
            g_ECS->SetComponentData(ent, 1, newT);
            g_ECS->SetComponentData(ent, 2, newB);
            g_ECS->SetComponentData(ent, 3, tpl.m);
        }
    }
}

}
