#pragma once
#include <vector>

namespace Endfield {

struct AABB {
    float minBounds[3];
    float maxBounds[3];
};

struct Frustum {
    // 6개의 평면 방정식 (Left, Right, Bottom, Top, Near, Far)
    // 각 평면은 (nx, ny, nz, d) 형태
    float planes[6][4];

    // View-Projection 행렬로부터 Frustum 평면을 추출하는 헬퍼
    void ExtractFromMatrix(const float* vpMatrix);
    
    // AABB가 프러스텀 내부에 있는지 검사 (가장 기본적인 Frustum Culling, 이후 SSE/AVX의 <immintrin.h> 내장 함수로 SIMD 최적화 가능)
    bool Intersects(const AABB& aabb) const;
};

// Rule 3: Software Occlusion Culling & Multithreading
// 유니티의 Job System 대신 순수 C++ 11 스레드 풀 메커니즘을 사용해 메인 렌더 루프를 블로킹하며 고속 처리
class CullingSystem {
public:
    void Initialize(uint32_t numWorkers);
    void Shutdown();
    
    // 1. Frustum Culling (SIMD/멀티스레드 병렬 처리용 진입점)
    void PerformFrustumCullingParallel(const Frustum& frustum, const std::vector<AABB>& aabbs, std::vector<bool>& outVisibility);
    
    // 2. Combine all occluders into screen space triangles
    void BatchOccluders();
    
    // 3. Tile screen into 8 tiles and rasterize in parallel (lock-free)
    void RasterizeTilesParallel();
    
    // 4. View integration: Culling for multiple views (Main, Shadow, etc.) at once
    void PerformOcclusionTestParallel();

private:
    uint32_t m_NumWorkers;
    // 소프트웨어 오클루전 컬링용 Low-res Depth Buffer (예: 256x128)
    std::vector<float> m_DepthBuffer; 
};

} // namespace Endfield

