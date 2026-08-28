#include "ECS.h"
#include <cstdlib>
#include <cstring>
#include <iostream>

namespace Endfield {

size_t ComponentRegistry::s_ComponentSizes[MAX_COMPONENTS] = {0};

void ComponentRegistry::RegisterComponent(uint32_t bitIndex, size_t size) {
    if (bitIndex < MAX_COMPONENTS) {
        s_ComponentSizes[bitIndex] = size;
    }
}

size_t ComponentRegistry::GetComponentSize(uint32_t bitIndex) {
    if (bitIndex < MAX_COMPONENTS) {
        return s_ComponentSizes[bitIndex];
    }
    return 0;
}

std::vector<uint32_t> ComponentRegistry::GetIndicesFromMask(const ComponentMask& mask) {
    std::vector<uint32_t> indices;
    // 하위 64비트 검사
    for (uint32_t i = 0; i < 64; ++i) {
        if ((mask.low >> i) & 1ULL) {
            indices.push_back(i);
        }
    }
    // 상위 64비트 검사
    for (uint32_t i = 0; i < 64; ++i) {
        if ((mask.high >> i) & 1ULL) {
            indices.push_back(i + 64);
        }
    }
    return indices;
}

// ---------------------------------------------------------
// Chunk Implementation
// ---------------------------------------------------------

Chunk::Chunk(const ComponentMask& m) : mask(m), entityCount(0) {
    componentIndices = ComponentRegistry::GetIndicesFromMask(mask);
    
    // SoA(Structure of Arrays) 방식 메모리 할당:
    // 청크 하나에 대해 각 컴포넌트 별로 CHUNK_CAPACITY 만큼의 크기를 일괄 연속 할당하거나 개별 할당합니다.
    // 여기서는 캐시 최적화를 위해 개별 배열(Column) 단위로 할당합니다.
    columns.resize(componentIndices.size(), nullptr);
    for (size_t i = 0; i < componentIndices.size(); ++i) {
        size_t compSize = ComponentRegistry::GetComponentSize(componentIndices[i]);
        if (compSize > 0) {
            // C++ 17 aligned allocation can be used for SIMD alignment, using malloc here for simplicity
            columns[i] = std::malloc(compSize * CHUNK_CAPACITY);
            std::memset(columns[i], 0, compSize * CHUNK_CAPACITY); // Zero initialize
        }
    }

    // 엔티티 ID를 기록할 메타 배열 할당
    entityArray = static_cast<Entity*>(std::malloc(sizeof(Entity) * CHUNK_CAPACITY));
}

Chunk::~Chunk() {
    for (void* col : columns) {
        if (col) {
            std::free(col);
        }
    }
    if (entityArray) {
        std::free(entityArray);
    }
}

uint32_t Chunk::AddEntity(Entity entity) {
    if (entityCount >= CHUNK_CAPACITY) return -1;
    
    uint32_t index = entityCount++;
    entityArray[index] = entity;
    return index;
}

// ---------------------------------------------------------
// ECSManager Implementation
// ---------------------------------------------------------

ECSManager::ECSManager() {
}

ECSManager::~ECSManager() {
    m_Archetypes.clear();
}

Archetype* ECSManager::GetOrCreateArchetype(const ComponentMask& mask) {
    for (auto& arch : m_Archetypes) {
        if (arch->mask == mask) {
            return arch.get();
        }
    }

    // 아키타입이 없으면 새로 생성
    auto newArch = std::make_unique<Archetype>();
    newArch->mask = mask;
    Archetype* ptr = newArch.get();
    m_Archetypes.push_back(std::move(newArch));
    
    std::cout << "[ECS] Created new Archetype (Mask Low: " << mask.low << ", High: " << mask.high << ")\n";
    return ptr;
}

Entity ECSManager::CreateEntity(const ComponentMask& mask) {
    Entity ent = { m_NextEntityId++, 1 };
    Archetype* arch = GetOrCreateArchetype(mask);

    // 해당 아키타입 내에 자리가 남은 청크 찾기
    Chunk* targetChunk = nullptr;
    for (auto& chunk : arch->chunks) {
        if (chunk->entityCount < CHUNK_CAPACITY) {
            targetChunk = chunk.get();
            break;
        }
    }

    // 남은 청크가 없으면 새로 청크 메모리 할당
    if (!targetChunk) {
        arch->chunks.push_back(std::make_unique<Chunk>(mask));
        targetChunk = arch->chunks.back().get();
        std::cout << "[ECS] Allocated new Chunk for Archetype.\n";
    }

    targetChunk->AddEntity(ent);
    return ent;
}

void ECSManager::DestroyEntity(Entity entity) {
    // 단순화된 Destroy: 실제 구현에선 Chunk를 순회하거나 HashMap 인덱스로 엔티티 위치를 찾아야 함
    // 삭제 시 Chunk의 마지막 엔티티를 삭제 대상 인덱스로 옮기고 entityCount-- 처리 (Swap-and-pop)
}

std::vector<Chunk*> ECSManager::QueryChunks(const ComponentMask& queryMask) {
    std::vector<Chunk*> result;
    // 모든 아키타입을 순회하면서 마스크 필터링
    for (const auto& arch : m_Archetypes) {
        if (arch->mask.Contains(queryMask)) {
            for (const auto& chunk : arch->chunks) {
                if (chunk->entityCount > 0) {
                    result.push_back(chunk.get());
                }
            }
        }
    }
    return result;
}

} // namespace Endfield

