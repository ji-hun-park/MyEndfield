#pragma once
#include <cstdint>
#include <algorithm>

namespace Endfield {

// Rule 4: 64-bit Sort Key for unified sorting and batching
// Little-endian 환경 기준: 가장 아래(LSB)부터 정의된 필드가 uint64_t의 하위 비트를 차지합니다.
// 비교( < ) 시에는 가장 상위 비트(MSB)가 가장 큰 영향력을 가집니다.
struct SortKey {
    union {
        uint64_t value;
        struct {
            uint64_t depth      : 16; // [0:15]  - Distance/Depth 
            uint64_t materialID : 16; // [16:31] - Material & Mesh ID
            uint64_t pipelineID : 16; // [32:47] - Shader / Pipeline State
            uint64_t passID     : 16; // [48:63] - Render Pass Order (가장 최우선 정렬)
        };
    };

    bool operator<(const SortKey& other) const {
        return value < other.value; 
    }

    bool operator==(const SortKey& other) const {
        return value == other.value;
    }
};

constexpr float DEFAULT_MAX_SORT_DISTANCE = 1000.0f;
constexpr uint16_t MAX_DEPTH_VAL = 0xFFFF;

class SortKeyBuilder {
public:
    // 불투명(Opaque) 오브젝트용 키 생성: Front-to-Back 정렬 (가까운 것이 먼저)
    static SortKey CreateOpaque(uint16_t passID, uint16_t pipelineID, uint16_t materialID, float distance, float maxDistance = DEFAULT_MAX_SORT_DISTANCE) {
        SortKey key;
        key.passID = passID;
        key.pipelineID = pipelineID;
        key.materialID = materialID;
        
        // 거리가 가까울수록 값이 작아지도록 매핑 (0 ~ 0xFFFF)
        float normalizedDepth = std::clamp(distance / maxDistance, 0.0f, 1.0f);
        key.depth = static_cast<uint16_t>(normalizedDepth * MAX_DEPTH_VAL);
        return key;
    }

    // 반투명(Transparent) 오브젝트용 키 생성: Back-to-Front 정렬 (먼 것이 먼저)
    static SortKey CreateTransparent(uint16_t passID, uint16_t pipelineID, uint16_t materialID, float distance, float maxDistance = DEFAULT_MAX_SORT_DISTANCE) {
        SortKey key;
        key.passID = passID;
        key.pipelineID = pipelineID;
        key.materialID = materialID;
        
        // 반투명은 멀리 있는 것(큰 distance)이 먼저 그려져야 하므로, 값을 반전(Invert)시킵니다.
        // 역순 정렬을 위해 0xFFFF에서 빼줌 (값이 작을수록 먼저 그려짐 -> 역전되어 먼 것이 먼저 그려짐)
        float normalizedDepth = std::clamp(distance / maxDistance, 0.0f, 1.0f);
        uint16_t depthVal = static_cast<uint16_t>(normalizedDepth * MAX_DEPTH_VAL);
        key.depth = MAX_DEPTH_VAL - depthVal; 
        return key;
    }
};

} // namespace Endfield

