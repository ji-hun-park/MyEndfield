#include "Benchmark.h"
#include "ECS.h"
#include "Culling.h"
#include "SortKey.h"
#include "VulkanBackend.h"
#include "ThreadPool.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <cstdlib>

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

    // 1. Prepare an isolated ECS Manager & Register components
    ECSManager ecs;
    ComponentRegistry::RegisterComponent(0, sizeof(float*));
    ComponentRegistry::RegisterComponent(1, sizeof(TransformComponent));
    ComponentRegistry::RegisterComponent(2, sizeof(BoundsComponent));
    ComponentRegistry::RegisterComponent(3, sizeof(MeshComponent));

    ComponentMask mask;
    mask.low = MASK_STANDARD_RENDER;

    float spread = 300.0f;
    for (int i = 0; i < instanceCount; ++i) {
        Entity ent = ecs.CreateEntity(mask);

        float x = (((float)rand() / RAND_MAX) - 0.5f) * spread;
        float y = (((float)rand() / RAND_MAX) - 0.5f) * 20.0f;
        float z = (((float)rand() / RAND_MAX) - 0.5f) * spread;

        TransformComponent t{};
        // Identity matrix with translation
        t.localToWorld[0] = 1.0f;
        t.localToWorld[5] = 1.0f;
        t.localToWorld[10] = 1.0f;
        t.localToWorld[15] = 1.0f;
        t.localToWorld[12] = x;
        t.localToWorld[13] = y;
        t.localToWorld[14] = z;

        BoundsComponent b{};
        b.minBounds[0] = x - 1.0f; b.minBounds[1] = y - 1.0f; b.minBounds[2] = z - 1.0f;
        b.maxBounds[0] = x + 1.0f; b.maxBounds[1] = y + 1.0f; b.maxBounds[2] = z + 1.0f;

        MeshComponent m{};
        m.meshId = i % 4;
        m.subMeshIndex = 0;
        m.materialId = (i / 4) % 16;

        ecs.SetComponentData(ent, 1, t);
        ecs.SetComponentData(ent, 2, b);
        ecs.SetComponentData(ent, 3, m);
    }

    // 2. Setup standard Camera Frustum
    // Looking from (0, 50, -150) towards (0, 0, 0)
    float vp[16] = {
        1.3f, 0.0f, 0.0f, 0.0f,
        0.0f, 2.4f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 1.0f,
        0.0f, 0.0f, -1.0f, 0.0f
    };
    Frustum frustum;
    frustum.ExtractFromMatrix(vp);

    unsigned int numThreads = std::thread::hardware_concurrency();
    if (numThreads == 0) numThreads = 4;
    ThreadPool threadPool(numThreads);

    NativeBenchmarkStats accumStats{};

    // 3. Execution loops
    for (int iter = 0; iter < iterations; ++iter) {
        auto iterStart = std::chrono::high_resolution_clock::now();
        NativeBenchmarkStats iterStats{};
        iterStats.totalInstances = static_cast<uint32_t>(instanceCount);

        // [Phase 1: ECS Query & Data Access]
        std::vector<Chunk*> chunks;
        {
            ScopedTimer timer(&iterStats.ecsQueryTimeMs);
            chunks = ecs.QueryChunks(mask);
        }

        // [Phase 2: Parallel Frustum Culling & Instance Assembly]
        struct SimInstance {
            float mvpMatrix[16];
            SortKey sortKey;
            uint32_t subMeshIndex;
        };
        std::vector<SimInstance> visibleList;
        visibleList.reserve(instanceCount);

        uint32_t culledFrustumCount = 0;
        {
            ScopedTimer timer(&iterStats.frustumCullingTimeMs);

            for (auto* chunk : chunks) {
                auto* transforms = ecs.GetComponentArray<TransformComponent>(chunk, BIT_TRANSFORM);
                auto* bounds = ecs.GetComponentArray<BoundsComponent>(chunk, BIT_BOUNDS);
                auto* meshes = ecs.GetComponentArray<MeshComponent>(chunk, BIT_MESH);

                if (!transforms || !bounds || !meshes) continue;

                for (uint32_t i = 0; i < chunk->entityCount; ++i) {
                    AABB aabb = {
                        {bounds[i].minBounds[0], bounds[i].minBounds[1], bounds[i].minBounds[2]},
                        {bounds[i].maxBounds[0], bounds[i].maxBounds[1], bounds[i].maxBounds[2]}
                    };

                    bool isInside = true;
                    if (enableCulling) {
                        isInside = frustum.Intersects(aabb);
                    }

                    if (isInside) {
                        SimInstance inst;
                        for (int k = 0; k < 16; ++k) inst.mvpMatrix[k] = transforms[i].localToWorld[k];
                        
                        // Calculate depth from camera
                        float dx = transforms[i].localToWorld[12];
                        float dy = transforms[i].localToWorld[13] - 50.0f;
                        float dz = transforms[i].localToWorld[14] + 150.0f;
                        float dist = std::sqrt(dx*dx + dy*dy + dz*dz);

                        inst.sortKey = SortKeyBuilder::CreateOpaque(0, static_cast<uint16_t>(meshes[i].meshId), static_cast<uint16_t>(meshes[i].materialId), dist);
                        inst.subMeshIndex = static_cast<uint32_t>(meshes[i].subMeshIndex);
                        visibleList.push_back(inst);
                    } else {
                        culledFrustumCount++;
                    }
                }
            }
        }
        iterStats.culledFrustum = culledFrustumCount;
        iterStats.visibleInstances = static_cast<uint32_t>(visibleList.size());

        // [Phase 3: 64-bit SortKey Sorting]
        {
            ScopedTimer timer(&iterStats.sortingTimeMs);
            std::sort(visibleList.begin(), visibleList.end(), [](const SimInstance& a, const SimInstance& b) {
                return a.sortKey < b.sortKey;
            });
        }

        // [Phase 4: Parallel Worker Batching (0x7F7F7F7F Placeholder & Dynamic Buffer)]
        struct SimIntermediateCmd {
            uint32_t placeholder;
            uint32_t materialId;
            uint32_t meshId;
            SimInstance data;
        };
        std::vector<SimIntermediateCmd> intermediateCmds(visibleList.size());
        const uint32_t PLACEHOLDER = 0x7F7F7F7F;

        // Simulated dynamic buffer
        size_t dynamicAlign = 256;
        std::vector<uint8_t> simDynamicBuffer(visibleList.size() * dynamicAlign);

        {
            ScopedTimer timer(&iterStats.batchingTimeMs);
            int visibleCount = static_cast<int>(visibleList.size());
            if (visibleCount > 0) {
                int chunkSize = (visibleCount + numThreads - 1) / numThreads;

                for (unsigned int t = 0; t < numThreads; ++t) {
                    int startIdx = t * chunkSize;
                    int endIdx = (std::min)(visibleCount, startIdx + chunkSize);
                    if (startIdx >= endIdx) continue;

                    threadPool.Enqueue([startIdx, endIdx, &visibleList, &intermediateCmds, PLACEHOLDER]() {
                        for (int i = startIdx; i < endIdx; ++i) {
                            SimIntermediateCmd cmd;
                            cmd.placeholder = PLACEHOLDER;
                            cmd.materialId = visibleList[i].sortKey.materialID;
                            cmd.meshId = visibleList[i].sortKey.pipelineID;
                            cmd.data = visibleList[i];
                            intermediateCmds[i] = cmd;
                        }
                    });
                }
                threadPool.WaitAll();

                // Finalize: Redundant binding skip & Dynamic uniform buffer copy
                uint32_t lastBoundMaterial = 0xFFFFFFFF;
                for (int i = 0; i < visibleCount; ++i) {
                    if (intermediateCmds[i].materialId != lastBoundMaterial) {
                        intermediateCmds[i].placeholder = intermediateCmds[i].materialId;
                        lastBoundMaterial = intermediateCmds[i].materialId;
                    }
                    // Copy to dynamic buffer offset
                    size_t offset = i * dynamicAlign;
                    memcpy(simDynamicBuffer.data() + offset, &intermediateCmds[i].data, sizeof(SimInstance));
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

