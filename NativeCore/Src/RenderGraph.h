#pragma once
#include <vector>
#include <cstdint>

namespace Endfield {

enum class ResourceAccessType {
    ReadOptimal,
    WriteOptimal,
    // ... other explicit states to replace driver guesswork
};

struct ResourceTag {
    uint32_t resourceID;
    ResourceAccessType accessType;
};

// Rule 4: Render Graph for barrier merging and resource optimization
class RenderPass {
public:
    virtual ~RenderPass() = default;
    
    // Explicitly declare read/write resources at compile/setup time
    virtual std::vector<ResourceTag> GetInputs() const = 0;
    virtual std::vector<ResourceTag> GetOutputs() const = 0;
    
    virtual void Execute() = 0; // C++ Native execution
};

class RenderGraph {
public:
    void AddPass(RenderPass* pass);
    
    // Analyzes tags and merges barriers for a single flush
    void Compile(); 
    
    // Execute all passes, handling Dynamic Offsets (Rule 4)
    void Execute(); 
    
private:
    std::vector<RenderPass*> m_Passes;
};

} // namespace Endfield

