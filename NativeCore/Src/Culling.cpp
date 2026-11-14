#define NOMINMAX
#include "Culling.h"
#include "VulkanBackend.h"
#include <algorithm>
#include <cmath>
#include <thread>
#include <future>
#include <iostream>
#include <limits>
#include <immintrin.h> // SSE2 / AVX2 SIMD Intrinsics

namespace Endfield {

template<typename Func>
void CullingSystem::DispatchParallel(uint32_t jobCount, Func&& task) {
    if (jobCount == 0) return;

    if (m_ThreadPool != nullptr) {
        for (uint32_t i = 0; i < jobCount; ++i) {
            m_ThreadPool->Enqueue([i, &task]() {
                task(static_cast<int>(i));
            });
        }
        m_ThreadPool->WaitAll();
    } else {
        std::vector<std::future<void>> futures;
        futures.reserve(jobCount);
        for (uint32_t i = 0; i < jobCount; ++i) {
            futures.push_back(std::async(std::launch::async, task, static_cast<int>(i)));
        }
        for (auto& fut : futures) {
            fut.wait();
        }
    }
}

void Frustum::ExtractFromMatrix(const float* vpMatrix) {
    // Left
    planes[0][0] = vpMatrix[3] + vpMatrix[0];
    planes[0][1] = vpMatrix[7] + vpMatrix[4];
    planes[0][2] = vpMatrix[11] + vpMatrix[8];
    planes[0][3] = vpMatrix[15] + vpMatrix[12];
    // Right
    planes[1][0] = vpMatrix[3] - vpMatrix[0];
    planes[1][1] = vpMatrix[7] - vpMatrix[4];
    planes[1][2] = vpMatrix[11] - vpMatrix[8];
    planes[1][3] = vpMatrix[15] - vpMatrix[12];
    // Bottom
    planes[2][0] = vpMatrix[3] + vpMatrix[1];
    planes[2][1] = vpMatrix[7] + vpMatrix[5];
    planes[2][2] = vpMatrix[11] + vpMatrix[9];
    planes[2][3] = vpMatrix[15] + vpMatrix[13];
    // Top
    planes[3][0] = vpMatrix[3] - vpMatrix[1];
    planes[3][1] = vpMatrix[7] - vpMatrix[5];
    planes[3][2] = vpMatrix[11] - vpMatrix[9];
    planes[3][3] = vpMatrix[15] - vpMatrix[13];
    // Near (Vulkan/DirectX [0, 1] depth range)
    planes[4][0] = vpMatrix[2];
    planes[4][1] = vpMatrix[6];
    planes[4][2] = vpMatrix[10];
    planes[4][3] = vpMatrix[14];
    // Far
    planes[5][0] = vpMatrix[3] - vpMatrix[2];
    planes[5][1] = vpMatrix[7] - vpMatrix[6];
    planes[5][2] = vpMatrix[11] - vpMatrix[10];
    planes[5][3] = vpMatrix[15] - vpMatrix[14];

    for (int i = 0; i < 6; i++) {
        float length = std::sqrt(planes[i][0] * planes[i][0] +
                                 planes[i][1] * planes[i][1] +
                                 planes[i][2] * planes[i][2]);
        if (length > 0.0001f) {
            planes[i][0] /= length;
            planes[i][1] /= length;
            planes[i][2] /= length;
            planes[i][3] /= length;
        }
    }
}

bool Frustum::Intersects(const AABB& aabb) const {
    for (int i = 0; i < 6; ++i) {
        float px = (planes[i][0] > 0.0f) ? aabb.maxBounds[0] : aabb.minBounds[0];
        float py = (planes[i][1] > 0.0f) ? aabb.maxBounds[1] : aabb.minBounds[1];
        float pz = (planes[i][2] > 0.0f) ? aabb.maxBounds[2] : aabb.minBounds[2];

        float dot = planes[i][0] * px + planes[i][1] * py + planes[i][2] * pz;
        if (dot < -planes[i][3]) {
            return false;
        }
    }
    return true;
}

// -------------------------------------------------------------

void CullingSystem::Initialize(uint32_t numWorkers, ThreadPool* threadPool, bool useReversedZ) {
    m_NumWorkers = numWorkers > 0 ? numWorkers : std::thread::hardware_concurrency();
    if (m_NumWorkers == 0) m_NumWorkers = 4;
    m_ThreadPool = threadPool;
    m_UseReversedZ = useReversedZ;

    float clearDepth = m_UseReversedZ ? 0.0f : 1.0f;
    m_DepthBuffer.assign(DEPTH_RES_X * DEPTH_RES_Y, clearDepth);

    VulkanBackend::LogToUnity("[CullingSystem] Initialized with " + std::to_string(m_NumWorkers) + " parallel workers.");
}

void CullingSystem::Shutdown() {
    m_DepthBuffer.clear();
    m_ScreenTriangles.clear();
    VulkanBackend::LogToUnity("[CullingSystem] Shutdown.");
}

void CullingSystem::PerformFrustumCullingParallel(const Frustum& frustum, const std::vector<AABB>& aabbs, std::vector<bool>& outVisibility) {
    if (aabbs.empty()) return;

    const size_t total = aabbs.size();
    // std::vector<bool> 비트 압축으로 인한 멀티스레드 Data Race를 방지하기 위해
    // 워커들은 독립된 1바이트(uint8_t) 배열에 기록합니다.
    std::vector<uint8_t> tempVis(total, 0);

    uint32_t activeWorkers = (std::min)(m_NumWorkers, static_cast<uint32_t>(total));
    auto cullTask = [&](int workerID) {
        size_t chunk = total / activeWorkers;
        size_t start = workerID * chunk;
        size_t end = (static_cast<uint32_t>(workerID) == activeWorkers - 1) ? total : start + chunk;

        for (size_t i = start; i < end; ++i) {
            tempVis[i] = frustum.Intersects(aabbs[i]) ? 1 : 0;
        }
    };

    DispatchParallel(activeWorkers, cullTask);

    outVisibility.resize(total);
    for (size_t i = 0; i < total; ++i) {
        outVisibility[i] = (tempVis[i] != 0);
    }
}

void CullingSystem::PerformMultiViewFrustumCullingParallel(
    const Frustum* frustums,
    uint32_t viewCount,
    const AABB* chunkAABBs,
    uint32_t entitiesPerChunk,
    const std::vector<AABB>& entityAABBs,
    std::vector<uint32_t>& outVisibilityMasks)
{
    if (entityAABBs.empty() || viewCount == 0 || !frustums) return;
    if (viewCount > 32) viewCount = 32; // 최대 32개 뷰 비트마스크

    const size_t totalEntities = entityAABBs.size();
    outVisibilityMasks.assign(totalEntities, 0u);

    const uint32_t batchSize = (entitiesPerChunk > 0) ? entitiesPerChunk : 256;
    const uint32_t numChunks = static_cast<uint32_t>((totalEntities + batchSize - 1) / batchSize);
    const uint32_t activeWorkers = (std::min)(m_NumWorkers, numChunks);

    // 데이터 흐름을 뒤집은 루프: for (Chunk) -> ChunkAABB 사전 컬링 -> for (Entity) -> Views 비트마스크 기록
    auto multiViewTask = [&](int workerID) {
        uint32_t chunksPerWorker = numChunks / activeWorkers;
        uint32_t startChunk = workerID * chunksPerWorker;
        uint32_t endChunk = (static_cast<uint32_t>(workerID) == activeWorkers - 1) ? numChunks : startChunk + chunksPerWorker;

        for (uint32_t c = startChunk; c < endChunk; ++c) {
            uint32_t activeViewMaskForChunk = (viewCount == 32) ? 0xFFFFFFFFu : ((1u << viewCount) - 1u);

            // 월드 청크 AABB 사전 컬링: 화면 밖 청크를 통째로 스킵
            if (chunkAABBs != nullptr) {
                activeViewMaskForChunk = 0u;
                for (uint32_t v = 0; v < viewCount; ++v) {
                    if (frustums[v].Intersects(chunkAABBs[c])) {
                        activeViewMaskForChunk |= (1u << v);
                    }
                }
                if (activeViewMaskForChunk == 0u) continue;
            }

            size_t startIdx = static_cast<size_t>(c) * batchSize;
            size_t endIdx = (std::min)(startIdx + batchSize, totalEntities);

            for (size_t i = startIdx; i < endIdx; ++i) {
                uint32_t visMask = 0u;
                const AABB& aabb = entityAABBs[i];

                for (uint32_t v = 0; v < viewCount; ++v) {
                    if ((activeViewMaskForChunk & (1u << v)) && frustums[v].Intersects(aabb)) {
                        visMask |= (1u << v);
                    }
                }
                outVisibilityMasks[i] = visMask;
            }
        }
    };

    DispatchParallel(activeWorkers, multiViewTask);
}

void CullingSystem::BatchOccluders(const std::vector<OccluderMesh>& occluders, const float* vpMatrix, float screenWidth, float screenHeight) {
    m_ScreenTriangles.clear();
    if (occluders.empty() || !vpMatrix) return;

    auto transformClip = [](const float* mvp, const Vector3& p, float& outW) -> Vector3 {
        Vector3 res;
        res.x = mvp[0] * p.x + mvp[4] * p.y + mvp[8]  * p.z + mvp[12];
        res.y = mvp[1] * p.x + mvp[5] * p.y + mvp[9]  * p.z + mvp[13];
        res.z = mvp[2] * p.x + mvp[6] * p.y + mvp[10] * p.z + mvp[14];
        outW  = mvp[3] * p.x + mvp[7] * p.y + mvp[11] * p.z + mvp[15];
        return res;
    };

    const __m128 halfVec = _mm_set1_ps(0.5f);
    const __m128 oneVec  = _mm_set1_ps(1.0f);
    const __m128 swVec   = _mm_set1_ps(screenWidth);
    const __m128 shVec   = _mm_set1_ps(screenHeight);
    const __m128 nearW   = _mm_set1_ps(0.0001f);

    for (const auto& mesh : occluders) {
        // 메시별 MVP 행렬 사전 결합 (정점마다 행렬 곱을 2번 하던 오버헤드 제거)
        float mvp[16];
        for (int c = 0; c < 4; ++c) {
            for (int r = 0; r < 4; ++r) {
                mvp[c * 4 + r] =
                    vpMatrix[0 * 4 + r] * mesh.localToWorld[c * 4 + 0] +
                    vpMatrix[1 * 4 + r] * mesh.localToWorld[c * 4 + 1] +
                    vpMatrix[2 * 4 + r] * mesh.localToWorld[c * 4 + 2] +
                    vpMatrix[3 * 4 + r] * mesh.localToWorld[c * 4 + 3];
            }
        }

        size_t triCount = mesh.indices.size() / 3;
        size_t t = 0;

        // 4-Wide SSE SIMD 배치 처리 (삼각형 4개씩 원근 분할, 백페이스 컬링, 뷰포트 매핑 동시 수행)
        for (; t + 3 < triCount; t += 4) {
            alignas(16) float cx0[4], cy0[4], cz0[4], cw0[4];
            alignas(16) float cx1[4], cy1[4], cz1[4], cw1[4];
            alignas(16) float cx2[4], cy2[4], cz2[4], cw2[4];

            for (int k = 0; k < 4; ++k) {
                size_t baseIdx = (t + k) * 3;
                Vector3 c0 = transformClip(mvp, mesh.vertices[mesh.indices[baseIdx]],     cw0[k]);
                Vector3 c1 = transformClip(mvp, mesh.vertices[mesh.indices[baseIdx + 1]], cw1[k]);
                Vector3 c2 = transformClip(mvp, mesh.vertices[mesh.indices[baseIdx + 2]], cw2[k]);

                cx0[k] = c0.x; cy0[k] = c0.y; cz0[k] = c0.z;
                cx1[k] = c1.x; cy1[k] = c1.y; cz1[k] = c1.z;
                cx2[k] = c2.x; cy2[k] = c2.y; cz2[k] = c2.z;
            }

            __m128 vw0 = _mm_load_ps(cw0);
            __m128 vw1 = _mm_load_ps(cw1);
            __m128 vw2 = _mm_load_ps(cw2);

            // 하나라도 Near Plane(w <= 0.0001f) 뒤에 있으면 원근 나눗셈 폭발 방지를 위해 클리핑
            __m128 validW = _mm_and_ps(_mm_and_ps(_mm_cmpgt_ps(vw0, nearW), _mm_cmpgt_ps(vw1, nearW)), _mm_cmpgt_ps(vw2, nearW));

            __m128 ndcX0 = _mm_div_ps(_mm_load_ps(cx0), vw0);
            __m128 ndcY0 = _mm_div_ps(_mm_load_ps(cy0), vw0);
            __m128 ndcZ0 = _mm_div_ps(_mm_load_ps(cz0), vw0);

            __m128 ndcX1 = _mm_div_ps(_mm_load_ps(cx1), vw1);
            __m128 ndcY1 = _mm_div_ps(_mm_load_ps(cy1), vw1);
            __m128 ndcZ1 = _mm_div_ps(_mm_load_ps(cz1), vw1);

            __m128 ndcX2 = _mm_div_ps(_mm_load_ps(cx2), vw2);
            __m128 ndcY2 = _mm_div_ps(_mm_load_ps(cy2), vw2);
            __m128 ndcZ2 = _mm_div_ps(_mm_load_ps(cz2), vw2);

            // SIMD Backface Culling: crossZ = (x1 - x0)*(y2 - y0) - (y1 - y0)*(x2 - x0) >= 0
            __m128 dx10 = _mm_sub_ps(ndcX1, ndcX0);
            __m128 dy20 = _mm_sub_ps(ndcY2, ndcY0);
            __m128 dy10 = _mm_sub_ps(ndcY1, ndcY0);
            __m128 dx20 = _mm_sub_ps(ndcX2, ndcX0);
            __m128 crossZ = _mm_sub_ps(_mm_mul_ps(dx10, dy20), _mm_mul_ps(dy10, dx20));
            __m128 frontMask = _mm_and_ps(validW, _mm_cmpge_ps(crossZ, _mm_setzero_ps()));

            int mask = _mm_movemask_ps(frontMask);
            if (mask == 0) continue;

            // SIMD Screen Space Mapping
            __m128 sx0 = _mm_mul_ps(_mm_mul_ps(_mm_add_ps(ndcX0, oneVec), halfVec), swVec);
            __m128 sy0 = _mm_mul_ps(_mm_mul_ps(_mm_sub_ps(oneVec, ndcY0), halfVec), shVec);
            __m128 sx1 = _mm_mul_ps(_mm_mul_ps(_mm_add_ps(ndcX1, oneVec), halfVec), swVec);
            __m128 sy1 = _mm_mul_ps(_mm_mul_ps(_mm_sub_ps(oneVec, ndcY1), halfVec), shVec);
            __m128 sx2 = _mm_mul_ps(_mm_mul_ps(_mm_add_ps(ndcX2, oneVec), halfVec), swVec);
            __m128 sy2 = _mm_mul_ps(_mm_mul_ps(_mm_sub_ps(oneVec, ndcY2), halfVec), shVec);

            alignas(16) float outSX0[4], outSY0[4], outSZ0[4];
            alignas(16) float outSX1[4], outSY1[4], outSZ1[4];
            alignas(16) float outSX2[4], outSY2[4], outSZ2[4];

            _mm_store_ps(outSX0, sx0); _mm_store_ps(outSY0, sy0); _mm_store_ps(outSZ0, ndcZ0);
            _mm_store_ps(outSX1, sx1); _mm_store_ps(outSY1, sy1); _mm_store_ps(outSZ1, ndcZ1);
            _mm_store_ps(outSX2, sx2); _mm_store_ps(outSY2, sy2); _mm_store_ps(outSZ2, ndcZ2);

            for (int k = 0; k < 4; ++k) {
                if (mask & (1 << k)) {
                    m_ScreenTriangles.push_back({
                        { outSX0[k], outSY0[k], outSZ0[k] },
                        { outSX1[k], outSY1[k], outSZ1[k] },
                        { outSX2[k], outSY2[k], outSZ2[k] }
                    });
                }
            }
        }

        // 나머지 삼각형(4개 미만 Tail) 스칼라 처리
        for (; t < triCount; ++t) {
            size_t baseIdx = t * 3;
            float w0, w1, w2;
            Vector3 clip0 = transformClip(mvp, mesh.vertices[mesh.indices[baseIdx]],     w0);
            Vector3 clip1 = transformClip(mvp, mesh.vertices[mesh.indices[baseIdx + 1]], w1);
            Vector3 clip2 = transformClip(mvp, mesh.vertices[mesh.indices[baseIdx + 2]], w2);

            if (w0 <= 0.0001f || w1 <= 0.0001f || w2 <= 0.0001f) continue;

            Vector3 ndc0 = { clip0.x / w0, clip0.y / w0, clip0.z / w0 };
            Vector3 ndc1 = { clip1.x / w1, clip1.y / w1, clip1.z / w1 };
            Vector3 ndc2 = { clip2.x / w2, clip2.y / w2, clip2.z / w2 };

            float crossZ = (ndc1.x - ndc0.x) * (ndc2.y - ndc0.y) - (ndc1.y - ndc0.y) * (ndc2.x - ndc0.x);
            if (crossZ < 0.0f) continue;

            auto toScreen = [screenWidth, screenHeight](const Vector3& ndc) -> Vector3 {
                return {
                    (ndc.x + 1.0f) * 0.5f * screenWidth,
                    (1.0f - ndc.y) * 0.5f * screenHeight,
                    ndc.z
                };
            };

            m_ScreenTriangles.push_back({ toScreen(ndc0), toScreen(ndc1), toScreen(ndc2) });
        }
    }
}

void CullingSystem::RasterizeTilesParallel(float screenWidth, float screenHeight) {
    if (m_DepthBuffer.size() != DEPTH_RES_X * DEPTH_RES_Y) {
        m_DepthBuffer.resize(DEPTH_RES_X * DEPTH_RES_Y);
    }

    float clearDepth = m_UseReversedZ ? 0.0f : 1.0f;
    std::fill(m_DepthBuffer.begin(), m_DepthBuffer.end(), clearDepth);

    if (m_ScreenTriangles.empty()) return;

    const float tileW = static_cast<float>(DEPTH_RES_X) / TILE_COLS;
    const float tileH = static_cast<float>(DEPTH_RES_Y) / TILE_ROWS;

    float scaleX = DEPTH_RES_X / screenWidth;
    float scaleY = DEPTH_RES_Y / screenHeight;

    // 4개의 비닝 잡이 8개 화면 타일에 대해 락 없이 총 32개(4x8)의 타일 리스트 생성
    struct alignas(64) BinningJobList {
        std::vector<int> tileTriangles[NUM_TILES];
    };
    std::vector<BinningJobList> workerLists(NUM_BINNING_JOBS);

    auto binningTask = [&](int jobID) {
        size_t totalTris = m_ScreenTriangles.size();
        size_t chunk = (totalTris + NUM_BINNING_JOBS - 1) / NUM_BINNING_JOBS;
        size_t start = static_cast<size_t>(jobID) * chunk;
        size_t end = (std::min)(start + chunk, totalTris);

        for (size_t i = start; i < end; ++i) {
            const auto& tri = m_ScreenTriangles[i];

            float minX = (std::min)(tri.v0.x, (std::min)(tri.v1.x, tri.v2.x)) * scaleX;
            float maxX = (std::max)(tri.v0.x, (std::max)(tri.v1.x, tri.v2.x)) * scaleX;
            float minY = (std::min)(tri.v0.y, (std::min)(tri.v1.y, tri.v2.y)) * scaleY;
            float maxY = (std::max)(tri.v0.y, (std::max)(tri.v1.y, tri.v2.y)) * scaleY;

            if (maxX < 0.0f || minX >= DEPTH_RES_X || maxY < 0.0f || minY >= DEPTH_RES_Y) continue;

            minX = (std::max)(0.0f, minX);
            minY = (std::max)(0.0f, minY);
            maxX = (std::min)(static_cast<float>(DEPTH_RES_X - 1), maxX);
            maxY = (std::min)(static_cast<float>(DEPTH_RES_Y - 1), maxY);

            int startTileX = static_cast<int>(minX / tileW);
            int endTileX   = static_cast<int>(maxX / tileW);
            int startTileY = static_cast<int>(minY / tileH);
            int endTileY   = static_cast<int>(maxY / tileH);

            for (int ty = startTileY; ty <= endTileY; ++ty) {
                for (int tx = startTileX; tx <= endTileX; ++tx) {
                    int tileIndex = ty * TILE_COLS + tx;
                    if (tileIndex >= 0 && tileIndex < NUM_TILES) {
                        workerLists[jobID].tileTriangles[tileIndex].push_back(static_cast<int>(i));
                    }
                }
            }
        }
    };

    DispatchParallel(NUM_BINNING_JOBS, binningTask);

    // 8개의 렌더 잡이 8개 화면 타일을 1개씩(t = 0..7) 전담하여 락프리 래스터화 수행
    auto rasterTask = [&](int tileIdx) {
        if (tileIdx < 0 || tileIdx >= NUM_TILES) return;

        int tx = tileIdx % TILE_COLS;
        int ty = tileIdx / TILE_COLS;
        int tileMinX = static_cast<int>(tx * tileW);
        int tileMaxX = static_cast<int>((tx + 1) * tileW) - 1;
        int tileMinY = static_cast<int>(ty * tileH);
        int tileMaxY = static_cast<int>((ty + 1) * tileH) - 1;

        auto edgeFunc = [](const Vector2& a, const Vector2& b, const Vector2& c) {
            return (c.x - a.x) * (b.y - a.y) - (c.y - a.y) * (b.x - a.x);
        };

        for (int jobID = 0; jobID < NUM_BINNING_JOBS; ++jobID) {
            const auto& tris = workerLists[jobID].tileTriangles[tileIdx];

            for (int triID : tris) {
                const auto& tri = m_ScreenTriangles[triID];

                Vector2 v0 = { tri.v0.x * scaleX, tri.v0.y * scaleY };
                Vector2 v1 = { tri.v1.x * scaleX, tri.v1.y * scaleY };
                Vector2 v2 = { tri.v2.x * scaleX, tri.v2.y * scaleY };

                int minX = (std::max)(tileMinX, static_cast<int>( std::floor((std::min)(v0.x, (std::min)(v1.x, v2.x))) ));
                int maxX = (std::min)(tileMaxX, static_cast<int>( std::ceil((std::max)(v0.x, (std::max)(v1.x, v2.x))) ));
                int minY = (std::max)(tileMinY, static_cast<int>( std::floor((std::min)(v0.y, (std::min)(v1.y, v2.y))) ));
                int maxY = (std::min)(tileMaxY, static_cast<int>( std::ceil((std::max)(v0.y, (std::max)(v1.y, v2.y))) ));

                float area = edgeFunc(v0, v1, v2);
                if (std::abs(area) < 1e-6f) continue;
                float invArea = 1.0f / area;

                for (int y = minY; y <= maxY; ++y) {
                    for (int x = minX; x <= maxX; ++x) {
                        Vector2 p = { x + 0.5f, y + 0.5f };

                        float w0 = edgeFunc(v1, v2, p);
                        float w1 = edgeFunc(v2, v0, p);
                        float w2 = edgeFunc(v0, v1, p);

                        if (w0 >= 0.0f && w1 >= 0.0f && w2 >= 0.0f) {
                            w0 *= invArea;
                            w1 *= invArea;
                            w2 *= invArea;

                            float z = w0 * tri.v0.z + w1 * tri.v1.z + w2 * tri.v2.z;
                            int pixelIndex = y * DEPTH_RES_X + x;

                            if (m_UseReversedZ) {
                                if (z > m_DepthBuffer[pixelIndex]) {
                                    m_DepthBuffer[pixelIndex] = z;
                                }
                            } else {
                                if (z < m_DepthBuffer[pixelIndex]) {
                                    m_DepthBuffer[pixelIndex] = z;
                                }
                            }
                        }
                    }
                }
            }
        }
    };

    DispatchParallel(NUM_TILES, rasterTask);
}

void CullingSystem::PerformOcclusionTestParallel(
    const float* vpMatrix,
    const float* modelMatrices,
    int instanceCount,
    size_t stride,
    const AABB& localBounds,
    float screenWidth,
    float screenHeight,
    std::vector<bool>& outVisibility)
{
    if (instanceCount <= 0 || !modelMatrices) return;
    if (m_DepthBuffer.size() != DEPTH_RES_X * DEPTH_RES_Y) {
        outVisibility.assign(instanceCount, true);
        return;
    }

    // 멀티스레드 Data Race 방지를 위한 1바이트 가시성 버퍼
    std::vector<uint8_t> tempVis(instanceCount, 0);

    auto transformPoint = [](const float* m, const Vector3& p, float& outW) -> Vector3 {
        Vector3 res;
        res.x = m[0] * p.x + m[4] * p.y + m[8]  * p.z + m[12];
        res.y = m[1] * p.x + m[5] * p.y + m[9]  * p.z + m[13];
        res.z = m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14];
        outW  = m[3] * p.x + m[7] * p.y + m[11] * p.z + m[15];
        return res;
    };

    const Vector3 corners[8] = {
        { localBounds.minBounds[0], localBounds.minBounds[1], localBounds.minBounds[2] },
        { localBounds.maxBounds[0], localBounds.minBounds[1], localBounds.minBounds[2] },
        { localBounds.minBounds[0], localBounds.maxBounds[1], localBounds.minBounds[2] },
        { localBounds.maxBounds[0], localBounds.maxBounds[1], localBounds.minBounds[2] },
        { localBounds.minBounds[0], localBounds.minBounds[1], localBounds.maxBounds[2] },
        { localBounds.maxBounds[0], localBounds.minBounds[1], localBounds.maxBounds[2] },
        { localBounds.minBounds[0], localBounds.maxBounds[1], localBounds.maxBounds[2] },
        { localBounds.maxBounds[0], localBounds.maxBounds[1], localBounds.maxBounds[2] }
    };

    uint32_t activeWorkers = (std::min)(m_NumWorkers, static_cast<uint32_t>(instanceCount));

    auto occlusionTask = [&](int workerIndex) {
        int start = (instanceCount * workerIndex) / static_cast<int>(activeWorkers);
        int end   = (instanceCount * (workerIndex + 1)) / static_cast<int>(activeWorkers);

        for (int i = start; i < end; ++i) {
            const float* matrixPtr = reinterpret_cast<const float*>(
                reinterpret_cast<const uint8_t*>(modelMatrices) + static_cast<size_t>(i) * stride);

            float mvp[16];
            if (vpMatrix != nullptr) {
                for (int c = 0; c < 4; ++c) {
                    for (int r = 0; r < 4; ++r) {
                        mvp[c * 4 + r] =
                            vpMatrix[0 * 4 + r] * matrixPtr[c * 4 + 0] +
                            vpMatrix[1 * 4 + r] * matrixPtr[c * 4 + 1] +
                            vpMatrix[2 * 4 + r] * matrixPtr[c * 4 + 2] +
                            vpMatrix[3 * 4 + r] * matrixPtr[c * 4 + 3];
                    }
                }
            } else {
                // 이미 MVP 행렬이 계산되어 넘어온 경우 불필요한 행렬 곱 생략
                std::copy(matrixPtr, matrixPtr + 16, mvp);
            }

            float minX = std::numeric_limits<float>::max();
            float maxX = std::numeric_limits<float>::lowest();
            float minY = std::numeric_limits<float>::max();
            float maxY = std::numeric_limits<float>::lowest();
            float instanceNearestZ = m_UseReversedZ
                ? std::numeric_limits<float>::lowest()
                : std::numeric_limits<float>::max();

            bool intersectsNearPlane = false;

            for (int k = 0; k < 8; ++k) {
                float w;
                Vector3 clip = transformPoint(mvp, corners[k], w);

                if (w <= 0.001f) {
                    intersectsNearPlane = true;
                    break;
                }

                float invW = 1.0f / w;
                float ndcX = clip.x * invW;
                float ndcY = clip.y * invW;
                float ndcZ = clip.z * invW;

                float sx = (ndcX + 1.0f) * 0.5f * DEPTH_RES_X;
                float sy = (1.0f - ndcY) * 0.5f * DEPTH_RES_Y;

                minX = (std::min)(minX, sx);
                maxX = (std::max)(maxX, sx);
                minY = (std::min)(minY, sy);
                maxY = (std::max)(maxY, sy);

                if (m_UseReversedZ) {
                    instanceNearestZ = (std::max)(instanceNearestZ, ndcZ);
                } else {
                    instanceNearestZ = (std::min)(instanceNearestZ, ndcZ);
                }
            }

            if (intersectsNearPlane) {
                tempVis[i] = 1;
                continue;
            }

            // 화면 및 Near/Far 범위 밖이면 즉시 컬링
            if (maxX < 0.0f || minX >= DEPTH_RES_X || maxY < 0.0f || minY >= DEPTH_RES_Y) {
                tempVis[i] = 0;
                continue;
            }

            int startX = (std::max)(0, static_cast<int>(minX));
            int endX   = (std::min)(DEPTH_RES_X - 1, static_cast<int>(maxX));
            int startY = (std::max)(0, static_cast<int>(minY));
            int endY   = (std::min)(DEPTH_RES_Y - 1, static_cast<int>(maxY));

            bool isVisible = false;
            __m128 nearestZVec = _mm_set1_ps(instanceNearestZ);

            // 4-Wide SSE SIMD 깊이 버퍼 비교
            for (int y = startY; y <= endY && !isVisible; ++y) {
                int rowOffset = y * DEPTH_RES_X;
                int x = startX;

                for (; x + 3 <= endX; x += 4) {
                    __m128 bufZ = _mm_loadu_ps(&m_DepthBuffer[rowOffset + x]);
                    __m128 cmp = m_UseReversedZ
                        ? _mm_cmpge_ps(nearestZVec, bufZ)
                        : _mm_cmple_ps(nearestZVec, bufZ);

                    if (_mm_movemask_ps(cmp) != 0) {
                        isVisible = true;
                        break;
                    }
                }

                for (; x <= endX && !isVisible; ++x) {
                    float bufDepth = m_DepthBuffer[rowOffset + x];
                    if (m_UseReversedZ ? (instanceNearestZ >= bufDepth) : (instanceNearestZ <= bufDepth)) {
                        isVisible = true;
                        break;
                    }
                }
            }

            tempVis[i] = isVisible ? 1 : 0;
        }
    };

    DispatchParallel(activeWorkers, occlusionTask);

    outVisibility.resize(instanceCount);
    for (int i = 0; i < instanceCount; ++i) {
        outVisibility[i] = (tempVis[i] != 0);
    }
}

} // namespace Endfield
