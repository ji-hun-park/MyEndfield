#include "ECS.h"
#include "VulkanBackend.h"
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
    
    VulkanBackend::LogToUnity("[ECS] Created new Archetype (Mask Low: " + std::to_string(mask.low) + ", High: " + std::to_string(mask.high) + ")");
    return ptr;
}

Entity ECSManager::CreateEntity(const ComponentMask& mask) {
    uint32_t id;
    uint32_t revision = 1;
    if (!m_FreeEntityIds.empty()) {
        id = m_FreeEntityIds.back();
        m_FreeEntityIds.pop_back();
        revision = m_EntityRecords[id].revision + 1;
    } else {
        id = static_cast<uint32_t>(m_EntityRecords.size());
        m_EntityRecords.emplace_back(); // default initialization
    }

    Entity ent = { id, revision };
    Archetype* arch = GetOrCreateArchetype(mask);

    Chunk* targetChunk = nullptr;
    for (auto& chunk : arch->chunks) {
        if (chunk->entityCount < CHUNK_CAPACITY) {
            targetChunk = chunk.get();
            break;
        }
    }

    if (!targetChunk) {
        arch->chunks.push_back(std::make_unique<Chunk>(mask));
        targetChunk = arch->chunks.back().get();
        VulkanBackend::LogToUnity("[ECS] Allocated new Chunk for Archetype.");
    }

    uint32_t index = targetChunk->AddEntity(ent);
    m_EntityRecords[id] = { targetChunk, index, revision };

    return ent;
}

void ECSManager::DestroyEntity(Entity entity) {
    if (entity.id >= m_EntityRecords.size()) return;
    EntityRecord& record = m_EntityRecords[entity.id];
    
    // Check revision to prevent double delete or deleting recycled entity
    if (record.revision != entity.revision || record.chunk == nullptr) return;

    Chunk* chunk = record.chunk;
    uint32_t deleteIndex = record.index;
    uint32_t lastIndex = chunk->entityCount - 1;

    // 만약 삭제하려는 엔티티가 청크의 마지막 엔티티가 아니라면, 마지막 엔티티를 삭제 위치로 복사 (Swap and pop)
    if (deleteIndex != lastIndex) {
        // 1. 마지막 엔티티의 ID를 삭제 위치로 이동
        Entity lastEntity = chunk->entityArray[lastIndex];
        chunk->entityArray[deleteIndex] = lastEntity;

        // 2. 각 컴포넌트(SoA Column)의 데이터를 복사 (메모리 덮어쓰기)
        for (size_t i = 0; i < chunk->columns.size(); ++i) {
            size_t compSize = ComponentRegistry::GetComponentSize(chunk->componentIndices[i]);
            if (compSize > 0 && chunk->columns[i] != nullptr) {
                uint8_t* compArray = static_cast<uint8_t*>(chunk->columns[i]);
                std::memcpy(
                    compArray + (deleteIndex * compSize),
                    compArray + (lastIndex * compSize),
                    compSize
                );
            }
        }

        // 3. 이동된 마지막 엔티티의 매핑 정보 갱신
        m_EntityRecords[lastEntity.id].index = deleteIndex;
    }

    // 엔티티 개수를 줄이고(pop), 삭제된 엔티티 맵에서 제거
    chunk->entityCount--;
    record.chunk = nullptr;
    
    m_FreeEntityIds.push_back(entity.id);
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

