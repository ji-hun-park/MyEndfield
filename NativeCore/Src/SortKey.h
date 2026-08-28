#pragma once
#include <cstdint>

namespace Endfield {

// Rule 4: 64-bit Sort Key for unified sorting and batching
struct SortKey {
    union {
        uint64_t key;
        struct {
            // LSB to MSB (depending on endianness, structured for fast radix sort)
            uint64_t distance     : 16; // Depth / Distance
            uint64_t materialID   : 16; // Material or Mesh ID
            uint64_t pipelineID   : 16; // Shader keywords / Pipeline
            uint64_t passID       : 16; // Screen / View Pass Order (Highest bit)
        };
    };

    bool operator<(const SortKey& other) const {
        return key < other.key; // std::sort will branch on this single comparison
    }
};

} // namespace Endfield

