#pragma once
#include <cstdint>
#include <chrono>
#include <vector>
#include <string>
#include <mutex>

namespace Endfield {

// 4-byte aligned structure matching C# [StructLayout(LayoutKind.Sequential)]
struct NativeBenchmarkStats {
    uint32_t totalInstances;
    uint32_t visibleInstances;
    uint32_t culledFrustum;
    uint32_t culledOcclusion;
    float ecsQueryTimeMs;
    float frustumCullingTimeMs;
    float occlusionCullingTimeMs;
    float sortingTimeMs;
    float batchingTimeMs;
    float renderSubmitTimeMs;
    float totalNativeFrameTimeMs;
};

class ScopedTimer {
public:
    explicit ScopedTimer(float* outMs) 
        : m_OutMs(outMs), m_Start(std::chrono::high_resolution_clock::now()) {}
    
    ~ScopedTimer() {
        if (m_OutMs) {
            auto end = std::chrono::high_resolution_clock::now();
            std::chrono::duration<float, std::milli> duration = end - m_Start;
            *m_OutMs = duration.count();
        }
    }
private:
    float* m_OutMs;
    std::chrono::time_point<std::chrono::high_resolution_clock> m_Start;
};

class BenchmarkManager {
public:
    static BenchmarkManager& Instance();

    void SetCullingOptions(bool enableFrustum, bool enableOcclusion);
    bool IsFrustumCullingEnabled() const { return m_EnableFrustum; }
    bool IsOcclusionCullingEnabled() const { return m_EnableOcclusion; }

    void BeginFrame();
    void EndFrame();

    NativeBenchmarkStats& GetCurrentFrameStats() { return m_CurrentStats; }
    NativeBenchmarkStats GetLatestFrameStats();

    // Pure C++ headless benchmark without requiring a Vulkan window or Swapchain
    void RunHeadlessBenchmark(int instanceCount, int iterations, bool enableCulling, NativeBenchmarkStats* outAverages);

private:
    BenchmarkManager();

    bool m_EnableFrustum = true;
    bool m_EnableOcclusion = false;

    NativeBenchmarkStats m_CurrentStats{};
    NativeBenchmarkStats m_LatestCompletedStats{};
    std::chrono::time_point<std::chrono::high_resolution_clock> m_FrameStartTime;
    std::mutex m_StatsMutex;
};

} // namespace Endfield

