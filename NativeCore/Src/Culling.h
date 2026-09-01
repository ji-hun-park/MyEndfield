#pragma once
#include <vector>

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
    void BatchOccluders(const std::vector<OccluderMesh>& occluders, const float* vpMatrix, float screenWidth, float screenHeight);
    
    // 3. Tile screen into 8 tiles and rasterize in parallel (lock-free)
    void RasterizeTilesParallel(float screenWidth, float screenHeight);
    
    // 4. View integration: Culling for multiple views (Main, Shadow, etc.) at once
    void PerformOcclusionTestParallel(const float* vpMatrix, const float* modelMatrices, int instanceCount, size_t stride, const AABB& localBounds, float screenWidth, float screenHeight, std::vector<bool>& outVisibility);

    static const int DEPTH_RES_X = 256;
    static const int DEPTH_RES_Y = 128;

private:
    uint32_t m_NumWorkers;
    // 소프트웨어 오클루전 컬링용 Low-res Depth Buffer (예: 256x128)
    std::vector<float> m_DepthBuffer; 
    std::vector<ScreenTriangle> m_ScreenTriangles; // 배칭된 스크린 스페이스 삼각형들
};

} // namespace Endfield

