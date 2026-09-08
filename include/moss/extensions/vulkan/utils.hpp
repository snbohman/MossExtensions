#pragma once

#include <moss/moss.hpp>
#include <vulkan/vulkan.h>

namespace moss::extensions::vulkan::utils {

/*
All abstracted and reused vkstructs biolerplate will be found here.
*/
namespace init {

inline VkCommandPoolCreateInfo cmdPoolCreateInfo(
    uint32_t queueFamilyIndex,
    VkCommandPoolCreateFlags flags
) {
    VkCommandPoolCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    info.pNext = nullptr;
    info.queueFamilyIndex = queueFamilyIndex;
    info.flags = flags;

    return info;
}

inline VkCommandBufferAllocateInfo cmdBufAllocateInfo(
    VkCommandPool pool,
    uint32_t count
) {
    VkCommandBufferAllocateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    info.pNext = nullptr;
    info.commandPool = pool;
    info.commandBufferCount = count;
    info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

    return info;
}

inline VkFenceCreateInfo fenceCreateInfo(VkFenceCreateFlags flags) {
    VkFenceCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    info.pNext = nullptr;
    info.flags = flags;

    return info;
}

inline VkSemaphoreCreateInfo semCreateInfo(VkSemaphoreCreateFlags flags) {
    VkSemaphoreCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    info.pNext = nullptr;
    info.flags = flags;

    return info;
}

inline VkCommandBufferBeginInfo cmdBufBeginInfo(VkCommandBufferUsageFlags flags) {
    VkCommandBufferBeginInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    info.pNext = nullptr;
    info.pInheritanceInfo = nullptr;
    info.flags = flags;

    return info;
}

inline VkSemaphoreSubmitInfo semSubmitInfo(
    VkPipelineStageFlags2 stageMask,
    VkSemaphore semaphore
) {
	VkSemaphoreSubmitInfo info{};
	info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
	info.pNext = nullptr;
	info.semaphore = semaphore;
	info.stageMask = stageMask;
	info.deviceIndex = 0;
	info.value = 1;

	return info;
}

inline VkCommandBufferSubmitInfo cmdBufSubmitInfo(VkCommandBuffer cmd) {
	VkCommandBufferSubmitInfo info{};
	info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
	info.pNext = nullptr;
	info.commandBuffer = cmd;
	info.deviceMask = 0;

	return info;
}

/*
 * We will be using vkQueueSubmit2 for submitting our commands. This is part of
 * syncronization-2 and is an updated version of the older VkQueueSubmit from
 * vulkan 1.0. The function call requires a VkSubmitInfo2 which contains the
 * information on the semaphores used as part of the submit, and we can give it
 * a Fence so that we can check for that submit to be finished executing.
 * VkSubmitInfo2 requires VkSemaphoreSubmitInfo for each of the semaphores it
 * uses, and a VkCommandBufferSubmitInfo for the command buffers that will be
 * enqueued as part of the submit. Lets check the vkinit functions for those.
 */
inline VkSubmitInfo2 submitInfo(
    VkCommandBufferSubmitInfo* cmd,
    VkSemaphoreSubmitInfo* signalSemaphoreInfo,
    VkSemaphoreSubmitInfo* waitSemaphoreInfo
) {
    VkSubmitInfo2 info = {};
    info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    info.pNext = nullptr;

    info.waitSemaphoreInfoCount = waitSemaphoreInfo == nullptr ? 0 : 1;
    info.pWaitSemaphoreInfos = waitSemaphoreInfo;

    info.signalSemaphoreInfoCount = signalSemaphoreInfo == nullptr ? 0 : 1;
    info.pSignalSemaphoreInfos = signalSemaphoreInfo;

    info.commandBufferInfoCount = 1;
    info.pCommandBufferInfos = cmd;

    return info;
}

inline VkImageSubresourceRange subresourceRange(VkImageAspectFlags aspectMask) {
    VkImageSubresourceRange subImage {};
    subImage.aspectMask = aspectMask;
    subImage.baseMipLevel = 0;
    subImage.levelCount = VK_REMAINING_MIP_LEVELS;
    subImage.baseArrayLayer = 0;
    subImage.layerCount = VK_REMAINING_ARRAY_LAYERS;

    return subImage;
}


} // namespace create

/*
 * A thing we care in that structure is the AspectMask. This is going to be
 * either VK_IMAGE_ASPECT_COLOR_BIT or VK_IMAGE_ASPECT_DEPTH_BIT. For color and
 * depth images respectively. We wont need any of the other options. We will
 * keep it as Color aspect under all cases except when the target layout is
 * VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, which we will use later when we add
 * depth buffer.
 */
inline void transitionImage(
    VkCommandBuffer cmd,
    VkImage image,
    VkImageLayout currentLayout,
    VkImageLayout newLayout
) {
    VkImageMemoryBarrier2 imageBarrier{.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    imageBarrier.pNext = nullptr;
    imageBarrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    imageBarrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
    imageBarrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    imageBarrier.dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT;
    imageBarrier.oldLayout = currentLayout;
    imageBarrier.newLayout = newLayout;

    VkImageAspectFlags aspectMask = (newLayout == VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    imageBarrier.subresourceRange = init::subresourceRange(aspectMask);
    imageBarrier.image = image;

    VkDependencyInfo depInfo {};
    depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    depInfo.pNext = nullptr;
    depInfo.imageMemoryBarrierCount = 1;
    depInfo.pImageMemoryBarriers = &imageBarrier;

    vkCmdPipelineBarrier2(cmd, &depInfo);
}

} // namespace moss::ext::vulkan::utils
