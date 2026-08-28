#pragma once
#include <vector>

namespace Endfield {

// Rule 3: Software Occlusion Culling & Multithreading
class CullingSystem {
public:
    void Initialize(uint32_t numWorkers);
    
    // 1. Combine all occluders into screen space triangles
    void BatchOccluders();
    
    // 2. Tile screen into 8 tiles and rasterize in parallel (lock-free)
    void RasterizeTilesParallel();
    
    // 3. View integration: Culling for multiple views (Main, Shadow, etc.) at once
    void PerformVisibilityTestParallel();

private:
    uint32_t m_NumWorkers;
    // Low-res depth buffer for software culling
    std::vector<float> m_DepthBuffer; 
};

} // namespace Endfield

