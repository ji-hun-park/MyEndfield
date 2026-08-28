#include "RenderGraph.h"
#include <iostream>

namespace Endfield {

void RenderGraph::AddResource(const std::string& name, bool isPersistent, AccessTag initialAccess, VkImage image, VkImageView imageView, VkSampler sampler)
{
    RenderResource res;
    res.name = name;
    res.isPersistent = isPersistent;
    res.currentAccess = initialAccess;
    res.image = image;
    res.imageView = imageView;
    res.sampler = sampler;
    m_Resources[name] = res;
}

void RenderGraph::AddPass(const std::string& passName)
{
    RenderPassNode node;
    node.name = passName;
    m_Passes.push_back(node);
}

void RenderGraph::DeclarePassAccess(const std::string& passName, const std::string& resourceName, AccessTag access, uint32_t bindingIndex)
{
    for (auto& pass : m_Passes) {
        if (pass.name == passName) {
            PassAccess pa;
            pa.resourceName = resourceName;
            pa.accessType = access;
            pa.bindingIndex = bindingIndex;
            pass.declaredAccesses.push_back(pa);
            return;
        }
    }
}

void RenderGraph::GetVulkanAccessParams(AccessTag tag, VkAccessFlags& outAccess, VkImageLayout& outLayout, VkPipelineStageFlags& outStage)
{
    switch (tag) {
        case AccessTag::None:
            outAccess = 0;
            outLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            outStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            break;
        case AccessTag::ColorAttachmentWrite:
            outAccess = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            outLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            outStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            break;
        case AccessTag::DepthStencilWrite:
            outAccess = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            outLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            outStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
            break;
        case AccessTag::ShaderRead:
            outAccess = VK_ACCESS_SHADER_READ_BIT;
            outLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            outStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            break;
        case AccessTag::TransferRead:
            outAccess = VK_ACCESS_TRANSFER_READ_BIT;
            outLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            outStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            break;
        case AccessTag::TransferWrite:
            outAccess = VK_ACCESS_TRANSFER_WRITE_BIT;
            outLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            outStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            break;
        case AccessTag::Present:
            outAccess = 0;
            outLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            outStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
            break;
    }
}

void RenderGraph::SetDescriptorAllocator(VkDevice device, VkDescriptorPool pool, VkDescriptorSetLayout setLayout, VkPipelineLayout pipelineLayout)
{
    m_Device = device;
    m_DescriptorPool = pool;
    m_SetLayout = setLayout;
    m_PipelineLayout = pipelineLayout;
}

void RenderGraph::CompileGraph()
{
    m_PassBarriers.clear();

    // 모든 패스를 순회하면서 상태 변화를 추적합니다.
    for (auto& pass : m_Passes) {
        MergedBarrierGroup barrierGroup;
        barrierGroup.passName = pass.name;
        barrierGroup.srcStageMask = 0;
        barrierGroup.dstStageMask = 0;

        for (const auto& access : pass.declaredAccesses) {
            auto it = m_Resources.find(access.resourceName);
            if (it == m_Resources.end()) continue;

            RenderResource& res = it->second;

            // 접근 태그가 이전과 다르면(상태 전이가 필요하면) 배리어를 생성합니다.
            if (res.currentAccess != access.accessType) {
                VkAccessFlags srcAccess, dstAccess;
                VkImageLayout srcLayout, dstLayout;
                VkPipelineStageFlags srcStage, dstStage;

                GetVulkanAccessParams(res.currentAccess, srcAccess, srcLayout, srcStage);
                GetVulkanAccessParams(access.accessType, dstAccess, dstLayout, dstStage);

                // 두 상태 모두 Read-Only라면 해저드가 없으므로 스킵 (규칙 4 적용)
                if (srcLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL && dstLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
                    continue; // Skip barrier
                }

                VkImageMemoryBarrier barrier{};
                barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                barrier.oldLayout = srcLayout;
                barrier.newLayout = dstLayout;
                barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.image = res.image; // 실제 할당된 이미지가 있다면 세팅됨
                
                // Color인지 Depth인지에 따라 AspectMask 결정
                if (dstLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
                    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
                } else {
                    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                }
                
                barrier.subresourceRange.baseMipLevel = 0;
                barrier.subresourceRange.levelCount = 1;
                barrier.subresourceRange.baseArrayLayer = 0;
                barrier.subresourceRange.layerCount = 1;
                barrier.srcAccessMask = srcAccess;
                barrier.dstAccessMask = dstAccess;

                barrierGroup.imageBarriers.push_back(barrier);
                
                // 해당 패스(플러시 구간) 내의 모든 의존성 스테이지를 병합(OR 연산)
                if (barrierGroup.srcStageMask == 0) barrierGroup.srcStageMask = srcStage;
                else barrierGroup.srcStageMask |= srcStage;
                
                if (barrierGroup.dstStageMask == 0) barrierGroup.dstStageMask = dstStage;
                else barrierGroup.dstStageMask |= dstStage;

                // 자원의 현재 상태를 업데이트
                res.currentAccess = access.accessType;
            }
        }

        // 전환해야 할 자원이 1개 이상 있으면 그룹에 저장
        if (!barrierGroup.imageBarriers.empty()) {
            m_PassBarriers[pass.name] = barrierGroup;
        }

        // ==========================================
        // 자동화된 리소스 바인딩 (Descriptor Set 할당)
        // ==========================================
        if (m_Device != VK_NULL_HANDLE && m_DescriptorPool != VK_NULL_HANDLE && m_SetLayout != VK_NULL_HANDLE) {
            std::vector<VkDescriptorImageInfo> imageInfos;
            std::vector<VkWriteDescriptorSet> descriptorWrites;

            for (const auto& access : pass.declaredAccesses) {
                if (access.accessType == AccessTag::ShaderRead) {
                    auto it = m_Resources.find(access.resourceName);
                    if (it != m_Resources.end() && it->second.imageView != VK_NULL_HANDLE) {
                        VkDescriptorImageInfo info{};
                        info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                        info.imageView = it->second.imageView;
                        info.sampler = it->second.sampler;
                        imageInfos.push_back(info);

                        VkWriteDescriptorSet write{};
                        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                        write.dstBinding = access.bindingIndex;
                        write.dstArrayElement = 0;
                        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                        write.descriptorCount = 1;
                        // 나중에 data() 포인터를 연결하기 위해 인덱스를 기록해둡니다. (임시)
                        // vector가 리사이즈되면 포인터가 깨지므로 아래에서 최종 연결합니다.
                        descriptorWrites.push_back(write);
                    }
                }
            }

            if (!descriptorWrites.empty()) {
                VkDescriptorSetAllocateInfo allocInfo{};
                allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
                allocInfo.descriptorPool = m_DescriptorPool;
                allocInfo.descriptorSetCount = 1;
                allocInfo.pSetLayouts = &m_SetLayout;

                // TODO: 렌더 패스가 삭제/재구성될 때의 해제 로직 필요
                vkAllocateDescriptorSets(m_Device, &allocInfo, &pass.boundDescriptorSet);

                // 안전하게 포인터 매핑
                for (size_t i = 0; i < descriptorWrites.size(); ++i) {
                    descriptorWrites[i].dstSet = pass.boundDescriptorSet;
                    descriptorWrites[i].pImageInfo = &imageInfos[i];
                }

                vkUpdateDescriptorSets(m_Device, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
                std::cout << "[RenderGraph] Automatically bound " << descriptorWrites.size() << " resources for Pass: " << pass.name << "\n";
            }
        }
    }
}

void RenderGraph::Execute(VkCommandBuffer cmdBuffer)
{
    // 선언된 패스를 순서대로 실행
    for (const auto& pass : m_Passes) {
        // 1. 병합된 배리어(Merged Barrier)가 있는지 확인하고 단 한 번의 호출로 적용
        auto it = m_PassBarriers.find(pass.name);
        if (it != m_PassBarriers.end()) {
            const MergedBarrierGroup& group = it->second;
            vkCmdPipelineBarrier(
                cmdBuffer,
                group.srcStageMask,
                group.dstStageMask,
                0, // Dependency flags
                0, nullptr, // Memory barriers
                0, nullptr, // Buffer barriers
                static_cast<uint32_t>(group.imageBarriers.size()),
                group.imageBarriers.data()
            );
        }

        // 2. 패스의 실제 렌더링 호출을 준비하며 자동 할당된 디스크립터 셋이 있다면 바인딩합니다.
        if (pass.boundDescriptorSet != VK_NULL_HANDLE && m_PipelineLayout != VK_NULL_HANDLE) {
            vkCmdBindDescriptorSets(
                cmdBuffer,
                VK_PIPELINE_BIND_POINT_GRAPHICS, // 차후 Compute Pass 지원 시 분기 필요
                m_PipelineLayout,
                0, // 예시로 Set 0번에 패스 글로벌 리소스를 바인딩
                1,
                &pass.boundDescriptorSet,
                0,
                nullptr
            );
        }

        // vkCmdBeginRenderPass( ... )
        // ... 패스 내 커스텀 커맨드 (드로우 콜 등) ...
        // vkCmdEndRenderPass(cmdBuffer);
        
        // (실제 프로젝트에서는 여기에 각 패스별 람다(Lambda)나 가상 함수 콜을 연결합니다.)
    }
}

} // namespace Endfield

