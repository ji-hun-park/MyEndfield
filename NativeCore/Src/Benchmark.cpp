#define NOMINMAX
#include "Benchmark.h"
#include "ECS.h"
#include "Culling.h"
#include "SortKey.h"
#include "VulkanBackend.h"
#include "ThreadPool.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <iostream>
#include <cstdlib>
#include <limits>
#include <vector>

namespace Endfield {

BenchmarkManager::BenchmarkManager() {
    m_CurrentStats = {};
    m_LatestCompletedStats = {};
}

BenchmarkManager& BenchmarkManager::Instance() {
    static BenchmarkManager s_Instance;
    return s_Instance;
}

void BenchmarkManager::SetCullingOptions(bool enableFrustum, bool enableOcclusion) {
    std::lock_guard<std::mutex> lock(m_StatsMutex);
    m_EnableFrustum = enableFrustum;
    m_EnableOcclusion = enableOcclusion;
}

void BenchmarkManager::BeginFrame() {
    std::lock_guard<std::mutex> lock(m_StatsMutex);
    m_CurrentStats = {};
    m_FrameStartTime = std::chrono::high_resolution_clock::now();
}

void BenchmarkManager::EndFrame() {
    auto frameEndTime = std::chrono::high_resolution_clock::now();
    std::chrono::duration<float, std::milli> frameDuration = frameEndTime - m_FrameStartTime;
    
    std::lock_guard<std::mutex> lock(m_StatsMutex);
    m_CurrentStats.totalNativeFrameTimeMs = frameDuration.count();
    m_LatestCompletedStats = m_CurrentStats;
}

NativeBenchmarkStats BenchmarkManager::GetLatestFrameStats() {
    std::lock_guard<std::mutex> lock(m_StatsMutex);
    return m_LatestCompletedStats;
}

void BenchmarkManager::RunHeadlessBenchmark(int instanceCount, int iterations, bool enableCulling, NativeBenchmarkStats* outAverages) {
    if (!outAverages || instanceCount <= 0 || iterations <= 0) return;

    // 1. 격리된 ECSManager 준비 및 컴포넌트 등록
    ECSManager ecs;
    ComponentRegistry::RegisterComponent(0, sizeof(float*));
    ComponentRegistry::RegisterComponent(BIT_TRANSFORM,     sizeof(TransformComponent));
    ComponentRegistry::RegisterComponent(BIT_BOUNDS,        sizeof(BoundsComponent));
    ComponentRegistry::RegisterComponent(BIT_MESH,          sizeof(MeshComponent));
    ComponentRegistry::RegisterComponent(BIT_SUB_RENDERERS, sizeof(InlineSubRendererComponent));
    ComponentRegistry::RegisterComponent(BIT_VISIBILITY,    sizeof(uint32_t));

    // InlineSubRendererComponent를 포함하는 아키타입 마스크
    ComponentMask mask;
    mask.low = MASK_TRANSFORM | MASK_BOUNDS | MASK_SUB_RENDERERS | MASK_VISIBILITY;

    constexpr uint32_t SUBMESH_COUNT = 19;
    uint32_t characterCount = (static_cast<uint32_t>(instanceCount) + SUBMESH_COUNT - 1) / SUBMESH_COUNT;
    uint32_t remainingDraws = static_cast<uint32_t>(instanceCount);
    float spread = 300.0f;

    for (uint32_t c = 0; c < characterCount; ++c) {
        Entity ent = ecs.CreateEntity(mask);

        float x = (((float)rand() / RAND_MAX) - 0.5f) * spread;
        float y = (((float)rand() / RAND_MAX) - 0.5f) * 20.0f;
        float z = (((float)rand() / RAND_MAX) - 0.5f) * spread;

        TransformComponent t{};
        t.localToWorld[0]  = 1.0f;
        t.localToWorld[5]  = 1.0f;
        t.localToWorld[10] = 1.0f;
        t.localToWorld[15] = 1.0f;
        t.localToWorld[12] = x;
        t.localToWorld[13] = y;
        t.localToWorld[14] = z;

        BoundsComponent b{};
        b.minBounds[0] = x - 1.0f; b.minBounds[1] = y - 1.0f; b.minBounds[2] = z - 1.0f;
        b.maxBounds[0] = x + 1.0f; b.maxBounds[1] = y + 1.0f; b.maxBounds[2] = z + 1.0f;

        // 포인터 점프 없이 청크 내부 컬럼에 19개 서브메시 정보를 직접 인라인 수납
        InlineSubRendererComponent subComp{};
        subComp.meshId = 0;
        subComp.subMeshCount = (std::min)(remainingDraws, SUBMESH_COUNT);
        for (uint32_t s = 0; s < subComp.subMeshCount; ++s) {
            subComp.subMeshIndices[s] = static_cast<uint16_t>(s);
            subComp.materialIds[s]    = static_cast<uint16_t>(s);
        }
        remainingDraws -= subComp.subMeshCount;

        ecs.SetComponentData(ent, BIT_TRANSFORM,     t);
        ecs.SetComponentData(ent, BIT_BOUNDS,        b);
        ecs.SetComponentData(ent, BIT_SUB_RENDERERS, subComp);
    }

    // 2. 카메라 프러스텀 및 15개 워커 스레드 풀 / 컬링 시스템 초기화
    float vp[16] = {
        1.3f, 0.0f, 0.0f, 0.0f,
        0.0f, 2.4f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 1.0f,
        0.0f, 0.0f, -1.0f, 0.0f
    };
    Frustum frustum;
    frustum.ExtractFromMatrix(vp);

    unsigned int hwThreads = std::thread::hardware_concurrency();
    unsigned int numWorkers = (hwThreads > 1) ? (hwThreads - 1) : 4; // 15 Workers + 1 Main/Render Thread
    ThreadPool threadPool(numWorkers);

    CullingSystem cullingSys;
    cullingSys.Initialize(numWorkers, &threadPool, false);

    const bool runOcclusion = enableCulling && m_EnableOcclusion;
    NativeBenchmarkStats accumStats{};

    struct alignas(16) SimInstance {
        float mvpMatrix[16];
        SortKey sortKey;
        uint32_t subMeshIndex;
        uint32_t _pad[3];
    };

    // 3. 반복 측정 루프
    for (int iter = 0; iter < iterations; ++iter) {
        auto iterStart = std::chrono::high_resolution_clock::now();
        NativeBenchmarkStats iterStats{};
        iterStats.totalInstances = static_cast<uint32_t>(instanceCount);

        // [Phase 1: ECS Query & SoA Chunk Access]
        std::vector<Chunk*> chunks;
        {
            ScopedTimer timer(&iterStats.ecsQueryTimeMs);
            chunks = ecs.QueryChunks(mask);
        }

        // [Phase 2: 15-Worker Multi-View Frustum Culling & Inline SubRenderer Expansion]
        std::vector<SimInstance> frustumSurvivors;
        frustumSurvivors.reserve(instanceCount);
        uint32_t culledFrustumDraws = 0;

        {
            ScopedTimer timer(&iterStats.frustumCullingTimeMs);
            ecs.AcquireWorkerReadFence();

            constexpr uint32_t CULL_BATCH_SIZE = 256;
            uint32_t numCullChunks = (characterCount + CULL_BATCH_SIZE - 1) / CULL_BATCH_SIZE;

            std::vector<AABB> entityAABBs(characterCount);
            std::vector<AABB> chunkAABBs(numCullChunks);

            for (uint32_t c = 0; c < numCullChunks; ++c) {
                chunkAABBs[c].minBounds[0] = std::numeric_limits<float>::max();
                chunkAABBs[c].minBounds[1] = std::numeric_limits<float>::max();
                chunkAABBs[c].minBounds[2] = std::numeric_limits<float>::max();
                chunkAABBs[c].maxBounds[0] = std::numeric_limits<float>::lowest();
                chunkAABBs[c].maxBounds[1] = std::numeric_limits<float>::lowest();
                chunkAABBs[c].maxBounds[2] = std::numeric_limits<float>::lowest();
            }

            uint32_t writeIdx = 0;
            for (auto* chunk : chunks) {
                auto* bounds = ecs.GetComponentArray<BoundsComponent>(chunk, BIT_BOUNDS);
                if (!bounds) continue;

                for (uint32_t i = 0; i < chunk->entityCount; ++i) {
                    AABB box = {
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

            std::vector<uint32_t> visMasks;
            if (enableCulling) {
                cullingSys.PerformMultiViewFrustumCullingParallel(
                    &frustum,
                    1,
                    chunkAABBs.data(),
                    CULL_BATCH_SIZE,
                    entityAABBs,
                    visMasks
                );
            }

            uint32_t readIdx = 0;
            for (auto* chunk : chunks) {
                auto* transforms   = ecs.GetComponentArray<TransformComponent>(chunk, BIT_TRANSFORM);
                auto* subRenderers = ecs.GetComponentArray<InlineSubRendererComponent>(chunk, BIT_SUB_RENDERERS);
                auto* visColumn    = ecs.GetComponentArray<uint32_t>(chunk, BIT_VISIBILITY);

                if (!transforms || !subRenderers) continue;

                for (uint32_t i = 0; i < chunk->entityCount; ++i, ++readIdx) {
                    uint32_t vis = enableCulling ? visMasks[readIdx] : 1u;
                    if (visColumn) visColumn[i] = vis;

                    const InlineSubRendererComponent& subComp = subRenderers[i];

                    if ((vis & (1u << 0)) != 0u) {
                        float dx = transforms[i].localToWorld[12];
                        float dy = transforms[i].localToWorld[13] - 50.0f;
                        float dz = transforms[i].localToWorld[14] + 150.0f;
                        float dist = std::sqrt(dx * dx + dy * dy + dz * dz);

                        SimInstance inst{};
                        for (int k = 0; k < 16; ++k) inst.mvpMatrix[k] = transforms[i].localToWorld[k];

                        for (uint32_t s = 0; s < subComp.subMeshCount; ++s) {
                            inst.sortKey = SortKeyBuilder::CreateOpaque(
                                0u,
                                1u,
                                subComp.materialIds[s],
                                dist,
                                DEFAULT_MAX_SORT_DISTANCE,
                                static_cast<uint16_t>(subComp.meshId),
                                subComp.subMeshIndices[s]
                            );
                            inst.subMeshIndex = static_cast<uint32_t>(subComp.subMeshIndices[s]);
                            frustumSurvivors.push_back(inst);
                        }
                    } else {
                        culledFrustumDraws += subComp.subMeshCount;
                    }
                }
            }

            ecs.ReleaseWorkerReadFence();
        }
        iterStats.culledFrustum = culledFrustumDraws;

        // [Phase 2.5: 3-Stage Software Occlusion Culling (선택 활성화 시)]
        std::vector<SimInstance> visibleList;
        if (runOcclusion && !frustumSurvivors.empty()) {
            ScopedTimer timer(&iterStats.occlusionCullingTimeMs);

            std::vector<OccluderMesh> proxyOccluders(1);
            OccluderMesh& wall = proxyOccluders[0];
            for (int q = 0; q < 4; ++q) {
                float ox = (static_cast<float>(q) - 1.5f) * 35.0f;
                float oz = (q % 2 == 0) ? -15.0f : 15.0f;
                uint32_t baseVtx = static_cast<uint32_t>(wall.vertices.size());
                wall.vertices.push_back({ ox - 14.0f, -5.0f, oz });
                wall.vertices.push_back({ ox + 14.0f, -5.0f, oz });
                wall.vertices.push_back({ ox + 14.0f, 18.0f, oz });
                wall.vertices.push_back({ ox - 14.0f, 18.0f, oz });
                wall.indices.insert(wall.indices.end(), {
                    baseVtx + 0, baseVtx + 1, baseVtx + 2,
                    baseVtx + 0, baseVtx + 2, baseVtx + 3,
                    baseVtx + 0, baseVtx + 2, baseVtx + 1,
                    baseVtx + 0, baseVtx + 3, baseVtx + 2
                });
            }
            std::memset(wall.localToWorld, 0, sizeof(wall.localToWorld));
            wall.localToWorld[0] = wall.localToWorld[5] = wall.localToWorld[10] = wall.localToWorld[15] = 1.0f;

            cullingSys.BatchOccluders(proxyOccluders, vp, 1280.0f, 720.0f);
            cullingSys.RasterizeTilesParallel(1280.0f, 720.0f);

            std::vector<bool> occVis;
            AABB defaultBounds = { {-1.0f, -1.0f, -1.0f}, {1.0f, 1.0f, 1.0f} };
            cullingSys.PerformOcclusionTestParallel(
                vp,
                reinterpret_cast<const float*>(frustumSurvivors.data()),
                static_cast<int>(frustumSurvivors.size()),
                sizeof(SimInstance),
                defaultBounds,
                1280.0f,
                720.0f,
                occVis
            );

            visibleList.reserve(frustumSurvivors.size());
            for (size_t i = 0; i < frustumSurvivors.size(); ++i) {
                if (i < occVis.size() && occVis[i]) {
                    visibleList.push_back(frustumSurvivors[i]);
                }
            }
            iterStats.culledOcclusion = static_cast<uint32_t>(frustumSurvivors.size() - visibleList.size());
        } else {
            visibleList = std::move(frustumSurvivors);
        }

        iterStats.visibleInstances = static_cast<uint32_t>(visibleList.size());

        // [Phase 3: 128-bit SortKey Branchless SIMD std::sort]
        {
            ScopedTimer timer(&iterStats.sortingTimeMs);
            std::sort(visibleList.begin(), visibleList.end(), [](const SimInstance& a, const SimInstance& b) noexcept {
                return SortKey::LessSIMD(a.sortKey, b.sortKey);
            });
        }

        // [Phase 4: 15-Worker Parallel Batching (0xF7F7F7F7 Sentinel & CAS Persistent Buffer Copy)]
        struct SimIntermediateCmd {
            uint32_t placeholder;
            uint32_t materialId;
            uint32_t meshId;
            uint32_t dynamicOffset;
            SimInstance data;
        };
        std::vector<SimIntermediateCmd> intermediateCmds(visibleList.size());
        constexpr uint32_t PLACEHOLDER = 0xF7F7F7F7;

        size_t dynamicAlign = 256;
        std::vector<uint8_t> simDynamicBuffer(visibleList.size() * dynamicAlign);
        uint8_t* mappedPtr = simDynamicBuffer.data();
        std::atomic<uint32_t> atomicWriteSlot{0u};

        {
            ScopedTimer timer(&iterStats.batchingTimeMs);
            int visibleCount = static_cast<int>(visibleList.size());
            if (visibleCount > 0) {
                int chunkSize = (visibleCount + static_cast<int>(numWorkers) - 1) / static_cast<int>(numWorkers);

                for (unsigned int t = 0; t < numWorkers; ++t) {
                    int startIdx = static_cast<int>(t) * chunkSize;
                    int endIdx = (std::min)(visibleCount, startIdx + chunkSize);
                    if (startIdx >= endIdx) continue;
                    uint32_t batchCount = static_cast<uint32_t>(endIdx - startIdx);

                    threadPool.Enqueue([startIdx, endIdx, batchCount, &visibleList, &intermediateCmds, &atomicWriteSlot, mappedPtr, dynamicAlign, PLACEHOLDER]() {
                        uint32_t claimedBaseSlot = atomicWriteSlot.load(std::memory_order_relaxed);
                        while (!atomicWriteSlot.compare_exchange_weak(
                            claimedBaseSlot,
                            claimedBaseSlot + batchCount,
                            std::memory_order_acq_rel,
                            std::memory_order_relaxed))
                        {
                        }

                        for (int i = startIdx; i < endIdx; ++i) {
                            uint32_t slotIdx = claimedBaseSlot + static_cast<uint32_t>(i - startIdx);
                            uint32_t offset = slotIdx * static_cast<uint32_t>(dynamicAlign);

                            std::memcpy(mappedPtr + offset, &visibleList[i], sizeof(SimInstance));

                            SimIntermediateCmd cmd;
                            cmd.placeholder   = PLACEHOLDER;
                            cmd.materialId    = visibleList[i].sortKey.materialID;
                            cmd.meshId        = visibleList[i].sortKey.meshID;
                            cmd.dynamicOffset = offset;
                            cmd.data          = visibleList[i];
                            intermediateCmds[i] = cmd;
                        }
                    });
                }
                threadPool.WaitAll();

                uint32_t lastBoundMaterial = 0xFFFFFFFFu;
                for (int i = 0; i < visibleCount; ++i) {
                    if (intermediateCmds[i].placeholder == PLACEHOLDER) {
                        if (intermediateCmds[i].materialId != lastBoundMaterial) {
                            lastBoundMaterial = intermediateCmds[i].materialId;
                        }
                        intermediateCmds[i].placeholder = lastBoundMaterial;
                    }
                }
            }
        }

        auto iterEnd = std::chrono::high_resolution_clock::now();
        std::chrono::duration<float, std::milli> iterDuration = iterEnd - iterStart;
        iterStats.totalNativeFrameTimeMs = iterDuration.count();

        // Accumulate
        accumStats.totalInstances = iterStats.totalInstances;
        accumStats.visibleInstances += iterStats.visibleInstances;
        accumStats.culledFrustum += iterStats.culledFrustum;
        accumStats.culledOcclusion += iterStats.culledOcclusion;
        accumStats.ecsQueryTimeMs += iterStats.ecsQueryTimeMs;
        accumStats.frustumCullingTimeMs += iterStats.frustumCullingTimeMs;
        accumStats.occlusionCullingTimeMs += iterStats.occlusionCullingTimeMs;
        accumStats.sortingTimeMs += iterStats.sortingTimeMs;
        accumStats.batchingTimeMs += iterStats.batchingTimeMs;
        accumStats.renderSubmitTimeMs += iterStats.renderSubmitTimeMs;
        accumStats.totalNativeFrameTimeMs += iterStats.totalNativeFrameTimeMs;
    }

    cullingSys.Shutdown();

    // Average
    outAverages->totalInstances = accumStats.totalInstances;
    outAverages->visibleInstances = accumStats.visibleInstances / iterations;
    outAverages->culledFrustum = accumStats.culledFrustum / iterations;
    outAverages->culledOcclusion = accumStats.culledOcclusion / iterations;
    outAverages->ecsQueryTimeMs = accumStats.ecsQueryTimeMs / iterations;
    outAverages->frustumCullingTimeMs = accumStats.frustumCullingTimeMs / iterations;
    outAverages->occlusionCullingTimeMs = accumStats.occlusionCullingTimeMs / iterations;
    outAverages->sortingTimeMs = accumStats.sortingTimeMs / iterations;
    outAverages->batchingTimeMs = accumStats.batchingTimeMs / iterations;
    outAverages->renderSubmitTimeMs = accumStats.renderSubmitTimeMs / iterations;
    outAverages->totalNativeFrameTimeMs = accumStats.totalNativeFrameTimeMs / iterations;
}

} // namespace Endfield
