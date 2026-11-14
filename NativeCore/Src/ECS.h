#pragma once
#include <cstdint>
#include <vector>
#include <memory>
#include <atomic>
#include <string>

#if defined(_MSC_VER)
#include <intrin.h> // __popcnt64
#endif

namespace Endfield {

constexpr uint32_t CHUNK_CAPACITY = 1024;
constexpr uint32_t MAX_COMPONENTS = 128; // 128-bit 마스크에 대응
constexpr uint32_t CACHE_LINE_SIZE = 64; // CPU 캐시 라인 정렬 (64 Bytes)
constexpr uint32_t MAX_INLINE_SUB_RENDERERS = 64; // 레바테인 19개 서브메시를 포인터 점프 없이 인라인 수납할 고정 버킷 크기

// 128-bit fixed mask for component types
struct ComponentMask {
    uint64_t low = 0;
    uint64_t high = 0;

    bool operator==(const ComponentMask& other) const noexcept {
        return low == other.low && high == other.high;
    }

    bool Contains(const ComponentMask& query) const noexcept {
        return (low & query.low) == query.low && (high & query.high) == query.high;
    }
};

enum ComponentBitIndex : uint32_t {
    BIT_TRANSFORM     = 1,
    BIT_BOUNDS        = 2,
    BIT_MESH          = 3,
    BIT_SUB_RENDERERS = 4, // 가변 길이 서브 렌더러 인라인 버킷 컬럼
    BIT_VISIBILITY    = 5  // 32-bit 뷰 병렬 가시성 마스크 컬럼
};

constexpr uint64_t MASK_TRANSFORM     = 1ULL << BIT_TRANSFORM;
constexpr uint64_t MASK_BOUNDS        = 1ULL << BIT_BOUNDS;
constexpr uint64_t MASK_MESH          = 1ULL << BIT_MESH;
constexpr uint64_t MASK_SUB_RENDERERS = 1ULL << BIT_SUB_RENDERERS;
constexpr uint64_t MASK_VISIBILITY    = 1ULL << BIT_VISIBILITY;

constexpr uint64_t MASK_STANDARD_RENDER = MASK_TRANSFORM | MASK_BOUNDS | MASK_MESH;

// 렌더링에 사용되는 핵심 데이터 컴포넌트 구조체 (SoA 메모리 매핑용)
struct alignas(16) TransformComponent {
    float localToWorld[16];
};

struct BoundsComponent {
    float minBounds[3];
    float maxBounds[3];
};

struct MeshComponent {
    int32_t meshId;
    int32_t subMeshIndex;
    int32_t materialId;
};

// 외부 힙(std::vector) 포인터 점프 없이
// 캐릭터 1개의 19개 서브메시 렌더러 정보를 청크 내부 컬럼에 직접 인라인 수납하는 구조체
struct alignas(64) InlineSubRendererComponent {
    uint32_t subMeshCount = 0; // 레바테인 기준 19
    int32_t  meshId = 0;
    uint16_t subMeshIndices[MAX_INLINE_SUB_RENDERERS] = {0};
    uint16_t materialIds[MAX_INLINE_SUB_RENDERERS] = {0};
};

// 8-byte Generational Index Entity Handle
struct Entity {
    uint32_t id;
    uint32_t revision;
};
using EntityHandle = Entity;

class ComponentRegistry {
public:
    static void RegisterComponent(uint32_t bitIndex, size_t size);
    static size_t GetComponentSize(uint32_t bitIndex);
    static std::vector<uint32_t> GetIndicesFromMask(const ComponentMask& mask);
private:
    static size_t s_ComponentSizes[MAX_COMPONENTS];
};

// 하드웨어 POPCNT 명령어 래퍼
inline uint32_t Popcnt64(uint64_t value) noexcept {
#if defined(_MSC_VER)
    return static_cast<uint32_t>(__popcnt64(value));
#else
    return static_cast<uint32_t>(__builtin_popcountll(value));
#endif
}

// Base chunk for SoA (Structure of Arrays) memory layout
struct alignas(64) Chunk {
    ComponentMask mask;
    uint32_t entityCount = 0;

    // 64바이트 캐시 라인 정렬된 컴포넌트 컬럼 배열 포인터들
    std::vector<void*> columns;
    std::vector<uint32_t> componentIndices;

    // 해당 청크에 저장된 엔티티 핸들 배열 (역참조용)
    Entity* entityArray = nullptr;

    Chunk(const ComponentMask& m);
    ~Chunk();

    uint32_t AddEntity(Entity entity);

    // 마스크가 곧 컬럼 주소 (128-bit Mask + POPCNT 3사이클 O(1) 조회)
    // unordered_map이나 for/switch 없이 하위 비트 마스크 AND 연산 후 POPCNT로 즉시 컬럼 인덱스를 계산함
    static constexpr uint32_t INVALID_COLUMN = 0xFFFFFFFFu;

    inline uint32_t GetColumnIndexFast(uint32_t componentBitIndex) const noexcept {
        if (componentBitIndex < 64) {
            uint64_t bit = 1ULL << componentBitIndex;
            if ((mask.low & bit) == 0) return INVALID_COLUMN;
            uint64_t lowerBitsMask = bit - 1ULL;
            return Popcnt64(mask.low & lowerBitsMask);
        } else if (componentBitIndex < 128) {
            uint64_t bit = 1ULL << (componentBitIndex - 64);
            if ((mask.high & bit) == 0) return INVALID_COLUMN;
            uint64_t lowerBitsMask = bit - 1ULL;
            return Popcnt64(mask.low) + Popcnt64(mask.high & lowerBitsMask);
        }
        return INVALID_COLUMN;
    }
};

struct Archetype {
    ComponentMask mask;
    std::vector<std::unique_ptr<Chunk>> chunks;
};

struct EntityRecord {
    Chunk* chunk = nullptr;
    uint32_t index = 0;
    uint32_t revision = 0;
};

class ECSManager {
public:
    ECSManager();
    ~ECSManager();

    Entity CreateEntity(const ComponentMask& mask);
    void DestroyEntity(Entity entity); // Swap-Erase O(1) 삭제 + 잡 펜스 보호
    void Clear();

    // 세대 인덱스(Revision) 일치 여부를 검사해 Use-After-Free(ABA 문제)를 방어하는 검증 함수
    bool IsValidEntity(Entity entity) const noexcept {
        if (entity.id >= m_EntityRecords.size()) return false;
        const EntityRecord& rec = m_EntityRecords[entity.id];
        return (rec.chunk != nullptr) && (rec.revision == entity.revision);
    }

    // 워커 스레드 순회 보호용 경량 잡 펜스 제어
    void AcquireWorkerReadFence() noexcept { m_ActiveWorkerJobs.fetch_add(1, std::memory_order_acquire); }
    void ReleaseWorkerReadFence() noexcept { m_ActiveWorkerJobs.fetch_sub(1, std::memory_order_release); }
    void WaitJobFence() const noexcept {
        while (m_ActiveWorkerJobs.load(std::memory_order_acquire) > 0) {
            // Spin-wait until all worker jobs finish reading chunks
        }
    }

    std::vector<Chunk*> QueryChunks(const ComponentMask& queryMask);

    // POPCNT 기반 O(1) 컬럼 포인터 획득 (선형 탐색 루프 완전 제거)
    template<typename T>
    T* GetComponentArray(Chunk* chunk, uint32_t componentBitIndex) const noexcept {
        if (!chunk) return nullptr;
        uint32_t colIdx = chunk->GetColumnIndexFast(componentBitIndex);
        if (colIdx == Chunk::INVALID_COLUMN || colIdx >= chunk->columns.size()) {
            return nullptr;
        }
        return static_cast<T*>(chunk->columns[colIdx]);
    }

    template<typename T>
    void SetComponentData(Entity entity, uint32_t componentBitIndex, const T& data) {
        if (IsValidEntity(entity)) {
            const EntityRecord& record = m_EntityRecords[entity.id];
            T* array = GetComponentArray<T>(record.chunk, componentBitIndex);
            if (array) {
                array[record.index] = data;
            }
        }
    }

    // 개별 엔티티 생성 루프 없이 count개 분량의 64B 정렬 청크를 즉시 할당
    Chunk* AllocateChunkRaw(const ComponentMask& mask, uint32_t count);

    // 청크 내부의 모든 SoA 컬럼을 디스크 파일로 단 1회씩 fwrite 직결 덤프
    bool SaveChunksBinary(const std::string& filePath) const;

    // 디스크의 바이너리 블록을 파싱 없이 청크의 SoA 컬럼 메모리로 1회씩 fread 직결 로드
    bool LoadChunksBinary(const std::string& filePath);

private:
    std::vector<std::unique_ptr<Archetype>> m_Archetypes;
    std::vector<EntityRecord> m_EntityRecords;
    std::vector<uint32_t> m_FreeEntityIds;
    std::atomic<uint32_t> m_ActiveWorkerJobs{0};

    Archetype* GetOrCreateArchetype(const ComponentMask& mask);
};

} // namespace Endfield
