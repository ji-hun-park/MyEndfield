#pragma once
#include <cstdint>
#include <algorithm>
#include <immintrin.h> // SSE2 Intrinsics (_mm_load_si128, _mm_cmpgt_epi32, _mm_movemask_ps)

namespace Endfield {

// [Chapter 2.3 - Slide 47] 128-bit (16-byte) Sort Key for unified sorting and batching
// Little-endian & SSE2 레인 기준:
// words[3] (Bits 96..127, MSB, movemask Bit 3) : Pass / Queue (32-bit)
// words[2] (Bits 64..95,       movemask Bit 2) : PSO / Shader (32-bit)
// words[1] (Bits 32..63,       movemask Bit 1) : Mesh (16-bit) | Submesh (16-bit)
// words[0] (Bits 0..31,   LSB, movemask Bit 0) : Material (16-bit) | Depth (16-bit)
struct alignas(16) SortKey {
    union {
        __m128i simdValue;
        uint32_t words[4];
        struct {
            uint64_t value;     // 하위 64비트 (기존 코드 하위 호환용)
            uint64_t highValue; // 상위 64비트
        };
        struct {
            // Word 0 [0:31] (LSB): Material & Depth (32b)
            uint32_t depth      : 16;
            uint32_t materialID : 16;

            // Word 1 [32:63]: Mesh & Submesh (32b)
            uint32_t subMeshID  : 16;
            uint32_t meshID     : 16;

            // Word 2 [64:95]: PSO / Shader (32b)
            uint32_t pipelineID;

            // Word 3 [96:127] (MSB): Pass / Queue (32b - 최우선 정렬)
            uint32_t passID;
        };
    };

    SortKey() noexcept {
        simdValue = _mm_setzero_si128();
    }

    // [Chapter 2.3] Branchless SIMD 128-bit 비교자
    // if-else 체인 없이 SSE2 명령어와 _mm_movemask_ps 가중치(Bit 3 > Bit 2 > Bit 1 > Bit 0)만으로 대소 판별
    static inline bool LessSIMD(const SortKey& a, const SortKey& b) noexcept {
        __m128i va = _mm_load_si128(&a.simdValue);
        __m128i vb = _mm_load_si128(&b.simdValue);

        // _mm_cmpgt_epi32는 부호 있는(signed) 32비트 비교이므로,
        // 부호 비트(0x80000000)를 XOR 반전시켜 부호 없는(unsigned) 32비트 비교로 변환
        const __m128i signFlip = _mm_set1_epi32(static_cast<int>(0x80000000u));
        __m128i vaUnsigned = _mm_xor_si128(va, signFlip);
        __m128i vbUnsigned = _mm_xor_si128(vb, signFlip);

        // 각 32비트 레인별로 (a < b) 및 (a > b) 마스크 동시 계산
        __m128i ltVec = _mm_cmpgt_epi32(vbUnsigned, vaUnsigned);
        __m128i gtVec = _mm_cmpgt_epi32(vaUnsigned, vbUnsigned);

        int ltMask = _mm_movemask_ps(_mm_castsi128_ps(ltVec));
        int gtMask = _mm_movemask_ps(_mm_castsi128_ps(gtVec));
        return ltMask > gtMask;
    }

    // [Chapter 5 벤치마크 비교용] 일반 if-else 분기 기반 스칼라 비교자
    static inline bool LessScalarBranch(const SortKey& a, const SortKey& b) noexcept {
        if (a.passID != b.passID)         return a.passID < b.passID;
        if (a.pipelineID != b.pipelineID) return a.pipelineID < b.pipelineID;
        if (a.words[1] != b.words[1])     return a.words[1] < b.words[1];
        return a.words[0] < b.words[0];
    }

    inline bool operator<(const SortKey& other) const noexcept {
        return LessSIMD(*this, other);
    }

    inline bool operator==(const SortKey& other) const noexcept {
        __m128i xorVec = _mm_xor_si128(_mm_load_si128(&simdValue), _mm_load_si128(&other.simdValue));
        return _mm_movemask_epi8(_mm_cmpeq_epi8(xorVec, _mm_setzero_si128())) == 0xFFFF;
    }
};

// std::sort 주입용 함수 객체 (Functor)
struct BranchlessSIMDComparator {
    inline bool operator()(const SortKey& a, const SortKey& b) const noexcept {
        return SortKey::LessSIMD(a, b);
    }
};

struct ScalarBranchComparator {
    inline bool operator()(const SortKey& a, const SortKey& b) const noexcept {
        return SortKey::LessScalarBranch(a, b);
    }
};

constexpr float DEFAULT_MAX_SORT_DISTANCE = 1000.0f;
constexpr uint16_t MAX_DEPTH_VAL = 0xFFFF;

class SortKeyBuilder {
public:
    // 불투명(Opaque) 오브젝트용 키 생성: Front-to-Back 정렬 (가까운 것이 먼저)
    // 기본 인자(meshID = 0, subMeshID = 0)를 통해 기존 4~5개 인자 호출 코드와 100% 호환
    static SortKey CreateOpaque(
        uint32_t passID,
        uint32_t pipelineID,
        uint16_t materialID,
        float distance,
        float maxDistance = DEFAULT_MAX_SORT_DISTANCE,
        uint16_t meshID = 0,
        uint16_t subMeshID = 0)
    {
        SortKey key;
        key.passID = passID;
        key.pipelineID = pipelineID;
        key.meshID = meshID;
        key.subMeshID = subMeshID;
        key.materialID = materialID;

        float normalizedDepth = std::clamp(distance / maxDistance, 0.0f, 1.0f);
        key.depth = static_cast<uint16_t>(normalizedDepth * MAX_DEPTH_VAL);
        return key;
    }

    // 반투명(Transparent) 오브젝트용 키 생성: Back-to-Front 정렬 (먼 것이 먼저)
    static SortKey CreateTransparent(
        uint32_t passID,
        uint32_t pipelineID,
        uint16_t materialID,
        float distance,
        float maxDistance = DEFAULT_MAX_SORT_DISTANCE,
        uint16_t meshID = 0,
        uint16_t subMeshID = 0)
    {
        SortKey key;
        key.passID = passID;
        key.pipelineID = pipelineID;
        key.meshID = meshID;
        key.subMeshID = subMeshID;
        key.materialID = materialID;

        float normalizedDepth = std::clamp(distance / maxDistance, 0.0f, 1.0f);
        uint16_t depthVal = static_cast<uint16_t>(normalizedDepth * MAX_DEPTH_VAL);
        key.depth = MAX_DEPTH_VAL - depthVal;
        return key;
    }
};

} // namespace Endfield
