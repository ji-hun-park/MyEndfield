#pragma once
#include <vector>
#include <string>
#include <unordered_map>
#include <vulkan/vulkan.h>
#include <functional>
#include <cstdint>

namespace Endfield {

// 기존 코드 하위 호환용 런타임 태그
enum class AccessTag {
    None,
    ColorAttachmentWrite,
    DepthStencilWrite,
    ShaderRead,
    TransferRead,
    TransferWrite,
    Present
};

// =========================================================================
// 컴파일 타임 타입 태그 (Compile-Time Access Tags)
// C++ 템플릿 인스턴스화 시점에 스테이지, 접근 마스크, 레이아웃, 쓰기 여부를 확정
// =========================================================================
struct ReadOnlyTag {
    static constexpr AccessTag            kTagEnum = AccessTag::ShaderRead;
    static constexpr VkAccessFlags        kAccess  = VK_ACCESS_SHADER_READ_BIT;
    static constexpr VkImageLayout        kLayout  = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    static constexpr VkPipelineStageFlags kStage   = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    static constexpr bool                 kIsWrite = false;
};
using ShaderReadOnlyTag = ReadOnlyTag;

struct ColorAttachmentWriteTag {
    static constexpr AccessTag            kTagEnum = AccessTag::ColorAttachmentWrite;
    static constexpr VkAccessFlags        kAccess  = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    static constexpr VkImageLayout        kLayout  = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    static constexpr VkPipelineStageFlags kStage   = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    static constexpr bool                 kIsWrite = true;
};
using WriteTag = ColorAttachmentWriteTag;

struct DepthStencilWriteTag {
    static constexpr AccessTag            kTagEnum = AccessTag::DepthStencilWrite;
    static constexpr VkAccessFlags        kAccess  = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    static constexpr VkImageLayout        kLayout  = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    static constexpr VkPipelineStageFlags kStage   = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    static constexpr bool                 kIsWrite = true;
};

struct TransferReadTag {
    static constexpr AccessTag            kTagEnum = AccessTag::TransferRead;
    static constexpr VkAccessFlags        kAccess  = VK_ACCESS_TRANSFER_READ_BIT;
    static constexpr VkImageLayout        kLayout  = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    static constexpr VkPipelineStageFlags kStage   = VK_PIPELINE_STAGE_TRANSFER_BIT;
    static constexpr bool                 kIsWrite = false;
};

struct TransferWriteTag {
    static constexpr AccessTag            kTagEnum = AccessTag::TransferWrite;
    static constexpr VkAccessFlags        kAccess  = VK_ACCESS_TRANSFER_WRITE_BIT;
    static constexpr VkImageLayout        kLayout  = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    static constexpr VkPipelineStageFlags kStage   = VK_PIPELINE_STAGE_TRANSFER_BIT;
    static constexpr bool                 kIsWrite = true;
};

struct PresentTag {
    static constexpr AccessTag            kTagEnum = AccessTag::Present;
    static constexpr VkAccessFlags        kAccess  = 0;
    static constexpr VkImageLayout        kLayout  = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    static constexpr VkPipelineStageFlags kStage   = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    static constexpr bool                 kIsWrite = false;
};

// 렌더 그래프에서 관리하는 자원(Resource) 정보
struct RenderResource {
    std::string name;
    bool isPersistent = false;
    VkImage image = VK_NULL_HANDLE;
    VkImageView imageView = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;

    // 현재 추적 상태
    AccessTag currentAccess = AccessTag::None;
    VkAccessFlags currentAccessMask = 0;
    VkImageLayout currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkPipelineStageFlags currentStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    bool lastAccessWasWrite = false;

    // 임시 자원(Transient) 수명 추적 및 메모리 앨리어싱 정보
    VkDeviceSize memorySize = 4ULL * 1024ULL * 1024ULL; // 기본 4MB 렌더 타겟 블록 가정
    VkDeviceSize aliasedMemoryOffset = 0;               // 재활용된 물리 메모리 오프셋
    int32_t firstPassIndex = -1;
    int32_t lastPassIndex = -1;
};

// 특정 패스에서 특정 자원에 접근하는 컴파일된 상태 정보
struct PassAccess {
    std::string resourceName;
    AccessTag accessType = AccessTag::None;
    uint32_t bindingIndex = 0;

    VkAccessFlags resolvedAccess = 0;
    VkImageLayout resolvedLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkPipelineStageFlags resolvedStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    bool isWrite = false;
};

// 하나의 렌더 패스(노드)
class RenderPassNode {
public:
    std::string name;
    std::vector<PassAccess> declaredAccesses;

    VkDescriptorSet boundDescriptorSet = VK_NULL_HANDLE;
    std::function<void(VkCommandBuffer)> executeCallback = nullptr;
};

// 플러시(Flush) 단위 해저드 병합 그룹
struct MergedBarrierGroup {
    std::string passName;
    std::vector<VkImageMemoryBarrier> imageBarriers;
    VkPipelineStageFlags srcStageMask = 0;
    VkPipelineStageFlags dstStageMask = 0;
};

class RenderGraph {
public:
    void AddResource(
        const std::string& name,
        bool isPersistent,
        AccessTag initialAccess,
        VkImage image = VK_NULL_HANDLE,
        VkImageView imageView = VK_NULL_HANDLE,
        VkSampler sampler = VK_NULL_HANDLE,
        VkDeviceSize memorySize = 4ULL * 1024ULL * 1024ULL);

    void AddPass(const std::string& passName, std::function<void(VkCommandBuffer)> callback = nullptr);

    // 1. 기존 런타임 AccessTag 기반 선언 (하위 호환 유지)
    void DeclarePassAccess(const std::string& passName, const std::string& resourceName, AccessTag access, uint32_t bindingIndex = 0);

    // 2. 컴파일 타임 타입 태그 기반 리소스 접근 선언
    template<typename AccessTagType>
    void DeclarePassAccess(const std::string& passName, const std::string& resourceName, uint32_t bindingIndex = 0) {
        for (auto& pass : m_Passes) {
            if (pass.name == passName) {
                PassAccess pa{};
                pa.resourceName   = resourceName;
                pa.accessType     = AccessTagType::kTagEnum;
                pa.bindingIndex   = bindingIndex;
                pa.resolvedAccess = AccessTagType::kAccess;
                pa.resolvedLayout = AccessTagType::kLayout;
                pa.resolvedStage  = AccessTagType::kStage;
                pa.isWrite        = AccessTagType::kIsWrite;
                pass.declaredAccesses.push_back(pa);
                return;
            }
        }
    }

    void SetDescriptorAllocator(VkDevice device, VkDescriptorPool pool, VkDescriptorSetLayout setLayout, VkPipelineLayout pipelineLayout);

    // 수명 분석, 임시 자원 메모리 앨리어싱, 해저드 병합을 수행
    void CompileGraph();

    // 병합된 배리어를 패스당 단 1회의 vkCmdPipelineBarrier로 실행하고 패스를 구동
    void Execute(VkCommandBuffer cmdBuffer);

    void Clear();

    // 단위 테스트 및 디버깅용 조회 API
    const RenderResource* GetResourceInfo(const std::string& name) const;
    size_t GetMergedBarrierCountForPass(const std::string& passName) const;
    VkDeviceSize GetTotalAliasedTransientMemorySize() const noexcept { return m_TotalTransientMemoryUsed; }

private:
    std::unordered_map<std::string, RenderResource> m_Resources;
    std::vector<RenderPassNode> m_Passes;
    std::unordered_map<std::string, MergedBarrierGroup> m_PassBarriers;

    VkDeviceSize m_TotalTransientMemoryUsed = 0;

    void GetVulkanAccessParams(AccessTag tag, VkAccessFlags& outAccess, VkImageLayout& outLayout, VkPipelineStageFlags& outStage, bool& outIsWrite) const;
    void ComputeTransientMemoryAliasing();

    VkDevice m_Device = VK_NULL_HANDLE;
    VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_SetLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_PipelineLayout = VK_NULL_HANDLE;
};

} // namespace Endfield
