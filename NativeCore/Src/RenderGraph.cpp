#include "RenderGraph.h"
#include <iostream>
#include <algorithm>

namespace Endfield {

void RenderGraph::AddResource(
    const std::string& name,
    bool isPersistent,
    AccessTag initialAccess,
    VkImage image,
    VkImageView imageView,
    VkSampler sampler,
    VkDeviceSize memorySize)
{
    RenderResource res{};
    res.name = name;
    res.isPersistent = isPersistent;
    res.currentAccess = initialAccess;
    res.image = image;
    res.imageView = imageView;
    res.sampler = sampler;
    res.memorySize = memorySize;

    GetVulkanAccessParams(
        initialAccess,
        res.currentAccessMask,
        res.currentLayout,
        res.currentStage,
        res.lastAccessWasWrite
    );

    m_Resources[name] = res;
}

void RenderGraph::AddPass(const std::string& passName, std::function<void(VkCommandBuffer)> callback)
{
    RenderPassNode node;
    node.name = passName;
    node.executeCallback = callback;
    m_Passes.push_back(node);
}

void RenderGraph::DeclarePassAccess(const std::string& passName, const std::string& resourceName, AccessTag access, uint32_t bindingIndex)
{
    for (auto& pass : m_Passes) {
        if (pass.name == passName) {
            PassAccess pa{};
            pa.resourceName = resourceName;
            pa.accessType = access;
            pa.bindingIndex = bindingIndex;
            GetVulkanAccessParams(access, pa.resolvedAccess, pa.resolvedLayout, pa.resolvedStage, pa.isWrite);
            pass.declaredAccesses.push_back(pa);
            return;
        }
    }
}

void RenderGraph::GetVulkanAccessParams(
    AccessTag tag,
    VkAccessFlags& outAccess,
    VkImageLayout& outLayout,
    VkPipelineStageFlags& outStage,
    bool& outIsWrite) const
{
    switch (tag) {
        case AccessTag::None:
            outAccess  = 0;
            outLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
            outStage   = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            outIsWrite = false;
            break;
        case AccessTag::ColorAttachmentWrite:
            outAccess  = ColorAttachmentWriteTag::kAccess;
            outLayout  = ColorAttachmentWriteTag::kLayout;
            outStage   = ColorAttachmentWriteTag::kStage;
            outIsWrite = ColorAttachmentWriteTag::kIsWrite;
            break;
        case AccessTag::DepthStencilWrite:
            outAccess  = DepthStencilWriteTag::kAccess;
            outLayout  = DepthStencilWriteTag::kLayout;
            outStage   = DepthStencilWriteTag::kStage;
            outIsWrite = DepthStencilWriteTag::kIsWrite;
            break;
        case AccessTag::ShaderRead:
            outAccess  = ReadOnlyTag::kAccess;
            outLayout  = ReadOnlyTag::kLayout;
            outStage   = ReadOnlyTag::kStage;
            outIsWrite = ReadOnlyTag::kIsWrite;
            break;
        case AccessTag::TransferRead:
            outAccess  = TransferReadTag::kAccess;
            outLayout  = TransferReadTag::kLayout;
            outStage   = TransferReadTag::kStage;
            outIsWrite = TransferReadTag::kIsWrite;
            break;
        case AccessTag::TransferWrite:
            outAccess  = TransferWriteTag::kAccess;
            outLayout  = TransferWriteTag::kLayout;
            outStage   = TransferWriteTag::kStage;
            outIsWrite = TransferWriteTag::kIsWrite;
            break;
        case AccessTag::Present:
            outAccess  = PresentTag::kAccess;
            outLayout  = PresentTag::kLayout;
            outStage   = PresentTag::kStage;
            outIsWrite = PresentTag::kIsWrite;
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

// 수명 기반 임시 자원(Transient) 분리 및 메모리 앨리어싱
void RenderGraph::ComputeTransientMemoryAliasing()
{
    // 1. 모든 자원의 패스 수명 구간 [firstPassIndex, lastPassIndex] 초기화 및 산출
    for (auto& kv : m_Resources) {
        kv.second.firstPassIndex = -1;
        kv.second.lastPassIndex  = -1;
        kv.second.aliasedMemoryOffset = 0;
    }

    for (int32_t passIdx = 0; passIdx < static_cast<int32_t>(m_Passes.size()); ++passIdx) {
        for (const auto& access : m_Passes[passIdx].declaredAccesses) {
            auto it = m_Resources.find(access.resourceName);
            if (it == m_Resources.end()) continue;

            RenderResource& res = it->second;
            if (res.firstPassIndex == -1) {
                res.firstPassIndex = passIdx;
            }
            res.lastPassIndex = passIdx;
        }
    }

    // 2. 임시 자원(!isPersistent)만 모아 수명이 겹치지 않는 메모리 블록(Slot)을 재활용(Aliasing)
    struct MemorySlot {
        VkDeviceSize offset;
        VkDeviceSize size;
        int32_t occupiedUntilPass;
    };
    std::vector<MemorySlot> activeSlots;
    VkDeviceSize currentHeapEnd = 0;

    for (auto& kv : m_Resources) {
        RenderResource& res = kv.second;
        // 영속 자원(Persistent)이거나 프레임 내 미사용 자원은 임시 풀 앨리어싱에서 제외
        if (res.isPersistent || res.firstPassIndex == -1) continue;

        bool aliased = false;
        for (auto& slot : activeSlots) {
            // 이전 임시 자원의 마지막 사용 패스(occupiedUntilPass)가 끝난 뒤에 시작하고 크기가 충분하면 오프셋 재활용
            if (slot.occupiedUntilPass < res.firstPassIndex && slot.size >= res.memorySize) {
                res.aliasedMemoryOffset = slot.offset;
                slot.occupiedUntilPass  = res.lastPassIndex;
                aliased = true;
                break;
            }
        }

        if (!aliased) {
            res.aliasedMemoryOffset = currentHeapEnd;
            activeSlots.push_back({ currentHeapEnd, res.memorySize, res.lastPassIndex });
            currentHeapEnd += res.memorySize;
        }
    }

    m_TotalTransientMemoryUsed = currentHeapEnd;
}

void RenderGraph::CompileGraph()
{
    m_PassBarriers.clear();

    // [Step 1] 임시 자원 수명 계산 및 메모리 오프셋 앨리어싱
    ComputeTransientMemoryAliasing();

    // [Step 2] 패스별 해저드 추적 및 플러시 단위 배리어 병합
    for (auto& pass : m_Passes) {
        MergedBarrierGroup barrierGroup;
        barrierGroup.passName = pass.name;
        barrierGroup.srcStageMask = 0;
        barrierGroup.dstStageMask = 0;

        for (const auto& access : pass.declaredAccesses) {
            auto it = m_Resources.find(access.resourceName);
            if (it == m_Resources.end()) continue;

            RenderResource& res = it->second;

            // 영속적 읽기 전용 자원(Persistent Read-Only)이 이미 확정된 레이아웃에 있다면 추적 생략
            if (res.isPersistent &&
                !res.lastAccessWasWrite &&
                !access.isWrite &&
                res.currentLayout == access.resolvedLayout)
            {
                continue;
            }

            // 연속된 읽기 패스(Read -> Read)이고 레이아웃이 같다면 불필요한 배리어 자동 생략
            bool isReadToReadSameLayout =
                (!res.lastAccessWasWrite && !access.isWrite) &&
                (res.currentLayout == access.resolvedLayout) &&
                (res.currentLayout != VK_IMAGE_LAYOUT_UNDEFINED);

            if (isReadToReadSameLayout) {
                continue;
            }

            // 그 외 RAW(Write->Read), WAR(Read->Write), WAW(Write->Write) 및 레이아웃 전이는 모두 배리어 생성
            if (res.image != VK_NULL_HANDLE) {
                VkImageMemoryBarrier barrier{};
                barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                barrier.oldLayout = res.currentLayout;
                barrier.newLayout = access.resolvedLayout;
                barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.image = res.image;

                if (access.resolvedLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL ||
                    res.currentLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
                    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
                } else {
                    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                }

                barrier.subresourceRange.baseMipLevel = 0;
                barrier.subresourceRange.levelCount = 1;
                barrier.subresourceRange.baseArrayLayer = 0;
                barrier.subresourceRange.layerCount = 1;
                barrier.srcAccessMask = res.currentAccessMask;
                barrier.dstAccessMask = access.resolvedAccess;

                barrierGroup.imageBarriers.push_back(barrier);

                // 플러시(패스) 구간 내 모든 의존성 스테이지를 비트 OR(|)로 병합
                barrierGroup.srcStageMask |= res.currentStage;
                barrierGroup.dstStageMask |= access.resolvedStage;
            }

            // 자원 상태 갱신
            res.currentAccess      = access.accessType;
            res.currentAccessMask  = access.resolvedAccess;
            res.currentLayout      = access.resolvedLayout;
            res.currentStage       = access.resolvedStage;
            res.lastAccessWasWrite = access.isWrite;
        }

        if (!barrierGroup.imageBarriers.empty()) {
            if (barrierGroup.srcStageMask == 0) barrierGroup.srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            if (barrierGroup.dstStageMask == 0) barrierGroup.dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
            m_PassBarriers[pass.name] = barrierGroup;
        }

        // [Step 3] 패스별 ShaderRead 리소스 자동 디스크립터 바인딩
        if (m_Device != VK_NULL_HANDLE && m_DescriptorPool != VK_NULL_HANDLE && m_SetLayout != VK_NULL_HANDLE) {
            std::vector<VkDescriptorImageInfo> imageInfos;
            std::vector<VkWriteDescriptorSet> descriptorWrites;

            for (const auto& access : pass.declaredAccesses) {
                if (!access.isWrite && access.resolvedLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
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

                vkAllocateDescriptorSets(m_Device, &allocInfo, &pass.boundDescriptorSet);

                for (size_t i = 0; i < descriptorWrites.size(); ++i) {
                    descriptorWrites[i].dstSet = pass.boundDescriptorSet;
                    descriptorWrites[i].pImageInfo = &imageInfos[i];
                }

                vkUpdateDescriptorSets(m_Device, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
            }
        }
    }
}

void RenderGraph::Clear()
{
    m_Resources.clear();
    m_Passes.clear();
    m_PassBarriers.clear();
    m_TotalTransientMemoryUsed = 0;
}

void RenderGraph::Execute(VkCommandBuffer cmdBuffer)
{
    for (const auto& pass : m_Passes) {
        // [Chapter 3 - Slide 56, 57] 플러시당 단 1회의 vkCmdPipelineBarrier 호출로 모든 이미지 배리어 일괄 적용
        auto it = m_PassBarriers.find(pass.name);
        if (it != m_PassBarriers.end() && cmdBuffer != VK_NULL_HANDLE) {
            const MergedBarrierGroup& group = it->second;
            vkCmdPipelineBarrier(
                cmdBuffer,
                group.srcStageMask,
                group.dstStageMask,
                0,
                0, nullptr,
                0, nullptr,
                static_cast<uint32_t>(group.imageBarriers.size()),
                group.imageBarriers.data()
            );
        }

        if (pass.boundDescriptorSet != VK_NULL_HANDLE && m_PipelineLayout != VK_NULL_HANDLE && cmdBuffer != VK_NULL_HANDLE) {
            vkCmdBindDescriptorSets(
                cmdBuffer,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                m_PipelineLayout,
                0,
                1,
                &pass.boundDescriptorSet,
                0,
                nullptr
            );
        }

        if (pass.executeCallback) {
            pass.executeCallback(cmdBuffer);
        }
    }
}

const RenderResource* RenderGraph::GetResourceInfo(const std::string& name) const
{
    auto it = m_Resources.find(name);
    return (it != m_Resources.end()) ? &it->second : nullptr;
}

size_t RenderGraph::GetMergedBarrierCountForPass(const std::string& passName) const
{
    auto it = m_PassBarriers.find(passName);
    return (it != m_PassBarriers.end()) ? it->second.imageBarriers.size() : 0;
}

} // namespace Endfield
