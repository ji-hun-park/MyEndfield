#include "ECS.h"
#include "VulkanBackend.h"
#include <cstdlib>
#include <cstring>
#include <iostream>

#if defined(_MSC_VER) || defined(_WIN32)
#include <malloc.h> // _aligned_malloc, _aligned_free
#endif

namespace Endfield {

namespace {
    inline void* AlignedAlloc64(size_t size) {
        if (size == 0) return nullptr;
        size_t alignedSize = (size + CACHE_LINE_SIZE - 1) & ~(static_cast<size_t>(CACHE_LINE_SIZE) - 1);
    #if defined(_MSC_VER) || defined(_WIN32)
        return _aligned_malloc(alignedSize, CACHE_LINE_SIZE);
    #else
        return std::aligned_alloc(CACHE_LINE_SIZE, alignedSize);
    #endif
    }

    inline void AlignedFree64(void* ptr) {
        if (!ptr) return;
    #if defined(_MSC_VER) || defined(_WIN32)
        _aligned_free(ptr);
    #else
        std::free(ptr);
    #endif
    }
}

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
    indices.reserve(Popcnt64(mask.low) + Popcnt64(mask.high));

    for (uint32_t i = 0; i < 64; ++i) {
        if ((mask.low >> i) & 1ULL) {
            indices.push_back(i);
        }
    }
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

    // SoA(Structure of Arrays) 컬럼별 64바이트 캐시 라인 정렬 할당
    columns.resize(componentIndices.size(), nullptr);
    for (size_t i = 0; i < componentIndices.size(); ++i) {
        size_t compSize = ComponentRegistry::GetComponentSize(componentIndices[i]);
        if (compSize > 0) {
            size_t totalBytes = compSize * CHUNK_CAPACITY;
            columns[i] = AlignedAlloc64(totalBytes);
            std::memset(columns[i], 0, totalBytes);
        }
    }

    entityArray = static_cast<Entity*>(AlignedAlloc64(sizeof(Entity) * CHUNK_CAPACITY));
    if (entityArray) {
        std::memset(entityArray, 0, sizeof(Entity) * CHUNK_CAPACITY);
    }
}

Chunk::~Chunk() {
    for (void* col : columns) {
        if (col) {
            AlignedFree64(col);
        }
    }
    if (entityArray) {
        AlignedFree64(entityArray);
    }
}

uint32_t Chunk::AddEntity(Entity entity) {
    if (entityCount >= CHUNK_CAPACITY) return static_cast<uint32_t>(-1);

    uint32_t index = entityCount++;
    entityArray[index] = entity;
    return index;
}

// ---------------------------------------------------------
// ECSManager Implementation
// ---------------------------------------------------------

ECSManager::ECSManager() {
    // 기본 내장 컴포넌트 크기 자동 등록
    ComponentRegistry::RegisterComponent(BIT_TRANSFORM,     sizeof(TransformComponent));
    ComponentRegistry::RegisterComponent(BIT_BOUNDS,        sizeof(BoundsComponent));
    ComponentRegistry::RegisterComponent(BIT_MESH,          sizeof(MeshComponent));
    ComponentRegistry::RegisterComponent(BIT_SUB_RENDERERS, sizeof(InlineSubRendererComponent));
    ComponentRegistry::RegisterComponent(BIT_VISIBILITY,    sizeof(uint32_t));
}

ECSManager::~ECSManager() {
    WaitJobFence();
    m_Archetypes.clear();
}

Archetype* ECSManager::GetOrCreateArchetype(const ComponentMask& mask) {
    for (auto& arch : m_Archetypes) {
        if (arch->mask == mask) {
            return arch.get();
        }
    }

    auto newArch = std::make_unique<Archetype>();
    newArch->mask = mask;
    Archetype* ptr = newArch.get();
    m_Archetypes.push_back(std::move(newArch));

    VulkanBackend::LogToUnity("[ECS] Created new Archetype (Mask Low: " + std::to_string(mask.low) + ", High: " + std::to_string(mask.high) + ")");
    return ptr;
}

Entity ECSManager::CreateEntity(const ComponentMask& mask) {
    // 구조적 변경 전 워커 스레드 잡 펜스 대기
    WaitJobFence();

    uint32_t id;
    uint32_t revision = 1;
    if (!m_FreeEntityIds.empty()) {
        id = m_FreeEntityIds.back();
        m_FreeEntityIds.pop_back();
        revision = m_EntityRecords[id].revision + 1; // 슬롯 재사용 시 세대(revision) 1 증가
    } else {
        id = static_cast<uint32_t>(m_EntityRecords.size());
        m_EntityRecords.emplace_back();
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
        VulkanBackend::LogToUnity("[ECS] Allocated new 64B-aligned Chunk for Archetype.");
    }

    uint32_t index = targetChunk->AddEntity(ent);
    m_EntityRecords[id] = { targetChunk, index, revision };

    return ent;
}

void ECSManager::DestroyEntity(Entity entity) {
    // 워커 스레드의 청크 순회와 겹치지 않도록 잡 펜스 경계에서 실행
    WaitJobFence();

    if (!IsValidEntity(entity)) return;
    EntityRecord& record = m_EntityRecords[entity.id];

    Chunk* chunk = record.chunk;
    uint32_t deleteIndex = record.index;
    uint32_t lastIndex = chunk->entityCount - 1;

    // Swap-Erase 상수 시간(O(1)) 삭제
    if (deleteIndex != lastIndex) {
        Entity lastEntity = chunk->entityArray[lastIndex];
        chunk->entityArray[deleteIndex] = lastEntity;

        for (size_t i = 0; i < chunk->columns.size(); ++i) {
            size_t compSize = ComponentRegistry::GetComponentSize(chunk->componentIndices[i]);
            if (compSize > 0 && chunk->columns[i] != nullptr) {
                uint8_t* compArray = static_cast<uint8_t*>(chunk->columns[i]);
                std::memcpy(
                    compArray + (static_cast<size_t>(deleteIndex) * compSize),
                    compArray + (static_cast<size_t>(lastIndex) * compSize),
                    compSize
                );
            }
        }

        m_EntityRecords[lastEntity.id].index = deleteIndex;
    }

    chunk->entityCount--;
    record.chunk = nullptr;

    m_FreeEntityIds.push_back(entity.id);
}

void ECSManager::Clear() {
    WaitJobFence();
    m_Archetypes.clear();
    m_EntityRecords.clear();
    m_FreeEntityIds.clear();
}

std::vector<Chunk*> ECSManager::QueryChunks(const ComponentMask& queryMask) {
    std::vector<Chunk*> result;
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
