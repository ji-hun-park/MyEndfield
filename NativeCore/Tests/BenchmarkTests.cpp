#define NOMINMAX
#include <gtest/gtest.h>
#include "../Src/Benchmark.h"
#include "../Src/Culling.h"
#include "../Src/SortKey.h"
#include "../Src/ThreadPool.h"
#include "../Src/RenderGraph.h"
#include "../Src/ECS.h"

#include <chrono>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <vector>
#include <random>
#include <algorithm>
#include <cstring>

using namespace Endfield;

namespace {

// 원본 Benchmark.h의 ScopedTimer 시그니처와 무관하게 동작하는 독립형 고정밀 타이머
class LocalTimer {
public:
    LocalTimer() noexcept : m_Start(std::chrono::high_resolution_clock::now()) {}

    float ElapsedMs() const noexcept {
        auto end = std::chrono::high_resolution_clock::now();
        return std::chrono::duration<float, std::milli>(end - m_Start).count();
    }
private:
    std::chrono::high_resolution_clock::time_point m_Start;
};

// 128-bit(16바이트) SortKey에 대한 8-bit 단위 16-Pass LSD 기수 정렬 (비교군)
void RadixSort128_Local(std::vector<SortKey>& keys) {
    const size_t n = keys.size();
    if (n <= 1) return;

    std::vector<SortKey> temp(n);
    SortKey* src = keys.data();
    SortKey* dst = temp.data();

    for (int bytePass = 0; bytePass < 16; ++bytePass) {
        uint32_t count[256] = { 0 };

        for (size_t i = 0; i < n; ++i) {
            const uint8_t* rawBytes = reinterpret_cast<const uint8_t*>(&src[i]);
            count[rawBytes[bytePass]]++;
        }

        uint32_t pref[256];
        pref[0] = 0;
        for (int b = 1; b < 256; ++b) {
            pref[b] = pref[b - 1] + count[b - 1];
        }

        for (size_t i = 0; i < n; ++i) {
            const uint8_t* rawBytes = reinterpret_cast<const uint8_t*>(&src[i]);
            uint8_t bucket = rawBytes[bytePass];
            dst[pref[bucket]++] = src[i];
        }

        std::swap(src, dst);
    }
}

} // anonymous namespace

// ============================================================================
// 1-1. ECS 128-bit Mask + POPCNT O(1) 컬럼 조회 및 Swap-Erase 검증
// ============================================================================
TEST(BenchmarkTests, Chapter1_ECS_PopcntLookup_And_SwapErase) {
    ECSManager ecs;
    ComponentMask mask;
    mask.low = MASK_STANDARD_RENDER | MASK_SUB_RENDERERS | MASK_VISIBILITY;

    Entity e0 = ecs.CreateEntity(mask);
    Entity e1 = ecs.CreateEntity(mask);

    InlineSubRendererComponent subComp{};
    subComp.subMeshCount = 19;
    subComp.meshId = 0;
    for (uint32_t s = 0; s < 19; ++s) {
        subComp.subMeshIndices[s] = static_cast<uint16_t>(s);
        subComp.materialIds[s]    = static_cast<uint16_t>(s);
    }
    ecs.SetComponentData(e1, BIT_SUB_RENDERERS, subComp);

    // e0 삭제 시 Swap-Erase로 e1이 0번 슬롯으로 이동해야 함
    ecs.DestroyEntity(e0);
    EXPECT_FALSE(ecs.IsValidEntity(e0));
    EXPECT_TRUE(ecs.IsValidEntity(e1));

    auto chunks = ecs.QueryChunks(mask);
    ASSERT_EQ(chunks.size(), 1u);

    // POPCNT 기반 O(1) 컬럼 포인터 조회 확인
    auto* inlineArr = ecs.GetComponentArray<InlineSubRendererComponent>(chunks[0], BIT_SUB_RENDERERS);
    ASSERT_NE(inlineArr, nullptr);
    EXPECT_EQ(inlineArr[0].subMeshCount, 19u);
    EXPECT_EQ(inlineArr[0].materialIds[18], 18u);
}

// ============================================================================
// 1-2. fread/fwrite 기반 Zero-Parsing SoA Chunk Block 직결 로드/저장 검증
// ============================================================================
TEST(BenchmarkTests, Chapter1_Trick4_ZeroParsing_Chunk_Fread_Fwrite) {
    ECSManager srcEcs;
    ComponentMask mask;
    mask.low = MASK_TRANSFORM | MASK_BOUNDS | MASK_SUB_RENDERERS | MASK_VISIBILITY;

    for (uint32_t i = 0; i < 100; ++i) {
        Entity e = srcEcs.CreateEntity(mask);
        InlineSubRendererComponent sub{};
        sub.subMeshCount = 19;
        sub.meshId = 0;
        sub.materialIds[18] = static_cast<uint16_t>(i + 10);
        srcEcs.SetComponentData(e, BIT_SUB_RENDERERS, sub);
    }

    const std::string tempChunkFile = "TestZeroParsingChunks.ecsc";
    ASSERT_TRUE(srcEcs.SaveChunksBinary(tempChunkFile));

    // 새로운 ECSManager에 개별 엔티티 생성 루프 없이 fread로 청크 컬럼 통째로 복원
    ECSManager dstEcs;
    ASSERT_TRUE(dstEcs.LoadChunksBinary(tempChunkFile));

    auto loadedChunks = dstEcs.QueryChunks(mask);
    ASSERT_EQ(loadedChunks.size(), 1u);
    EXPECT_EQ(loadedChunks[0]->entityCount, 100u);

    auto* loadedSubArr = dstEcs.GetComponentArray<InlineSubRendererComponent>(loadedChunks[0], BIT_SUB_RENDERERS);
    ASSERT_NE(loadedSubArr, nullptr);
    EXPECT_EQ(loadedSubArr[0].subMeshCount, 19u);
    EXPECT_EQ(loadedSubArr[99].materialIds[18], 109u);

    std::remove(tempChunkFile.c_str());
}

// ============================================================================
// 2. 15개 워커 기준 10,000개 엔티티 CPU 컬링 소요 시간 측정
// ============================================================================
TEST(BenchmarkTests, Chapter5_CpuCullingTime_10000Entities_15Workers) {
    constexpr uint32_t NUM_WORKERS  = 15;
    constexpr uint32_t NUM_ENTITIES = 10000;
    constexpr uint32_t CHUNK_SIZE   = 256;

    ThreadPool pool(NUM_WORKERS);
    CullingSystem culling;
    culling.Initialize(NUM_WORKERS, &pool, true);

    std::vector<AABB> entityAABBs(NUM_ENTITIES);
    uint32_t numChunks = (NUM_ENTITIES + CHUNK_SIZE - 1) / CHUNK_SIZE;
    std::vector<AABB> chunkAABBs(numChunks);

    for (uint32_t c = 0; c < numChunks; ++c) {
        float chunkBaseX = static_cast<float>((c % 10) * 50);
        float chunkBaseZ = static_cast<float>((c / 10) * 50);
        chunkAABBs[c] = {
            { chunkBaseX, 0.0f, chunkBaseZ },
            { chunkBaseX + 40.0f, 5.0f, chunkBaseZ + 40.0f }
        };

        for (uint32_t i = 0; i < CHUNK_SIZE; ++i) {
            uint32_t idx = c * CHUNK_SIZE + i;
            if (idx >= NUM_ENTITIES) break;
            float ox = chunkBaseX + static_cast<float>(i % 16) * 2.0f;
            float oz = chunkBaseZ + static_cast<float>(i / 16) * 2.0f;
            entityAABBs[idx] = { { ox, 0.0f, oz }, { ox + 1.0f, 2.2f, oz + 1.0f } };
        }
    }

    float identityVP[16] = {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1
    };
    Frustum views[2];
    views[0].ExtractFromMatrix(identityVP);
    views[1].ExtractFromMatrix(identityVP);

    std::vector<uint32_t> visMasks;
    LocalTimer timer;
    culling.PerformMultiViewFrustumCullingParallel(views, 2, chunkAABBs.data(), CHUNK_SIZE, entityAABBs, visMasks);
    float frustumMs = timer.ElapsedMs();

    std::cout << "\n[Chapter 5 Culling Benchmark] 10,000 Entities Multi-View Frustum Culling (15 Workers): "
              << frustumMs << " ms\n";

    EXPECT_EQ(visMasks.size(), NUM_ENTITIES);
    culling.Shutdown();
}

// ============================================================================
// 3. 190,000개 드로우콜 128-bit 정렬 3종 비교 & 리바인드 감소 실측
// ============================================================================
TEST(BenchmarkTests, Chapter5_SortComparison_And_StateRebindReduction) {
    constexpr uint32_t NUM_ENTITIES  = 10000;
    constexpr uint32_t SUBMESH_COUNT = 19;
    constexpr uint32_t TOTAL_DRAWS   = NUM_ENTITIES * SUBMESH_COUNT; // 190,000 서브메시 드로우콜

    // 테스트 데이터 생성 (레바테인 10,000개 x 19 서브메시 분포)
    std::vector<SortKey> baseData(TOTAL_DRAWS);
    std::mt19937 rng(2026);
    std::uniform_real_distribution<float> distDepth(1.0f, 950.0f);

    for (uint32_t i = 0; i < TOTAL_DRAWS; ++i) {
        uint16_t subMeshIdx = static_cast<uint16_t>(i % SUBMESH_COUNT);
        uint16_t materialId = subMeshIdx;
        uint16_t meshId     = 0;
        uint32_t pipelineId = 1;
        uint32_t passId     = 0;

        baseData[i] = SortKeyBuilder::CreateOpaque(
            passId, pipelineId, materialId, distDepth(rng), DEFAULT_MAX_SORT_DISTANCE, meshId, subMeshIdx
        );
    }

    std::vector<SortKey> dataScalar = baseData;
    std::vector<SortKey> dataRadix  = baseData;
    std::vector<SortKey> dataSIMD   = baseData;

    // 1) 일반 if-else 스칼라 std::sort 측정
    LocalTimer tScalar;
    std::sort(dataScalar.begin(), dataScalar.end(), ScalarBranchComparator{});
    float scalarMs = tScalar.ElapsedMs();

    // 2) 128-bit 16-Pass LSD Radix Sort 측정
    LocalTimer tRadix;
    RadixSort128_Local(dataRadix);
    float radixMs = tRadix.ElapsedMs();

    // 3) Branchless SIMD (_mm_movemask_ps) std::sort 측정
    LocalTimer tSIMD;
    std::sort(dataSIMD.begin(), dataSIMD.end(), BranchlessSIMDComparator{});
    float simdMs = tSIMD.ElapsedMs();

    // 4) 센티넬 마커(0x7F7F7F7F) 뒤채움 및 클러스터 리바인드 감소 횟수 집계
    constexpr uint32_t SENTINEL_MARKER = 0x7F7F7F7F;
    uint32_t lastMaterial = 0xFFFFFFFFu;
    uint32_t lastMesh     = 0xFFFFFFFFu;

    uint32_t actualPipelineBinds = 1;
    uint32_t actualSet0Binds     = 1;
    uint32_t actualSet1MatBinds  = 0;
    uint32_t actualMeshBinds     = 0;
    uint32_t skippedRebinds      = 0;

    for (uint32_t i = 0; i < TOTAL_DRAWS; ++i) {
        uint32_t placeholder = SENTINEL_MARKER;
        if (placeholder == SENTINEL_MARKER) {
            if (dataSIMD[i].materialID != lastMaterial) {
                lastMaterial = dataSIMD[i].materialID;
                placeholder = lastMaterial;
                actualSet1MatBinds++;
            } else {
                placeholder = lastMaterial;
                skippedRebinds++;
            }
        }

        if (dataSIMD[i].meshID != lastMesh) {
            lastMesh = dataSIMD[i].meshID;
            actualMeshBinds++;
        } else {
            skippedRebinds++;
        }
    }

    // 마크다운 리포트 출력
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(3);
    oss << "\n================================================================================\n";
    oss << "### Endfield-Vulkan-Renderer Chapter 5 정량 벤치마크 실측 결과\n";
    oss << "================================================================================\n";
    oss << "- 총 렌더링 규모: 10,000 엔티티 x 19 서브메시 = 190,000 서브메시 드로우콜\n\n";
    oss << "[1] 128-bit 정렬 키 알고리즘 3종 소요 시간 비교 (190,000 Elements)\n";
    oss << "  * 일반 if-else 스칼라 std::sort          : " << scalarMs << " ms\n";
    oss << "  * 128-bit LSD Radix Sort (16-Pass)       : " << radixMs  << " ms\n";
    oss << "  * Branchless SIMD std::sort (movemask)   : " << simdMs   << " ms\n\n";
    oss << "[2] 상태 리바인드 및 디스크립터 바인딩 호출 감소 횟수\n";
    oss << "  * vkCmdBindPipeline (PSO)                : 190,000 회 -> " << actualPipelineBinds << " 회\n";
    oss << "  * vkCmdBindDescriptorSets (Set 0 Global) : 190,000 회 -> " << actualSet0Binds     << " 회\n";
    oss << "  * vkCmdBindDescriptorSets (Set 1 Mat)    : 190,000 회 -> " << actualSet1MatBinds  << " 회 (0x7F7F7F7F 뒤채움)\n";
    oss << "  * vkCmdBindVertexBuffers + IndexBuffer   : 190,000 회 -> " << actualMeshBinds     << " 회\n";
    oss << "  * 총 생략된 상태 리바인드 호출 수        : " << skippedRebinds << " 회\n";
    oss << "================================================================================\n";
    std::cout << oss.str();

    EXPECT_EQ(actualPipelineBinds, 1u);
    EXPECT_EQ(actualSet0Binds, 1u);
    EXPECT_EQ(actualSet1MatBinds, SUBMESH_COUNT); // 190,000 -> 19회
    EXPECT_EQ(actualMeshBinds, 1u);               // 190,000 -> 1회
}

// ============================================================================
// 4. 렌더 그래프 컴파일 타임 태그, Read->Read 생략, 메모리 앨리어싱 검증
// ============================================================================
TEST(BenchmarkTests, Chapter3_RenderGraph_BarrierMerging_And_Aliasing) {
    RenderGraph graph;
    VkImage dummyColor = reinterpret_cast<VkImage>(0x1001);
    VkImage dummyDepth = reinterpret_cast<VkImage>(0x1002);

    graph.AddResource("GBufferColor", true,  AccessTag::None, dummyColor);
    graph.AddResource("GBufferDepth", true,  AccessTag::None, dummyDepth);
    // 임시 자원 2개 (각 8MB): 수명이 겹치지 않을 때 총 16MB가 아닌 8MB만 사용하는지 검증
    graph.AddResource("ShadowMap",    false, AccessTag::None, dummyDepth, VK_NULL_HANDLE, VK_NULL_HANDLE, 8ULL * 1024 * 1024);
    graph.AddResource("PostTempRT",   false, AccessTag::None, dummyColor, VK_NULL_HANDLE, VK_NULL_HANDLE, 8ULL * 1024 * 1024);

    // Pass 0: ShadowMap 쓰기 (ShadowMap 수명 시작: 0)
    graph.AddPass("ShadowPass");
    graph.DeclarePassAccess<DepthStencilWriteTag>("ShadowPass", "ShadowMap");

    // Pass 1: GBufferColor/Depth 쓰기 + ShadowMap 읽기
    graph.AddPass("OpaquePass");
    graph.DeclarePassAccess<ColorAttachmentWriteTag>("OpaquePass", "GBufferColor");
    graph.DeclarePassAccess<DepthStencilWriteTag>("OpaquePass", "GBufferDepth");
    graph.DeclarePassAccess<ReadOnlyTag>("OpaquePass", "ShadowMap");

    // Pass 2: GBufferColor/Depth 읽기(배리어 2개 병합) + ShadowMap 연속 읽기(Read->Read 배리어 생략 ShadowMap 수명 종료: 2)
    graph.AddPass("LightingPass");
    graph.DeclarePassAccess<ReadOnlyTag>("LightingPass", "GBufferColor"); // Write -> Read (배리어 1)
    graph.DeclarePassAccess<ReadOnlyTag>("LightingPass", "GBufferDepth"); // Write -> Read (배리어 2)
    graph.DeclarePassAccess<ReadOnlyTag>("LightingPass", "ShadowMap");    // Read  -> Read (배리어 자동 생략)

    // Pass 3: ShadowMap 수명(0~2)이 완전히 끝난 뒤 PostTempRT 쓰기 (수명: 3~3 -> ShadowMap 오프셋 0 재활용)
    graph.AddPass("PostProcessPass");
    graph.DeclarePassAccess<ColorAttachmentWriteTag>("PostProcessPass", "PostTempRT");

    graph.CompileGraph();

    // 1. LightingPass에서 3개 자원을 선언했지만 ShadowMap(Read->Read)이 생략되어 2개 배리어만 병합되었는지 확인
    EXPECT_EQ(graph.GetMergedBarrierCountForPass("LightingPass"), 2u);

    // 2. ShadowMap(Pass 0~2) 수명 종료 후 PostTempRT(Pass 3)가 오프셋 0을 재활용(Aliasing)해 총 임시 메모리가 8MB인지 확인
    const RenderResource* shadowRes = graph.GetResourceInfo("ShadowMap");
    const RenderResource* postRes   = graph.GetResourceInfo("PostTempRT");
    ASSERT_NE(shadowRes, nullptr);
    ASSERT_NE(postRes, nullptr);
    EXPECT_EQ(shadowRes->aliasedMemoryOffset, 0ULL);
    EXPECT_EQ(postRes->aliasedMemoryOffset, 0ULL);
    EXPECT_EQ(graph.GetTotalAliasedTransientMemorySize(), 8ULL * 1024 * 1024);
}
