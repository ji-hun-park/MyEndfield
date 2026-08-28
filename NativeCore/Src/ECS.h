#pragma once
#include <cstdint>
#include <vector>
#include <memory>
#include <unordered_map>

namespace Endfield {

constexpr uint32_t CHUNK_CAPACITY = 1024;
constexpr uint32_t MAX_COMPONENTS = 128; // 128-bit 마스크에 대응

// Rule 2: 128-bit fixed mask for component types
struct ComponentMask {
    uint64_t low = 0;
    uint64_t high = 0;

    bool operator==(const ComponentMask& other) const {
        return low == other.low && high == other.high;
    }
    
    // this 마스크가 query 마스크를 포함(모두 가짐)하는지 검사
    bool Contains(const ComponentMask& query) const {
        return (low & query.low) == query.low && (high & query.high) == query.high;
    }
};

// 8-byte Entity Handle
struct Entity {
    uint32_t id;
    uint32_t revision;
};

// 컴포넌트의 크기 정보를 저장하는 전역/정적 레지스트리
class ComponentRegistry {
public:
    static void RegisterComponent(uint32_t bitIndex, size_t size);
    static size_t GetComponentSize(uint32_t bitIndex);
    static std::vector<uint32_t> GetIndicesFromMask(const ComponentMask& mask);
private:
    static size_t s_ComponentSizes[MAX_COMPONENTS];
};

// Base chunk for SoA (Structure of Arrays) memory layout
struct Chunk {
    ComponentMask mask;
    uint32_t entityCount = 0;
    
    // 컴포넌트 데이터가 저장되는 연속된 메모리 배열(SoA) 포인터들
    std::vector<void*> columns;
    // columns 배열의 각 원소가 어떤 컴포넌트(bitIndex)인지 매핑
    std::vector<uint32_t> componentIndices; 
    
    // 해당 청크에 저장된 엔티티 ID 배열 (역참조용)
    Entity* entityArray = nullptr;

    Chunk(const ComponentMask& m);
    ~Chunk();

    // 청크에 엔티티 할당 후 청크 내 인덱스 반환
    uint32_t AddEntity(Entity entity);
};

// 동일한 ComponentMask를 가지는 청크들의 집합
struct Archetype {
    ComponentMask mask;
    std::vector<std::unique_ptr<Chunk>> chunks;
};

struct EntityRecord {
    Chunk* chunk;
    uint32_t index;
};

// Custom ECS Manager to bypass Unity DOTS
class ECSManager {
public:
    ECSManager();
    ~ECSManager();

    Entity CreateEntity(const ComponentMask& mask);
    void DestroyEntity(Entity entity); // Swap-and-pop implementation

    // Rule 2: 마스크를 통한 빠른 청크 필터링
    std::vector<Chunk*> QueryChunks(const ComponentMask& queryMask);

    // Direct pointer arithmetic access
    template<typename T>
    T* GetComponentArray(Chunk* chunk, uint32_t componentBitIndex) {
        for (size_t i = 0; i < chunk->componentIndices.size(); ++i) {
            if (chunk->componentIndices[i] == componentBitIndex) {
                return static_cast<T*>(chunk->columns[i]);
            }
        }
        return nullptr;
    }

private:
    std::vector<std::unique_ptr<Archetype>> m_Archetypes;
    std::unordered_map<uint32_t, EntityRecord> m_EntityMap;
    uint32_t m_NextEntityId = 1;

    Archetype* GetOrCreateArchetype(const ComponentMask& mask);
};

} // namespace Endfield
