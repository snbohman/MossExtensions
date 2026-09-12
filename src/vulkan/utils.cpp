#include "moss/extensions/vulkan/utils.hpp"
#include "moss/extensions/vulkan/logs.hpp"
#include <algorithm>
#include <ranges>

namespace moss::extensions::vulkan::utils {

/* ------------------------------------------------------ */
/* ------------------------ Info ------------------------ */
/* ------------------------------------------------------ */
namespace info {

VkCommandPoolCreateInfo cmdPoolCreate(
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

VkCommandBufferAllocateInfo cmdBufAllocate(
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

VkFenceCreateInfo fenceCreate(VkFenceCreateFlags flags) {
    VkFenceCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    info.pNext = nullptr;
    info.flags = flags;

    return info;
}

VkSemaphoreCreateInfo semaphoreCreate(VkSemaphoreCreateFlags flags) {
    VkSemaphoreCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    info.pNext = nullptr;
    info.flags = flags;

    return info;
}

VkSemaphoreSubmitInfo semaphoreSubmit(
    VkPipelineStageFlags2 stageMask,
    VkSemaphore semaphore
) {
	VkSemaphoreSubmitInfo info = {};
	info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
	info.pNext = nullptr;
	info.semaphore = semaphore;
	info.stageMask = stageMask;
	info.deviceIndex = 0;
	info.value = 1;

	return info;
}

VkCommandBufferBeginInfo cmdBufBegin(VkCommandBufferUsageFlags flags) {
    VkCommandBufferBeginInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    info.pNext = nullptr;
    info.pInheritanceInfo = nullptr;
    info.flags = flags;

    return info;
}

VkCommandBufferSubmitInfo cmdBufSubmit(VkCommandBuffer cmd) {
	VkCommandBufferSubmitInfo info = {};
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
VkSubmitInfo2 submit(
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

VkImageCreateInfo imageCreate(
    VkFormat format,
    VkImageUsageFlags usageFlags,
    VkExtent3D extent
) {
    VkImageCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    info.pNext = nullptr;

    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = format;
    info.extent = extent;
    info.samples = VK_SAMPLE_COUNT_1_BIT; // For MSAA
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.tiling = VK_IMAGE_TILING_OPTIMAL; // Optimal tiling, best gpu format
    info.usage = usageFlags;

    return info;
}

/* From vkguide 2.1 (vkguide.dev/docs/new_chapter_2/vulkan_new_rendering):
 * We will hardcode the image tiling to OPTIMAL, which means that we allow the
 * gpu to shuffle the data however it sees fit. If we want to read the image
 * data from cpu, we would need to use tiling LINEAR, which makes the gpu data
 * into a simple 2d array. This tiling highly limits what the gpu can do, so the
 * only real use case for LINEAR is CPU readback.
 */
VkImageViewCreateInfo imageViewCreate(
    VkFormat format,
    VkImage image,
    VkImageAspectFlags aspectFlags
) {
    VkImageViewCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    info.pNext = nullptr;

    info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    info.image = image;
    info.format = format;
    info.subresourceRange.baseMipLevel = 0;
    info.subresourceRange.levelCount = 1;
    info.subresourceRange.baseArrayLayer = 0;
    info.subresourceRange.layerCount = 1;
    info.subresourceRange.aspectMask = aspectFlags;

    return info;
}

VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreate(
    std::vector<VkDescriptorSetLayoutBinding> bindings,
    VkShaderStageFlags shaderStages,
    void* pNext,
    VkDescriptorSetLayoutCreateFlags flags
) {
    for (auto& b : bindings)
        b.stageFlags |= shaderStages;

    VkDescriptorSetLayoutCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    info.pNext = pNext;
    info.pBindings = bindings.data();
    info.bindingCount = (u32)bindings.size();
    info.flags = flags;
    return info;
}

} // namespace info

/* ------------------------------------------------------ */
/* ------------------------ Mask ------------------------ */
/* ------------------------------------------------------ */
namespace mask {

inline VkImageSubresourceRange subresourceRange(VkImageAspectFlags aspectMask) {
    VkImageSubresourceRange subImage = {};
    subImage.aspectMask = aspectMask;
    subImage.baseMipLevel = 0;
    subImage.levelCount = VK_REMAINING_MIP_LEVELS;
    subImage.baseArrayLayer = 0;
    subImage.layerCount = VK_REMAINING_ARRAY_LAYERS;

    return subImage;
}

} // namespace mask

/* ----------------------------------------------------------- */
/* ------------------------ Interface ------------------------ */
/* ----------------------------------------------------------- */
namespace interface {

void DeletionQueue::push(std::function<void()>&& function) {
    deletors.push_back(function);
}

void DeletionQueue::flush() {
    for (auto it = deletors.rbegin(); it != deletors.rend(); it++) (*it)();
    deletors.clear();
}

void DescriptorAllocator::init(VkDevice device, uint32_t maxSets, std::span<PoolSizeRatio> poolRatios) {
    std::vector<VkDescriptorPoolSize> poolSizes;
    for (PoolSizeRatio ratio : poolRatios) {
        poolSizes.push_back(VkDescriptorPoolSize{
            .type = ratio.type,
            .descriptorCount = uint32_t(ratio.ratio * maxSets)
        });
    }

	VkDescriptorPoolCreateInfo pool_info = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
	pool_info.flags = 0;
	pool_info.maxSets = maxSets;
	pool_info.poolSizeCount = (uint32_t)poolSizes.size();
	pool_info.pPoolSizes = poolSizes.data();

	vkCreateDescriptorPool(device, &pool_info, nullptr, &pool);
}

void DescriptorAllocator::clear(VkDevice device) {
    vkResetDescriptorPool(device, pool, 0);
}
void DescriptorAllocator::destroy(VkDevice device) {
    vkDestroyDescriptorPool(device,pool,nullptr);
}
VkDescriptorSet DescriptorAllocator::allocate(VkDevice device, VkDescriptorSetLayout layout) {
    VkDescriptorSetAllocateInfo allocInfo = { };
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.pNext = nullptr;
    allocInfo.descriptorPool = pool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &layout;

    VkDescriptorSet ds;
    VK_CHECK(vkAllocateDescriptorSets(device, &allocInfo, &ds));

    return ds;
}

}; // namespace build

/*
 * A thing we care in that structure is the AspectMask. This is going to be
 * either VK_IMAGE_ASPECT_COLOR_BIT or VK_IMAGE_ASPECT_DEPTH_BIT. For color and
 * depth images respectively. We wont need any of the other options. We will
 * keep it as Color aspect under all cases except when the target layout is
 * VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, which we will use later when we add
 * depth buffer.
 */
void transitionImage(
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
    imageBarrier.subresourceRange = mask::subresourceRange(aspectMask);
    imageBarrier.image = image;

    VkDependencyInfo depInfo {};
    depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    depInfo.pNext = nullptr;
    depInfo.imageMemoryBarrierCount = 1;
    depInfo.pImageMemoryBarriers = &imageBarrier;

    vkCmdPipelineBarrier2(cmd, &depInfo);
}

void copyImage(
    VkCommandBuffer cmd,
    VkImage source,
    VkImage destination,
    VkExtent2D srcSize,
    VkExtent2D dstSize
) {
	VkImageBlit2 blitRegion{ .sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2, .pNext = nullptr };

	blitRegion.srcOffsets[1].x = srcSize.width;
	blitRegion.srcOffsets[1].y = srcSize.height;
	blitRegion.srcOffsets[1].z = 1;

	blitRegion.dstOffsets[1].x = dstSize.width;
	blitRegion.dstOffsets[1].y = dstSize.height;
	blitRegion.dstOffsets[1].z = 1;

	blitRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	blitRegion.srcSubresource.baseArrayLayer = 0;
	blitRegion.srcSubresource.layerCount = 1;
	blitRegion.srcSubresource.mipLevel = 0;

	blitRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	blitRegion.dstSubresource.baseArrayLayer = 0;
	blitRegion.dstSubresource.layerCount = 1;
	blitRegion.dstSubresource.mipLevel = 0;

	VkBlitImageInfo2 blitInfo{ .sType = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2, .pNext = nullptr };
	blitInfo.dstImage = destination;
	blitInfo.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	blitInfo.srcImage = source;
	blitInfo.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	blitInfo.filter = VK_FILTER_LINEAR;
	blitInfo.regionCount = 1;
	blitInfo.pRegions = &blitRegion;

	vkCmdBlitImage2(cmd, &blitInfo);
}

} // namespace moss::ext::vulkan::utils
