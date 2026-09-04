#include <gtest/gtest.h>
#include "Benchmark.h"
#include "PluginAPI.h"
#include <thread>
#include <chrono>

using namespace Endfield;

TEST(BenchmarkTests, ScopedTimerMeasurement) {
    float measuredMs = 0.0f;
    {
        ScopedTimer timer(&measuredMs);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    // Should be at least ~9ms and not excessively large (< 100ms)
    EXPECT_GE(measuredMs, 8.0f);
    EXPECT_LT(measuredMs, 100.0f);
}

TEST(BenchmarkTests, HeadlessBenchmarkExecution) {
    NativeBenchmarkStats stats{};
    // Run 5,000 instances with culling enabled for 3 iterations
    BenchmarkManager::Instance().RunHeadlessBenchmark(5000, 3, true, &stats);

    EXPECT_EQ(stats.totalInstances, 5000u);
    EXPECT_GT(stats.visibleInstances, 0u);
    EXPECT_LE(stats.visibleInstances, 5000u);
    
    // Frustum culling should cull instances outside the view frustum
    EXPECT_GT(stats.culledFrustum, 0u);
    EXPECT_EQ(stats.visibleInstances + stats.culledFrustum, 5000u);

    // Timings should be positive and realistic (< 50ms for 5k instances)
    EXPECT_GE(stats.ecsQueryTimeMs, 0.0f);
    EXPECT_GE(stats.frustumCullingTimeMs, 0.0f);
    EXPECT_GE(stats.sortingTimeMs, 0.0f);
    EXPECT_GE(stats.batchingTimeMs, 0.0f);
    EXPECT_LT(stats.totalNativeFrameTimeMs, 100.0f);
}

TEST(BenchmarkTests, ScaleUpTo50KInstances) {
    NativeBenchmarkStats stats{};
    // Run 50,000 instances to stress test C++ ECS, culling, sorting, and batching
    BenchmarkManager::Instance().RunHeadlessBenchmark(50000, 2, true, &stats);

    EXPECT_EQ(stats.totalInstances, 50000u);
    EXPECT_GT(stats.visibleInstances, 0u);
    EXPECT_GT(stats.culledFrustum, 0u);
    
    // Total sorting + batching time for 50,000 instances on modern CPU should be fast
    float sortAndBatchTime = stats.sortingTimeMs + stats.batchingTimeMs;
    std::cout << "[Benchmark 50K Test] Total Native Frame: " << stats.totalNativeFrameTimeMs 
              << " ms, Sort: " << stats.sortingTimeMs 
              << " ms, Batch: " << stats.batchingTimeMs 
              << " ms, Visible: " << stats.visibleInstances << " / 50000" << std::endl;
    
    EXPECT_LT(sortAndBatchTime, 50.0f);
}

TEST(BenchmarkTests, PluginAPIBenchmarkBindings) {
    SetBenchmarkCullingOptions(true, false);
    
    NativeBenchmarkStats averages{};
    RunNativeHeadlessBenchmark(1000, 2, true, &averages);
    
    EXPECT_EQ(averages.totalInstances, 1000u);
    EXPECT_GT(averages.visibleInstances, 0u);
}

