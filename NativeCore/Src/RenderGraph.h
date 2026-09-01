#pragma once
#include <vector>
#include <string>
#include <unordered_map>
#include <vulkan/vulkan.h>
#include <functional>

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
    VkImageView imageView; // 바인딩을 위한 뷰
    VkSampler sampler;     // 바인딩을 위한 샘플러
    AccessTag currentAccess;
};

// 특정 패스에서 특정 자원에 접근하는 정보
struct PassAccess {
    std::string resourceName;
    AccessTag accessType;
    uint32_t bindingIndex; // 쉐이더의 어느 바인딩 슬롯에 꽂을 것인가?
};

// 하나의 렌더 패스(노드)
class RenderPassNode {
public:
    std::string name;
    std::vector<PassAccess> declaredAccesses;
    
    // 컴파일 시점에 자동 생성된 디스크립터 셋 (패스 전용 Set 0번 등)
    VkDescriptorSet boundDescriptorSet = VK_NULL_HANDLE;

    // 런타임에 실행할 패스 실제 로직
    std::function<void(VkCommandBuffer)> executeCallback = nullptr;
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
    void AddResource(const std::string& name, bool isPersistent, AccessTag initialAccess, VkImage image = VK_NULL_HANDLE, VkImageView imageView = VK_NULL_HANDLE, VkSampler sampler = VK_NULL_HANDLE);
    void AddPass(const std::string& passName, std::function<void(VkCommandBuffer)> callback = nullptr);
    void DeclarePassAccess(const std::string& passName, const std::string& resourceName, AccessTag access, uint32_t bindingIndex = 0);

    // 디스크립터 할당을 위한 풀 및 레이아웃 주입 (간략화)
    void SetDescriptorAllocator(VkDevice device, VkDescriptorPool pool, VkDescriptorSetLayout setLayout, VkPipelineLayout pipelineLayout);

    // 선언된 자원 접근 정보를 분석하여 런타임 배리어를 미리 계산해 병합(Merge)하고 디스크립터를 바인딩합니다.
    void CompileGraph();
    
    // 미리 컴파일된 배리어를 실행하고 디스크립터를 바인딩하며 패스를 구동합니다.
    void Execute(VkCommandBuffer cmdBuffer); 

    // 렌더 패스가 재구성되거나 삭제될 때 메모리 및 컨테이너를 비우는 해제 로직
    void Clear();

private:
    std::unordered_map<std::string, RenderResource> m_Resources;
    std::vector<RenderPassNode> m_Passes;
    
    // 각 패스 진입 시점에 적용할 배리어 그룹
    std::unordered_map<std::string, MergedBarrierGroup> m_PassBarriers;

    // AccessTag를 실제 Vulkan 파라미터로 변환하는 헬퍼
    void GetVulkanAccessParams(AccessTag tag, VkAccessFlags& outAccess, VkImageLayout& outLayout, VkPipelineStageFlags& outStage);

    VkDevice m_Device = VK_NULL_HANDLE;
    VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_SetLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_PipelineLayout = VK_NULL_HANDLE;
};

} // namespace Endfield
