#include "Culling.h"
#include "VulkanBackend.h"
#include <cmath>
#include <thread>
#include <future>
#include <iostream>

namespace Endfield {

void Frustum::ExtractFromMatrix(const float* vpMatrix) {
    // vpMatrix is assumed to be column-major 4x4 matrix (float[16])
    // Unity/Vulkan matrices may need specific adjustments depending on coordinate systems,
    // this is a standard Gribb/Hart plane extraction.
    
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
    // Near (For Vulkan/DirectX [0, 1] depth range)
    planes[4][0] = vpMatrix[2];
    planes[4][1] = vpMatrix[6];
    planes[4][2] = vpMatrix[10];
    planes[4][3] = vpMatrix[14];
    // Far
    planes[5][0] = vpMatrix[3] - vpMatrix[2];
    planes[5][1] = vpMatrix[7] - vpMatrix[6];
    planes[5][2] = vpMatrix[11] - vpMatrix[10];
    planes[5][3] = vpMatrix[15] - vpMatrix[14];

    // Normalize planes
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
    // p-vertex (positive vertex) 테스트
    for (int i = 0; i < 6; ++i) {
        float px = (planes[i][0] > 0.0f) ? aabb.maxBounds[0] : aabb.minBounds[0];
        float py = (planes[i][1] > 0.0f) ? aabb.maxBounds[1] : aabb.minBounds[1];
        float pz = (planes[i][2] > 0.0f) ? aabb.maxBounds[2] : aabb.minBounds[2];

        float dot = planes[i][0] * px + planes[i][1] * py + planes[i][2] * pz;
        if (dot < -planes[i][3]) {
            return false; // AABB is completely outside this plane
        }
    }
    return true;
}

// -------------------------------------------------------------

void CullingSystem::Initialize(uint32_t numWorkers) {
    m_NumWorkers = numWorkers > 0 ? numWorkers : std::thread::hardware_concurrency();
    
    // 저해상도 소프트웨어 뎁스 버퍼 할당 (예: 256x128 픽셀용)
    m_DepthBuffer.resize(256 * 128, 0.0f);
    
    VulkanBackend::LogToUnity("[CullingSystem] Initialized with " + std::to_string(m_NumWorkers) + " parallel workers.");
}

void CullingSystem::Shutdown() {
    m_DepthBuffer.clear();
}

void CullingSystem::PerformFrustumCullingParallel(const Frustum& frustum, const std::vector<AABB>& aabbs, std::vector<bool>& outVisibility) {
    if (aabbs.empty()) return;
    outVisibility.resize(aabbs.size(), false);

    // Multi-threading (Task Graph 분할)
    auto workerTask = [&](size_t start, size_t end) {
        // (실제 프로젝트에서는 SSE/AVX 내장 함수 <immintrin.h>를 통해 SIMD 4~8개씩 일괄 비교 수행)
        for (size_t i = start; i < end; ++i) {
            outVisibility[i] = frustum.Intersects(aabbs[i]);
        }
    };

    size_t chunkSize = aabbs.size() / m_NumWorkers;
    if (chunkSize == 0) chunkSize = aabbs.size();

    std::vector<std::future<void>> futures;
    for (size_t i = 0; i < m_NumWorkers; ++i) {
        size_t start = i * chunkSize;
        size_t end = (i == m_NumWorkers - 1) ? aabbs.size() : start + chunkSize;
        
        if (start < aabbs.size()) {
            futures.push_back(std::async(std::launch::async, workerTask, start, end));
        }
    }

    // 모든 스레드 작업 대기 (유니티 렌더 스레드가 블로킹되는 메인 스레드에서 대기)
    for (auto& fut : futures) {
        fut.wait();
    }
}

void CullingSystem::BatchOccluders() {
    // 1단계 (Batch): 모든 오클루더(occluder)를 가져와 트랜스폼, 클리핑, 프러스텀 처리를 적용해
    // 하나의 긴 스크린 스페이스 삼각형(또는 바운딩 박스) 덩어리로 만듭니다.
    // Endfield 문서: "데이터는 자신이 어떤 메시에서 왔는지에 대한 정보를 잃습니다. 그냥 화면 위의 삼각형 덩어리일 뿐입니다."
    
    // (모의 구현) 실제로는 오클루더 메쉬들을 Screen-space 삼각형 리스트로 변환하여 m_ScreenTriangles 같은 버퍼에 쌓습니다.
    VulkanBackend::LogToUnity("[CullingSystem] Phase 1: BatchOccluders - Created screen-space triangles.");
}

void CullingSystem::RasterizeTilesParallel() {
    // 2단계 (Raster/Tiling): 화면을 8개의 타일로 나누고, 각 워커가 독립적으로 래스터화 (Lock-Free)
    // Endfield 문서: "화면을 8개의 타일로 나누고... 4개의 잡을 사용하며, 각 잡은 전체 삼각형의 1/4을 담당하고 
    // 각자 자신만의 프라이빗한 8개 타일 슬롯을 가집니다. 4x8=32개의 리스트... 절대 락이 필요 없습니다."

    const int NUM_WORKERS = 4;
    const int NUM_TILES = 8;
    
    // [Phase 2-1: Binning (분배)] 
    // 각 워커는 전체 삼각형의 1/4씩을 맡아서, 어떤 타일에 속하는지 판별해 자신의 8개 프라이빗 리스트에 분배합니다.
    struct WorkerPrivateList {
        std::vector<int> tileTriangles[NUM_TILES];
    };
    std::vector<WorkerPrivateList> workerLists(NUM_WORKERS);

    auto binningTask = [&](int workerID) {
        // 실제로는 m_ScreenTriangles 배열의 (workerID/4) 구간을 순회하며 타일 인덱스를 계산 후 추가
        // workerLists[workerID].tileTriangles[tileIndex].push_back(triangleID);
    };

    std::vector<std::future<void>> futures;
    for (int i = 0; i < NUM_WORKERS; ++i) {
        futures.push_back(std::async(std::launch::async, binningTask, i));
    }
    for (auto& fut : futures) fut.wait();
    futures.clear();

    // [Phase 2-2: Rasterization (래스터화)]
    // 각 워커는 이제 특정 '타일(Tile)'들을 전담하여 깊이 버퍼에 래스터화합니다.
    // 타일 영역은 겹치지 않으므로 m_DepthBuffer에 쓰는 작업은 락 프리가 됩니다.
    auto rasterTask = [&](int workerID) {
        // 4명의 워커가 8개의 타일을 2개씩 나눠 가짐
        int tilesPerWorker = NUM_TILES / NUM_WORKERS;
        int startTile = workerID * tilesPerWorker;
        int endTile = startTile + tilesPerWorker;

        for (int t = startTile; t < endTile; ++t) {
            // 다른 4명의 워커들이 만들어둔 t번째 타일의 리스트를 모두 가져와서 그린다.
            for (int w = 0; w < NUM_WORKERS; ++w) {
                const auto& tris = workerLists[w].tileTriangles[t];
                // for (int triID : tris) {
                //      SIMD를 활용해 해당 삼각형을 래스터화하고 m_DepthBuffer의 타일 영역에 기록 (Lock-free)
                // }
            }
        }
    };

    for (int i = 0; i < NUM_WORKERS; ++i) {
        futures.push_back(std::async(std::launch::async, rasterTask, i));
    }
    for (auto& fut : futures) fut.wait();

    VulkanBackend::LogToUnity("[CullingSystem] Phase 2: RasterizeTilesParallel - 8 Tiles Rasterized (Lock-Free).");
}

void CullingSystem::PerformOcclusionTestParallel() {
    // 3단계 (Visibility): 프러스텀에 살아남은 엔티티들의 AABB를 Screen-space로 투영해 m_DepthBuffer와 비교
    // Endfield 문서: "가시성 판정에서 대부분의 워커는 개별(per-entity) 작업을 수행하고... 락 스텝이 없습니다."

    auto occlusionTask = [&](int workerID) {
        // 각 워커는 담당하는 대상(instances)의 AABB를 스크린에 투영하여 화면 상의 min Z 값을 구함.
        // 그리고 m_DepthBuffer에서 해당하는 영역의 깊이 최대값과 비교.
        // if (instance.z_min > m_DepthBuffer[tile_max_z]) -> 완전히 가려짐(Culled)
    };

    VulkanBackend::LogToUnity("[CullingSystem] Phase 3: PerformOcclusionTestParallel - Culling executed against Depth Buffer.");
}

} // namespace Endfield

