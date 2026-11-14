#define NOMINMAX
#include "PluginAPI.h"
#include "VulkanBackend.h"
#include "ECS.h"
#include "Culling.h"
#include "SceneLoader.h"
#include "SortKey.h"
#include "ThreadPool.h"
#include <memory>
#include <iostream>
#include <cstdlib>
#include <mutex>
#include <cmath>
#include <algorithm>
#include <limits>
#include <vector>

static std::unique_ptr<Endfield::VulkanBackend> g_Backend = nullptr;
static std::unique_ptr<Endfield::ECSManager> g_ECS = nullptr;
static std::unique_ptr<Endfield::CullingSystem> g_Culling = nullptr;
static std::unique_ptr<Endfield::ThreadPool> g_CullingThreadPool = nullptr;
static std::mutex g_NativeMutex;

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

static std::atomic<bool> g_WindowShouldClose{false};
static std::atomic<bool> g_WindowSafeToDestroy{false};
#define WM_SAFE_DESTROY (WM_USER + 1)

static LRESULT CALLBACK StandaloneWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_CLOSE:
            g_WindowShouldClose = true;
            return 0;
        case WM_SAFE_DESTROY:
            DestroyWindow(hwnd);
            return 0;
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
    g_StandaloneWindow = NULL;
}
#endif

struct SpawnTemplateData {
    Endfield::TransformComponent t;
    Endfield::BoundsComponent b;
    Endfield::MeshComponent m;
};
static std::vector<SpawnTemplateData> g_SpawnTemplates;

struct InlinePartTemplate {
    Endfield::TransformComponent t;
    Endfield::BoundsComponent b;
    Endfield::InlineSubRendererComponent inlineSub;
};

// 동일한 meshId를 가진 서브메시 템플릿끼리 묶어 원래의 스케일과 로컬 트랜스폼을 보존
static std::vector<InlinePartTemplate> BuildInlinePartTemplates()
{
    std::vector<InlinePartTemplate> parts;
    for (const auto& tpl : g_SpawnTemplates) {
        // 같은 meshId와 같은 위치/스케일을 가진 파츠가 이미 있는지 확인
        InlinePartTemplate* targetPart = nullptr;
        for (auto& p : parts) {
            if (p.inlineSub.meshId == tpl.m.meshId &&
                p.inlineSub.subMeshCount < Endfield::MAX_INLINE_SUB_RENDERERS &&
                std::memcmp(p.t.localToWorld, tpl.t.localToWorld, sizeof(float) * 16) == 0)
            {
                targetPart = &p;
                break;
            }
        }

        if (!targetPart) {
            InlinePartTemplate newPart{};
            newPart.t = tpl.t;
            newPart.b = tpl.b;
            newPart.inlineSub.meshId = tpl.m.meshId;
            newPart.inlineSub.subMeshCount = 0;
            parts.push_back(newPart);
            targetPart = &parts.back();
        }

        uint32_t idx = targetPart->inlineSub.subMeshCount++;
        targetPart->inlineSub.subMeshIndices[idx] = static_cast<uint16_t>(tpl.m.subMeshIndex);
        targetPart->inlineSub.materialIds[idx]    = static_cast<uint16_t>(tpl.m.materialId);

        for (int axis = 0; axis < 3; ++axis) {
            targetPart->b.minBounds[axis] = (std::min)(targetPart->b.minBounds[axis], tpl.b.minBounds[axis]);
            targetPart->b.maxBounds[axis] = (std::max)(targetPart->b.maxBounds[axis], tpl.b.maxBounds[axis]);
        }
    }
    return parts;
}

ENDFIELD_API void SpawnNativeInstances(int count, float spread)
{
    std::lock_guard<std::mutex> lock(g_NativeMutex);
    if (!g_ECS || count <= 0) return;

    if (g_SpawnTemplates.empty()) {
        Endfield::ComponentMask mask;
        mask.low = Endfield::MASK_STANDARD_RENDER;
        auto chunks = g_ECS->QueryChunks(mask);
        for (auto chunk : chunks) {
            auto* transforms = g_ECS->GetComponentArray<Endfield::TransformComponent>(chunk, Endfield::BIT_TRANSFORM);
            auto* bounds = g_ECS->GetComponentArray<Endfield::BoundsComponent>(chunk, Endfield::BIT_BOUNDS);
            auto* meshes = g_ECS->GetComponentArray<Endfield::MeshComponent>(chunk, Endfield::BIT_MESH);
            if (transforms && bounds && meshes) {
                for (uint32_t i = 0; i < chunk->entityCount; ++i) {
                    g_SpawnTemplates.push_back({transforms[i], bounds[i], meshes[i]});
                }
            }
        }
    }

    if (g_SpawnTemplates.empty()) return;

    std::vector<InlinePartTemplate> partTemplates = BuildInlinePartTemplates();
    g_ECS->Clear();

    Endfield::ComponentMask mask;
    mask.low = Endfield::MASK_TRANSFORM | Endfield::MASK_BOUNDS | Endfield::MASK_SUB_RENDERERS | Endfield::MASK_VISIBILITY;

    for (int i = 0; i < count; ++i) {
        // 첫 번째(i == 0) 인스턴스는 카메라 바로 앞 원본 위치(offset = 0)에 배치하여 가까이서 바로 보이도록 함
        float offsetX = (i == 0) ? 0.0f : (((float)rand() / RAND_MAX) * spread - (spread * 0.5f));
        float offsetZ = (i == 0) ? 0.0f : (((float)rand() / RAND_MAX) * spread - (spread * 0.5f));

        for (const auto& part : partTemplates) {
            Endfield::TransformComponent newT = part.t;
            Endfield::BoundsComponent newB = part.b;
            
            newT.localToWorld[12] += offsetX;
            newT.localToWorld[14] += offsetZ;

            newB.minBounds[0] += offsetX;
            newB.minBounds[2] += offsetZ;
            newB.maxBounds[0] += offsetX;
            newB.maxBounds[2] += offsetZ;

            Endfield::Entity ent = g_ECS->CreateEntity(mask);
            g_ECS->SetComponentData(ent, Endfield::BIT_TRANSFORM,     newT);
            g_ECS->SetComponentData(ent, Endfield::BIT_BOUNDS,        newB);
            g_ECS->SetComponentData(ent, Endfield::BIT_SUB_RENDERERS, part.inlineSub);
        }
    }
}

extern "C" {

ENDFIELD_API void InitializeVulkanRenderer(void* windowHandle, uint32_t width, uint32_t height)
{
#if defined(_WIN32) || defined(_WIN64)
    g_WindowShouldClose = false;
    g_WindowSafeToDestroy = false;
#endif

    if (!g_Backend) {
#if defined(_WIN32) || defined(_WIN64)
        if (!g_StandaloneWindow) {
            std::unique_lock<std::mutex> lock(g_WindowInitMutex);
            if (g_WindowThread.joinable()) {
                g_WindowThread.join();
            }
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
    }
    
    if (!g_ECS) {
        g_ECS = std::make_unique<Endfield::ECSManager>();
        Endfield::ComponentRegistry::RegisterComponent(0, sizeof(float*));
        Endfield::ComponentRegistry::RegisterComponent(Endfield::BIT_TRANSFORM,     sizeof(Endfield::TransformComponent));
        Endfield::ComponentRegistry::RegisterComponent(Endfield::BIT_BOUNDS,        sizeof(Endfield::BoundsComponent));
        Endfield::ComponentRegistry::RegisterComponent(Endfield::BIT_MESH,          sizeof(Endfield::MeshComponent));
        Endfield::ComponentRegistry::RegisterComponent(Endfield::BIT_SUB_RENDERERS, sizeof(Endfield::InlineSubRendererComponent));
        Endfield::ComponentRegistry::RegisterComponent(Endfield::BIT_VISIBILITY,    sizeof(uint32_t));
    }
    
    if (!g_Culling) {
        uint32_t hwThreads = std::thread::hardware_concurrency();
        uint32_t numWorkers = (hwThreads > 1) ? (hwThreads - 1) : 4;
        g_CullingThreadPool = std::make_unique<Endfield::ThreadPool>(numWorkers);

        g_Culling = std::make_unique<Endfield::CullingSystem>();
        g_Culling->Initialize(numWorkers, g_CullingThreadPool.get(), false);
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
    g_CullingThreadPool.reset();
    g_ECS.reset();
    if (g_Backend) {
        g_Backend->Shutdown();
        g_Backend.reset();
    }

#if defined(_WIN32) || defined(_WIN64)
    if (g_StandaloneWindow) {
        PostMessageA(g_StandaloneWindow, WM_SAFE_DESTROY, 0, 0);
    }
    if (g_WindowThread.joinable()) {
        g_WindowThread.join();
    }
    g_StandaloneWindow = NULL;
#endif
}

static float g_ViewMatrix[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
static float g_ProjMatrix[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};

ENDFIELD_API void ExecuteNativeRenderLoop()
{
    std::lock_guard<std::mutex> lock(g_NativeMutex);
    try {
#if defined(_WIN32) || defined(_WIN64)
        if (g_WindowShouldClose.load() && !g_WindowSafeToDestroy.load()) {
            if (g_Backend) {
                g_Backend->WaitDeviceIdle();
                g_WindowSafeToDestroy = true;
                if (g_StandaloneWindow) {
                    PostMessageA(g_StandaloneWindow, WM_SAFE_DESTROY, 0, 0);
                }
            }
            return;
        }
        if (g_WindowShouldClose.load()) {
            return;
        }
#endif
        Endfield::BenchmarkManager::Instance().BeginFrame();

        if (g_Backend && g_Backend->BeginFrame()) {
            if (g_ECS && g_Culling) {
                auto& stats = Endfield::BenchmarkManager::Instance().GetCurrentFrameStats();

                // Transform + Bounds를 가진 모든 렌더러 청크 조회 (InlineSubRenderer 및 단일 Mesh 모두 호환)
                Endfield::ComponentMask queryMask;
                queryMask.low = Endfield::MASK_TRANSFORM | Endfield::MASK_BOUNDS;
                
                std::vector<Endfield::Chunk*> chunks;
                {
                    Endfield::ScopedTimer timer(&stats.ecsQueryTimeMs);
                    chunks = g_ECS->QueryChunks(queryMask);
                }
                
                // View-Projection 행렬 계산
                float vp[16];
                for (int c = 0; c < 4; ++c) {
                    for (int r = 0; r < 4; ++r) {
                        vp[c * 4 + r] = 0.0f;
                        for (int k = 0; k < 4; ++k) {
                            vp[c * 4 + r] += g_ProjMatrix[k * 4 + r] * g_ViewMatrix[c * 4 + k];
                        }
                    }
                }
                
                Endfield::Frustum frustum;
                frustum.ExtractFromMatrix(vp);

                uint32_t totalEntities = 0;
                for (auto* chunk : chunks) {
                    totalEntities += chunk->entityCount;
                }

                std::vector<Endfield::VulkanBackend::InstanceData> visibleInstances;
                uint32_t totalSubMeshDraws = 0;
                uint32_t culledFrustumDraws = 0;
                bool enableFrustum = Endfield::BenchmarkManager::Instance().IsFrustumCullingEnabled();

                {
                    Endfield::ScopedTimer timer(&stats.frustumCullingTimeMs);
                    g_ECS->AcquireWorkerReadFence();

                    if (totalEntities > 0) {
                        constexpr uint32_t CULL_BATCH_SIZE = 256;
                        uint32_t numCullChunks = (totalEntities + CULL_BATCH_SIZE - 1) / CULL_BATCH_SIZE;

                        std::vector<Endfield::AABB> entityAABBs(totalEntities);
                        std::vector<Endfield::AABB> chunkAABBs(numCullChunks);

                        for (uint32_t c = 0; c < numCullChunks; ++c) {
                            chunkAABBs[c].minBounds[0] = std::numeric_limits<float>::max();
                            chunkAABBs[c].minBounds[1] = std::numeric_limits<float>::max();
                            chunkAABBs[c].minBounds[2] = std::numeric_limits<float>::max();
                            chunkAABBs[c].maxBounds[0] = std::numeric_limits<float>::lowest();
                            chunkAABBs[c].maxBounds[1] = std::numeric_limits<float>::lowest();
                            chunkAABBs[c].maxBounds[2] = std::numeric_limits<float>::lowest();
                        }

                        // 1. 캐릭터 엔티티 단위(10,000개) AABB 및 256단위 월드 청크 AABB 구성
                        uint32_t writeIdx = 0;
                        for (auto* chunk : chunks) {
                            auto* bounds = g_ECS->GetComponentArray<Endfield::BoundsComponent>(chunk, Endfield::BIT_BOUNDS);
                            if (!bounds) continue;

                            for (uint32_t i = 0; i < chunk->entityCount; ++i) {
                                Endfield::AABB box = {
                                    { bounds[i].minBounds[0], bounds[i].minBounds[1], bounds[i].minBounds[2] },
                                    { bounds[i].maxBounds[0], bounds[i].maxBounds[1], bounds[i].maxBounds[2] }
                                };
                                entityAABBs[writeIdx] = box;

                                uint32_t batchIdx = writeIdx / CULL_BATCH_SIZE;
                                for (int axis = 0; axis < 3; ++axis) {
                                    chunkAABBs[batchIdx].minBounds[axis] = (std::min)(chunkAABBs[batchIdx].minBounds[axis], box.minBounds[axis]);
                                    chunkAABBs[batchIdx].maxBounds[axis] = (std::max)(chunkAABBs[batchIdx].maxBounds[axis], box.maxBounds[axis]);
                                }
                                writeIdx++;
                            }
                        }

                        // 2. 15개 워커 뷰 병렬 프러스텀 컬링 (캐릭터당 1회만 검사)
                        std::vector<uint32_t> visMasks;
                        if (enableFrustum) {
                            g_Culling->PerformMultiViewFrustumCullingParallel(
                                &frustum,
                                1,
                                chunkAABBs.data(),
                                CULL_BATCH_SIZE,
                                entityAABBs,
                                visMasks
                            );
                        }

                        visibleInstances.reserve(totalEntities * 19);

                        // 3. 생존한 엔티티의 인라인 서브 렌더러(InlineSubRendererComponent) 컬럼을 펼쳐 드로우 패킷 생성
                        uint32_t readIdx = 0;
                        for (auto* chunk : chunks) {
                            auto* transforms   = g_ECS->GetComponentArray<Endfield::TransformComponent>(chunk, Endfield::BIT_TRANSFORM);
                            auto* subRenderers = g_ECS->GetComponentArray<Endfield::InlineSubRendererComponent>(chunk, Endfield::BIT_SUB_RENDERERS);
                            auto* meshes       = g_ECS->GetComponentArray<Endfield::MeshComponent>(chunk, Endfield::BIT_MESH);
                            auto* visColumn    = g_ECS->GetComponentArray<uint32_t>(chunk, Endfield::BIT_VISIBILITY);

                            if (!transforms) continue;

                            for (uint32_t i = 0; i < chunk->entityCount; ++i, ++readIdx) {
                                uint32_t vis = enableFrustum ? visMasks[readIdx] : 1u;
                                if (visColumn) {
                                    visColumn[i] = vis;
                                }

                                // [경로 A] InlineSubRendererComponent(19개 서브메시 인라인 배열)가 존재하는 아키타입
                                if (subRenderers != nullptr && subRenderers[i].subMeshCount > 0) {
                                    const auto& subComp = subRenderers[i];
                                    uint32_t subCount = subComp.subMeshCount;
                                    totalSubMeshDraws += subCount;

                                    if ((vis & (1u << 0)) != 0u) {
                                        float dx = transforms[i].localToWorld[12] + g_ViewMatrix[12];
                                        float dy = transforms[i].localToWorld[13] + g_ViewMatrix[13];
                                        float dz = transforms[i].localToWorld[14] + g_ViewMatrix[14];
                                        float dist = std::sqrt(dx * dx + dy * dy + dz * dz);

                                        Endfield::VulkanBackend::InstanceData inst;
                                        for (int k = 0; k < 16; ++k) {
                                            inst.mvpMatrix[k] = transforms[i].localToWorld[k];
                                        }

                                        // 포인터 점프 없이 청크 인라인 배열에서 19개 서브메시 드로우콜 즉시 생성
                                        for (uint32_t s = 0; s < subCount; ++s) {
                                            inst.sortKey = Endfield::SortKeyBuilder::CreateOpaque(
                                                0u,
                                                1u,
                                                subComp.materialIds[s],
                                                dist,
                                                Endfield::DEFAULT_MAX_SORT_DISTANCE,
                                                static_cast<uint16_t>(subComp.meshId),
                                                subComp.subMeshIndices[s]
                                            );
                                            inst.subMeshIndex = static_cast<uint32_t>(subComp.subMeshIndices[s]);
                                            visibleInstances.push_back(inst);
                                        }
                                    } else {
                                        culledFrustumDraws += subCount;
                                    }
                                }
                                // [경로 B] 단일 MeshComponent만 가진 일반 엔티티 폴백
                                else if (meshes != nullptr) {
                                    totalSubMeshDraws += 1;
                                    if ((vis & (1u << 0)) != 0u) {
                                        float dx = transforms[i].localToWorld[12] + g_ViewMatrix[12];
                                        float dy = transforms[i].localToWorld[13] + g_ViewMatrix[13];
                                        float dz = transforms[i].localToWorld[14] + g_ViewMatrix[14];
                                        float dist = std::sqrt(dx * dx + dy * dy + dz * dz);

                                        Endfield::VulkanBackend::InstanceData inst;
                                        for (int k = 0; k < 16; ++k) {
                                            inst.mvpMatrix[k] = transforms[i].localToWorld[k];
                                        }
                                        inst.sortKey = Endfield::SortKeyBuilder::CreateOpaque(
                                            0u,
                                            1u,
                                            static_cast<uint16_t>(meshes[i].materialId),
                                            dist,
                                            Endfield::DEFAULT_MAX_SORT_DISTANCE,
                                            static_cast<uint16_t>(meshes[i].meshId),
                                            static_cast<uint16_t>(meshes[i].subMeshIndex)
                                        );
                                        inst.subMeshIndex = static_cast<uint32_t>(meshes[i].subMeshIndex);
                                        visibleInstances.push_back(inst);
                                    } else {
                                        culledFrustumDraws += 1;
                                    }
                                }
                            }
                        }
                    }

                    g_ECS->ReleaseWorkerReadFence();
                }

                stats.totalInstances   = totalSubMeshDraws;
                stats.culledFrustum    = culledFrustumDraws;
                stats.visibleInstances = static_cast<uint32_t>(visibleInstances.size());

                if (!visibleInstances.empty()) {
                    g_Backend->SubmitBatch(visibleInstances.data(), static_cast<int>(visibleInstances.size()));
                }
            }

            g_Backend->EndFrame();
        }
        Endfield::BenchmarkManager::Instance().EndFrame();
    } catch (const std::exception& e) {
        Endfield::BenchmarkManager::Instance().EndFrame();
        std::cerr << "[PluginAPI Exception] " << e.what() << std::endl;
        Endfield::VulkanBackend::LogToUnity(std::string("[NativeRenderLoop Exception] ") + e.what());
    }
}

ENDFIELD_API void GetLatestBenchmarkStats(Endfield::NativeBenchmarkStats* outStats)
{
    if (outStats) {
        *outStats = Endfield::BenchmarkManager::Instance().GetLatestFrameStats();
    }
}

ENDFIELD_API void SetBenchmarkCullingOptions(bool enableFrustum, bool enableOcclusion)
{
    Endfield::BenchmarkManager::Instance().SetCullingOptions(enableFrustum, enableOcclusion);
}

ENDFIELD_API void RunNativeHeadlessBenchmark(int instanceCount, int iterations, bool enableCulling, Endfield::NativeBenchmarkStats* outAverages)
{
    Endfield::BenchmarkManager::Instance().RunHeadlessBenchmark(instanceCount, iterations, enableCulling, outAverages);
}

ENDFIELD_API void RegisterEntity(uint32_t id, float* transformData)
{
    if (g_ECS) {
        Endfield::ComponentMask mask;
        mask.low = 1;

        Endfield::Entity ent = g_ECS->CreateEntity(mask);
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
        std::vector<Endfield::SceneLoader::TemplateData> templates;
        if (Endfield::SceneLoader::LoadScene(filePath, *g_ECS, meshes, templates)) {
            for (size_t i = 0; i < meshes.size(); ++i) {
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

            g_SpawnTemplates.clear();
            for (const auto& t : templates) {
                g_SpawnTemplates.push_back({t.transform, t.bounds, t.mesh});
            }
            Endfield::VulkanBackend::LogToUnity("[PluginAPI] Scene loaded and " + std::to_string(g_SpawnTemplates.size()) + " submesh templates cached into InlineSubRendererComponent.");
        }
    }
}

ENDFIELD_API void AnimateNativeInstances(float time, float deltaTime)
{
    std::lock_guard<std::mutex> lock(g_NativeMutex);
    if (!g_ECS) return;

    Endfield::ComponentMask mask;
    mask.low = Endfield::MASK_TRANSFORM | Endfield::MASK_BOUNDS;
    auto chunks = g_ECS->QueryChunks(mask);

    for (auto chunk : chunks) {
        auto* transforms = g_ECS->GetComponentArray<Endfield::TransformComponent>(chunk, Endfield::BIT_TRANSFORM);
        auto* bounds     = g_ECS->GetComponentArray<Endfield::BoundsComponent>(chunk, Endfield::BIT_BOUNDS);
        
        if (transforms && bounds) {
            for (uint32_t i = 0; i < chunk->entityCount; ++i) {
                // 엔티티 1개 = 캐릭터 1개이므로 나눗셈 없이 entityId를 바로 고유 위상 오프셋으로 사용
                uint32_t entityId = chunk->entityArray[i].id;
                float phaseOffset = static_cast<float>(entityId) * 0.1f;

                float amplitude = 2.0f;
                float deltaY = amplitude * cosf(time + phaseOffset) * deltaTime;

                transforms[i].localToWorld[13] += deltaY;
                bounds[i].minBounds[1] += deltaY;
                bounds[i].maxBounds[1] += deltaY;
            }
        }
    }
}

ENDFIELD_API void ExecuteNativeFrameStep(
    float* viewMatrix,
    float* projMatrix,
    bool enableAnimation,
    float animTime,
    float animDeltaTime,
    Endfield::NativeBenchmarkStats* outStats)
{
    // 1. 카메라 행렬 업데이트 (P/Invoke #1 통합)
    if (viewMatrix && projMatrix) {
        UpdateCameraState(viewMatrix, projMatrix);
    }

    // 2. 네이티브 ECS 인스턴스 애니메이션 (P/Invoke #2 통합)
    if (enableAnimation && animDeltaTime > 0.0f) {
        AnimateNativeInstances(animTime, animDeltaTime);
    }

    // 3. 네이티브 Vulkan 렌더 루프 실행 (P/Invoke #3 통합)
    ExecuteNativeRenderLoop();

    // 4. 최신 프레임 벤치마크 통계 즉시 반환 (P/Invoke #4 통합)
    if (outStats) {
        *outStats = Endfield::BenchmarkManager::Instance().GetLatestFrameStats();
    }
}

} // extern "C"
