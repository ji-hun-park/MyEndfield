#pragma once
#include <vector>
#include <cstdint>
#include <atomic>
#include "ThreadPool.h"

namespace Endfield {

struct AABB {
    float minBounds[3];
    float maxBounds[3];
};

struct Vector2 { float x, y; };
struct Vector3 { float x, y, z; };

struct ScreenTriangle {
    Vector3 v0, v1, v2;
};

struct OccluderMesh {
    std::vector<Vector3> vertices;
    std::vector<uint32_t> indices;
    float localToWorld[16]; // Column-major Transform Matrix
};

struct alignas(16) Frustum {
    // 6개의 평면 방정식 (Left, Right, Bottom, Top, Near, Far)
    // 각 평면은 (nx, ny, nz, d) 형태
    float planes[6][4];

    void ExtractFromMatrix(const float* vpMatrix);
    bool Intersects(const AABB& aabb) const;
};

class CullingSystem {
public:
    static constexpr int DEPTH_RES_X = 256;
    static constexpr int DEPTH_RES_Y = 128;
    static constexpr int NUM_BINNING_JOBS = 4;
    static constexpr int TILE_COLS = 4;
    static constexpr int TILE_ROWS = 2;
    static constexpr int NUM_TILES = TILE_COLS * TILE_ROWS; // 8 Tiles

    // 기본값 유지로 기존 CullingTests.cpp와 100% 하위 호환
    void Initialize(uint32_t numWorkers, ThreadPool* threadPool = nullptr, bool useReversedZ = true);
    void Shutdown();

    void SetThreadPool(ThreadPool* threadPool) { m_ThreadPool = threadPool; }
    void SetReversedZ(bool useReversedZ) { m_UseReversedZ = useReversedZ; }

    // 단일 뷰 프러스텀 컬링 (기존 호환용)
    void PerformFrustumCullingParallel(const Frustum& frustum, const std::vector<AABB>& aabbs, std::vector<bool>& outVisibility);

    // 뷰 병렬(Multi-View) 프러스텀 컬링 & 청크 AABB 사전 컬링 (32비트 비트마스크 기록)
    // BeginOcclusionRasterizationAsync() 직후 호출 시 동일한 스레드 풀에서 전역 배리어 없이 동시 실행됨
    void PerformMultiViewFrustumCullingParallel(
        const Frustum* frustums,
        uint32_t viewCount,
        const AABB* chunkAABBs,
        uint32_t entitiesPerChunk,
        const std::vector<AABB>& entityAABBs,
        std::vector<uint32_t>& outVisibilityMasks);

    // 전역 배리어 없는 비동기 오클루더 배치 + 4x2 비닝 -> 8타일 래스터화 Continuation 디스패치
    // 호출 즉시 스레드 풀에 비닝 잡을 올리고 대기 없이 반환하여 프러스텀 컬링과 병렬 오버랩됨
    void BeginOcclusionRasterizationAsync(
        const std::vector<OccluderMesh>& occluders,
        const float* vpMatrix,
        float screenWidth,
        float screenHeight);

    // 비동기 오클루전 래스터화(Phase 1 ~ Phase 3-A)에서 측정된 소요 시간(ms) 반환
    float GetLastAsyncRasterTimeMs() const noexcept {
        return static_cast<float>(m_LastAsyncRasterTimeUs.load(std::memory_order_acquire)) * 0.001f;
    }

    // 4-Wide SIMD 오클루더 배치 및 화면 공간 삼각형 변환
    void BatchOccluders(const std::vector<OccluderMesh>& occluders, const float* vpMatrix, float screenWidth, float screenHeight);

    // 4x2 타일 비닝(32개 리스트) 및 8개 잡 락프리 타일 래스터화 (동기 실행 호환용)
    void RasterizeTilesParallel(float screenWidth, float screenHeight);

    // SIMD Hi-Z 깊이 비교 및 최종 가시성 판정 (합류 펜스 이후 실행)
    void PerformOcclusionTestParallel(
        const float* vpMatrix,
        const float* modelMatrices,
        int instanceCount,
        size_t stride,
        const AABB& localBounds,
        float screenWidth,
        float screenHeight,
        std::vector<bool>& outVisibility);

private:
    struct alignas(64) BinningJobList {
        std::vector<int> tileTriangles[NUM_TILES];
    };

    uint32_t m_NumWorkers = 4;
    ThreadPool* m_ThreadPool = nullptr;
    bool m_UseReversedZ = true;

    alignas(64) std::vector<float> m_DepthBuffer;
    std::vector<ScreenTriangle> m_ScreenTriangles;

    // 프레임마다 재할당하지 않고 재사용하는 32개(4x8) 타일 비닝 버퍼 및 Continuation 카운터
    std::vector<BinningJobList> m_BinningWorkerLists;
    std::atomic<int> m_RemainingBinningJobs{0};
    std::atomic<int> m_RemainingRasterJobs{0};
    std::atomic<uint64_t> m_LastAsyncRasterTimeUs{0};
    float m_CachedScaleX = 1.0f;
    float m_CachedScaleY = 1.0f;

    void ExecuteBinningJob(int jobID);
    void ExecuteTileRasterJob(int tileIdx);

    // 스레드 풀 유무에 따라 작업을 병렬 디스패치하는 내부 헬퍼
    template<typename Func>
    void DispatchParallel(uint32_t jobCount, Func&& task);
};

} // namespace Endfield
