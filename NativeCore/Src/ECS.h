#pragma once
#include <cstdint>
#include <vector>

namespace Endfield {

// Rule 2: 128-bit fixed mask for component types
struct ComponentMask {
    uint64_t low;
    uint64_t high;

    bool operator==(const ComponentMask& other) const {
        return low == other.low && high == other.high;
    }
};

// 8-byte Entity Handle
struct Entity {
    uint32_t index;
    uint32_t revision;
};

// Base chunk for SoA memory layout
struct Chunk {
    ComponentMask mask;
    uint32_t entityCount;
    // Pointers to contiguous arrays of components (SoA)
    // C++ skeleton - void* used for type-erasure in generic chunk
    std::vector<void*> columns; 
    
    // Allocate memory for components
    void Allocate(uint32_t capacity);
};

// Archetype groups chunks with the same ComponentMask
struct Archetype {
    ComponentMask mask;
    std::vector<Chunk*> chunks;
};

// Custom ECS Manager to bypass Unity DOTS
class ECSManager {
public:
    void Initialize();
    void Shutdown();

    Entity CreateEntity(const ComponentMask& mask);
    void DestroyEntity(Entity entity);

    // Rule 2: No reflection, no hashmap. Direct pointer arithmetic access.
    template<typename T>
    T* GetComponentArray(Chunk* chunk, uint32_t componentIndex) {
        return static_cast<T*>(chunk->columns[componentIndex]);
    }

    void Update(); // Called every frame
};

} // namespace Endfield

