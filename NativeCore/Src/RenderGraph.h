#pragma once
#include <vector>
#include <string>
#include <unordered_map>
#include <vulkan/vulkan.h>

namespace Endfield {

// 렌더 패스에서 자원을 어떻게 사용할지 명시하는 태그
enum class AccessTag {
    None,
    ColorAttachmentWrite,
    DepthStencilWrite,
    ShaderRead,
    TransferRead,
    TransferWrite,
    Present
};

// 렌더 그래프에서 관리하는 자원(Resource) 정보
struct RenderResource {
    std::string name;
    bool isPersistent;
    VkImage image;
    AccessTag currentAccess;
};

// 특정 패스에서 특정 자원에 접근하는 정보
struct PassAccess {
    std::string resourceName;
    AccessTag accessType;
};

// 하나의 렌더 패스(노드)
class RenderPassNode {
public:
    std::string name;
    std::vector<PassAccess> declaredAccesses;
};

// 그래프 선언 시점에 계산된, 특정 패스 진입 직전에 실행할 배리어 묶음
struct MergedBarrierGroup {
    std::string passName;
    std::vector<VkImageMemoryBarrier> imageBarriers;
    VkPipelineStageFlags srcStageMask;
    VkPipelineStageFlags dstStageMask;
};

class RenderGraph {
public:
    void AddResource(const std::string& name, bool isPersistent, AccessTag initialAccess, VkImage image = VK_NULL_HANDLE);
    void AddPass(const std::string& passName);
    void DeclarePassAccess(const std::string& passName, const std::string& resourceName, AccessTag access);

    // 선언된 자원 접근 정보를 분석하여 런타임 배리어를 미리 계산해 병합(Merge)합니다.
    void CompileGraph();
    
    // 미리 컴파일된 배리어를 실행하고 패스를 순차적으로 구동합니다.
    void Execute(VkCommandBuffer cmdBuffer); 

private:
    std::unordered_map<std::string, RenderResource> m_Resources;
    std::vector<RenderPassNode> m_Passes;
    
    // 각 패스 진입 시점에 적용할 배리어 그룹
    std::unordered_map<std::string, MergedBarrierGroup> m_PassBarriers;

    // AccessTag를 실제 Vulkan 파라미터로 변환하는 헬퍼
    void GetVulkanAccessParams(AccessTag tag, VkAccessFlags& outAccess, VkImageLayout& outLayout, VkPipelineStageFlags& outStage);
};

} // namespace Endfield
