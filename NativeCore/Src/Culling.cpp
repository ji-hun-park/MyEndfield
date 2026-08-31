#define NOMINMAX
#include "Culling.h"
#include "VulkanBackend.h"
#include <algorithm>
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
    
    // 저해상도 소프트웨어 뎁스 버퍼 할당
    m_DepthBuffer.resize(DEPTH_RES_X * DEPTH_RES_Y, 0.0f);
    
    VulkanBackend::LogToUnity("[CullingSystem] Initialized with " + std::to_string(m_NumWorkers) + " parallel workers.");
}

void CullingSystem::Shutdown() {
    m_DepthBuffer.clear();
    m_ScreenTriangles.clear();
    VulkanBackend::LogToUnity("[CullingSystem] Shutdown.");
}

void CullingSystem::PerformFrustumCullingParallel(const Frustum& frustum, const std::vector<AABB>& aabbs, std::vector<bool>& outVisibility) {
    if (aabbs.empty()) return;
    
    outVisibility.resize(aabbs.size(), false);
    
    auto cullTask = [&](int workerID) {
        size_t total = aabbs.size();
        size_t chunk = total / m_NumWorkers;
        size_t start = workerID * chunk;
        size_t end = (workerID == m_NumWorkers - 1) ? total : start + chunk;

        for (size_t i = start; i < end; ++i) {
            outVisibility[i] = frustum.Intersects(aabbs[i]);
        }
    };

    std::vector<std::future<void>> futures;
    for (uint32_t i = 0; i < m_NumWorkers; ++i) {
        futures.push_back(std::async(std::launch::async, cullTask, i));
    }
    
    for (auto& fut : futures) {
        fut.wait();
    }
}

void CullingSystem::BatchOccluders(const std::vector<OccluderMesh>& occluders, const float* vpMatrix, float screenWidth, float screenHeight) {
    m_ScreenTriangles.clear();

    auto transformPoint = [](const float* vp, const float* model, const Vector3& p, float& outW) -> Vector3 {
        float worldX = model[0]*p.x + model[4]*p.y + model[8]*p.z  + model[12];
        float worldY = model[1]*p.x + model[5]*p.y + model[9]*p.z  + model[13];
        float worldZ = model[2]*p.x + model[6]*p.y + model[10]*p.z + model[14];

        Vector3 res;
        res.x = vp[0]*worldX + vp[4]*worldY + vp[8]*worldZ  + vp[12];
        res.y = vp[1]*worldX + vp[5]*worldY + vp[9]*worldZ  + vp[13];
        res.z = vp[2]*worldX + vp[6]*worldY + vp[10]*worldZ + vp[14];
        outW  = vp[3]*worldX + vp[7]*worldY + vp[11]*worldZ + vp[15];
        return res;
    };

    for (const auto& mesh : occluders) {
        for (size_t i = 0; i < mesh.indices.size(); i += 3) {
            const Vector3& p0 = mesh.vertices[mesh.indices[i]];
            const Vector3& p1 = mesh.vertices[mesh.indices[i+1]];
            const Vector3& p2 = mesh.vertices[mesh.indices[i+2]];

            float w0, w1, w2;
            Vector3 clip0 = transformPoint(vpMatrix, mesh.localToWorld, p0, w0);
            Vector3 clip1 = transformPoint(vpMatrix, mesh.localToWorld, p1, w1);
            Vector3 clip2 = transformPoint(vpMatrix, mesh.localToWorld, p2, w2);

            if (w0 <= 0.0f && w1 <= 0.0f && w2 <= 0.0f) continue;

            // 원근 나누기 (Perspective Divide) -> NDC (Normalized Device Coordinates)
            Vector3 ndc0 = { clip0.x / w0, clip0.y / w0, clip0.z / w0 };
            Vector3 ndc1 = { clip1.x / w1, clip1.y / w1, clip1.z / w1 };
            Vector3 ndc2 = { clip2.x / w2, clip2.y / w2, clip2.z / w2 };

            // 백페이스 컬링 (Backface Culling)
            float crossZ = (ndc1.x - ndc0.x) * (ndc2.y - ndc0.y) - (ndc1.y - ndc0.y) * (ndc2.x - ndc0.x);
            if (crossZ < 0.0f) { // 시계방향 기준, 후면이면 제거
                continue;
            }

            // Screen Space 로 맵핑 [-1, 1] -> [0, width], [0, height]
            auto toScreen = [screenWidth, screenHeight](const Vector3& ndc) -> Vector3 {
                return {
                    (ndc.x + 1.0f) * 0.5f * screenWidth,
                    (1.0f - ndc.y) * 0.5f * screenHeight, // Vulkan/Unity Y-axis 대응
                    ndc.z
                };
            };

            m_ScreenTriangles.push_back({
                toScreen(ndc0), 
                toScreen(ndc1), 
                toScreen(ndc2)
            });
        }
    }

    VulkanBackend::LogToUnity("[CullingSystem] Phase 1: BatchOccluders - Created " + std::to_string(m_ScreenTriangles.size()) + " screen-space triangles.");
}

void CullingSystem::RasterizeTilesParallel(float screenWidth, float screenHeight) {
    if (m_ScreenTriangles.empty()) return;

    // 버퍼 초기화 (Reversed Z 가정: 0.0f가 가장 먼 곳)
    std::fill(m_DepthBuffer.begin(), m_DepthBuffer.end(), 0.0f);

    const int NUM_WORKERS = 4;
    const int NUM_TILES = 8;
    const int TILE_COLS = 4;
    const int TILE_ROWS = 2;
    const float tileW = static_cast<float>(DEPTH_RES_X) / TILE_COLS;
    const float tileH = static_cast<float>(DEPTH_RES_Y) / TILE_ROWS;
    
    // 화면 크기 대비 저해상도 뎁스 버퍼 스케일 비율
    float scaleX = DEPTH_RES_X / screenWidth;
    float scaleY = DEPTH_RES_Y / screenHeight;

    struct WorkerPrivateList {
        std::vector<int> tileTriangles[NUM_TILES];
    };
    std::vector<WorkerPrivateList> workerLists(NUM_WORKERS);

    auto binningTask = [&](int workerID) {
        size_t totalTris = m_ScreenTriangles.size();
        size_t chunk = totalTris / NUM_WORKERS;
        size_t start = workerID * chunk;
        size_t end = (workerID == NUM_WORKERS - 1) ? totalTris : start + chunk;

        for (size_t i = start; i < end; ++i) {
            const auto& tri = m_ScreenTriangles[i];
            
            float minX = (std::min)(tri.v0.x, (std::min)(tri.v1.x, tri.v2.x)) * scaleX;
            float maxX = (std::max)(tri.v0.x, (std::max)(tri.v1.x, tri.v2.x)) * scaleX;
            float minY = (std::min)(tri.v0.y, (std::min)(tri.v1.y, tri.v2.y)) * scaleY;
            float maxY = (std::max)(tri.v0.y, (std::max)(tri.v1.y, tri.v2.y)) * scaleY;

            // 컬링: 화면(뎁스 버퍼) 밖으로 나간 삼각형 무시
            if (maxX < 0 || minX >= DEPTH_RES_X || maxY < 0 || minY >= DEPTH_RES_Y) continue;

            minX = (std::max)(0.0f, minX);
            minY = (std::max)(0.0f, minY);
            maxX = (std::min)((float)DEPTH_RES_X - 1.0f, maxX);
            maxY = (std::min)((float)DEPTH_RES_Y - 1.0f, maxY);

            int startTileX = static_cast<int>(minX / tileW);
            int endTileX   = static_cast<int>(maxX / tileW);
            int startTileY = static_cast<int>(minY / tileH);
            int endTileY   = static_cast<int>(maxY / tileH);

            for (int ty = startTileY; ty <= endTileY; ++ty) {
                for (int tx = startTileX; tx <= endTileX; ++tx) {
                    int tileIndex = ty * TILE_COLS + tx;
                    if (tileIndex >= 0 && tileIndex < NUM_TILES) {
                        workerLists[workerID].tileTriangles[tileIndex].push_back(static_cast<int>(i));
                    }
                }
            }
        }
    };

    std::vector<std::future<void>> futures;
    for (int i = 0; i < NUM_WORKERS; ++i) {
        futures.push_back(std::async(std::launch::async, binningTask, i));
    }
    for (auto& fut : futures) fut.wait();
    futures.clear();

    auto rasterTask = [&](int workerID) {
        int tilesPerWorker = NUM_TILES / NUM_WORKERS;
        int startTile = workerID * tilesPerWorker;
        int endTile = startTile + tilesPerWorker;

        // Edge function 헬퍼
        auto edgeFunc = [](const Vector2& a, const Vector2& b, const Vector2& c) {
            return (c.x - a.x) * (b.y - a.y) - (c.y - a.y) * (b.x - a.x);
        };

        for (int t = startTile; t < endTile; ++t) {
            // 현재 타일의 픽셀 경계 계산
            int tx = t % TILE_COLS;
            int ty = t / TILE_COLS;
            int tileMinX = static_cast<int>(tx * tileW);
            int tileMaxX = static_cast<int>((tx + 1) * tileW) - 1;
            int tileMinY = static_cast<int>(ty * tileH);
            int tileMaxY = static_cast<int>((ty + 1) * tileH) - 1;

            // 모든 워커(4명)가 이 타일(t)에 던져둔 삼각형 인덱스를 모두 모아서 래스터라이즈
            for (int w = 0; w < NUM_WORKERS; ++w) {
                const auto& tris = workerLists[w].tileTriangles[t];
                
                for (int triID : tris) {
                    const auto& tri = m_ScreenTriangles[triID];
                    
                    Vector2 v0 = { tri.v0.x * scaleX, tri.v0.y * scaleY };
                    Vector2 v1 = { tri.v1.x * scaleX, tri.v1.y * scaleY };
                    Vector2 v2 = { tri.v2.x * scaleX, tri.v2.y * scaleY };

                    int minX = (std::max)(tileMinX, static_cast<int>((std::min)(v0.x, (std::min)(v1.x, v2.x))));
                    int maxX = (std::min)(tileMaxX, static_cast<int>((std::max)(v0.x, (std::max)(v1.x, v2.x))));
                    int minY = (std::max)(tileMinY, static_cast<int>((std::min)(v0.y, (std::min)(v1.y, v2.y))));
                    int maxY = (std::min)(tileMaxY, static_cast<int>((std::max)(v0.y, (std::max)(v1.y, v2.y))));

                    float area = edgeFunc(v0, v1, v2);
                    if (area == 0.0f) continue;

                    for (int y = minY; y <= maxY; ++y) {
                        for (int x = minX; x <= maxX; ++x) {
                            Vector2 p = { x + 0.5f, y + 0.5f }; // 픽셀 중심점

                            float w0 = edgeFunc(v1, v2, p);
                            float w1 = edgeFunc(v2, v0, p);
                            float w2 = edgeFunc(v0, v1, p);

                            // 삼각형 내부에 있는지 확인 (시계 방향 기준)
                            if (w0 >= 0 && w1 >= 0 && w2 >= 0) {
                                w0 /= area;
                                w1 /= area;
                                w2 /= area;

                                // Barycentric Coordinates를 이용한 깊이(Depth) 보간
                                float z = w0 * tri.v0.z + w1 * tri.v1.z + w2 * tri.v2.z;
                                
                                int pixelIndex = y * DEPTH_RES_X + x;
                                // Reversed Z: 1.0(가까움) -> 0.0(멈), 따라서 덮어쓰려면 z 값이 더 커야 함.
                                if (z > m_DepthBuffer[pixelIndex]) {
                                    m_DepthBuffer[pixelIndex] = z; // Lock-free 안전 구역!
                                }
                            }
                        }
                    }
                }
            }
        }
    };

    for (int i = 0; i < NUM_WORKERS; ++i) {
        futures.push_back(std::async(std::launch::async, rasterTask, i));
    }
    for (auto& fut : futures) fut.wait();

    VulkanBackend::LogToUnity("[CullingSystem] Phase 2: RasterizeTilesParallel - 8 Tiles Rasterized (Lock-Free) into Depth Buffer.");
}

void CullingSystem::PerformOcclusionTestParallel(const float* mvpMatrices, int instanceCount, size_t stride, const AABB& localBounds, float screenWidth, float screenHeight, std::vector<bool>& outVisibility) {
    if (instanceCount <= 0) return;
    outVisibility.resize(instanceCount, false);

    const int NUM_WORKERS = m_NumWorkers;

    auto transformPoint = [](const float* m, const Vector3& p, float& outW) -> Vector3 {
        Vector3 res;
        res.x = m[0]*p.x + m[4]*p.y + m[8]*p.z  + m[12];
        res.y = m[1]*p.x + m[5]*p.y + m[9]*p.z  + m[13];
        res.z = m[2]*p.x + m[6]*p.y + m[10]*p.z + m[14];
        outW  = m[3]*p.x + m[7]*p.y + m[11]*p.z + m[15];
        return res;
    };

    Vector3 corners[8] = {
        { localBounds.minBounds[0], localBounds.minBounds[1], localBounds.minBounds[2] },
        { localBounds.maxBounds[0], localBounds.minBounds[1], localBounds.minBounds[2] },
        { localBounds.minBounds[0], localBounds.maxBounds[1], localBounds.minBounds[2] },
        { localBounds.maxBounds[0], localBounds.maxBounds[1], localBounds.minBounds[2] },
        { localBounds.minBounds[0], localBounds.minBounds[1], localBounds.maxBounds[2] },
        { localBounds.maxBounds[0], localBounds.minBounds[1], localBounds.maxBounds[2] },
        { localBounds.minBounds[0], localBounds.maxBounds[1], localBounds.maxBounds[2] },
        { localBounds.maxBounds[0], localBounds.maxBounds[1], localBounds.maxBounds[2] }
    };

    auto occlusionTask = [&](int workerID) {
        int chunk = instanceCount / NUM_WORKERS;
        int start = workerID * chunk;
        int end = (workerID == NUM_WORKERS - 1) ? instanceCount : start + chunk;

        for (int i = start; i < end; ++i) {
            const float* mvp = reinterpret_cast<const float*>(reinterpret_cast<const uint8_t*>(mvpMatrices) + i * stride);
            
            float minX = 1e9f, maxX = -1e9f;
            float minY = 1e9f, maxY = -1e9f;
            float instanceNearestZ = -1e9f; // Reversed Z: 1.0이 가장 가깝다

            bool intersectsNearPlane = false;

            for (int k = 0; k < 8; ++k) {
                float w;
                Vector3 clip = transformPoint(mvp, corners[k], w);

                if (w <= 0.001f) {
                    intersectsNearPlane = true;
                    break;
                }

                float ndcX = clip.x / w;
                float ndcY = clip.y / w;
                float ndcZ = clip.z / w;

                float sx = (ndcX + 1.0f) * 0.5f * DEPTH_RES_X;
                float sy = (1.0f - ndcY) * 0.5f * DEPTH_RES_Y; // Vulkan Y-down 대응

                minX = std::min(minX, sx);
                maxX = std::max(maxX, sx);
                minY = std::min(minY, sy);
                maxY = std::max(maxY, sy);
                instanceNearestZ = std::max(instanceNearestZ, ndcZ);
            }

            // 카메라(Near Plane)를 뚫고 들어오는 오브젝트는 무조건 보인다고 보수적으로 판정
            if (intersectsNearPlane) {
                outVisibility[i] = true;
                continue;
            }

            // AABB가 화면 밖에 있는 경우 (Frustum Culling)
            if (maxX < 0 || minX >= DEPTH_RES_X || maxY < 0 || minY >= DEPTH_RES_Y) {
                outVisibility[i] = false;
                continue;
            }

            int startX = (std::max)(0, static_cast<int>(minX));
            int endX   = (std::min)(DEPTH_RES_X - 1, static_cast<int>(maxX));
            int startY = (std::max)(0, static_cast<int>(minY));
            int endY   = (std::min)(DEPTH_RES_Y - 1, static_cast<int>(maxY));

            bool isVisible = false;

            // Depth 버퍼(m_DepthBuffer)와 비교
            for (int y = startY; y <= endY; ++y) {
                for (int x = startX; x <= endX; ++x) {
                    int pixelIndex = y * DEPTH_RES_X + x;
                    
                    // Reversed Z: 오브젝트의 가장 가까운 점(instanceNearestZ)이 버퍼의 값보다 크면(앞에 있으면) 보인다.
                    if (instanceNearestZ >= m_DepthBuffer[pixelIndex]) {
                        isVisible = true;
                        break;
                    }
                }
                if (isVisible) break;
            }

            outVisibility[i] = isVisible;
        }
    };

    std::vector<std::future<void>> futures;
    for (int i = 0; i < NUM_WORKERS; ++i) {
        futures.push_back(std::async(std::launch::async, occlusionTask, i));
    }
    for (auto& fut : futures) fut.wait();

    VulkanBackend::LogToUnity("[CullingSystem] Phase 3: PerformOcclusionTestParallel - Culling executed against Depth Buffer.");
}

} // namespace Endfield
