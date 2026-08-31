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
    // 씬의 정적(Static) 오클루더 메쉬들을 모아 소프트웨어 래스터라이즈용 삼각형 데이터로 가공
}

void CullingSystem::RasterizeTilesParallel() {
    // 화면을 TILE 크기로 분할하여 병렬 워커들이 락프리(Lock-Free)로 m_DepthBuffer에 기록
}

void CullingSystem::PerformOcclusionTestParallel() {
    // AABB를 스크린 스페이스(Screen Space)로 투영 후, m_DepthBuffer와 비교 (소프트웨어 오클루전 컬링)
}

} // namespace Endfield

