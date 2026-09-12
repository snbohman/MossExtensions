#pragma once

#include <moss/moss.hpp>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_core.h>

namespace moss::extensions::vulkan::utils {

/* ------------------------------------------------------ */
/* ------------------------ Info ------------------------ */
/* ------------------------------------------------------ */
namespace info {

VkCommandPoolCreateInfo cmdPoolCreate(
    uint32_t queueFamilyIndex,
    VkCommandPoolCreateFlags flags
);
VkCommandBufferAllocateInfo cmdBufAllocate(
    VkCommandPool pool,
    uint32_t count
);

VkFenceCreateInfo fenceCreate(VkFenceCreateFlags flags);
VkSemaphoreCreateInfo semaphoreCreate(VkSemaphoreCreateFlags flags);
VkSemaphoreSubmitInfo semaphoreSubmit(
    VkPipelineStageFlags2 stageMask,
    VkSemaphore semaphore
);

VkCommandBufferBeginInfo cmdBufBegin(VkCommandBufferUsageFlags flags);
VkCommandBufferSubmitInfo cmdBufSubmit(VkCommandBuffer cmd);
VkSubmitInfo2 submit(
    VkCommandBufferSubmitInfo* cmd,
    VkSemaphoreSubmitInfo* signalSemaphoreInfo,
    VkSemaphoreSubmitInfo* waitSemaphoreInfo
);

VkImageCreateInfo imageCreate(
    VkFormat format,
    VkImageUsageFlags usageFlags,
    VkExtent3D extent
);
VkImageViewCreateInfo imageViewCreate(
    VkFormat format,
    VkImage image,
    VkImageAspectFlags aspectFlags
);

VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreate(
    std::vector<VkDescriptorSetLayoutBinding> bindings,
    VkShaderStageFlags shaderStages,
    void* pNext = nullptr,
    VkDescriptorSetLayoutCreateFlags flags = 0
);

} // namespace info

/* ------------------------------------------------------ */
/* ------------------------ Mask ------------------------ */
/* ------------------------------------------------------ */
namespace mask {

VkImageSubresourceRange subresourceRange(VkImageAspectFlags aspectMask);

} // namespace mask


/* ----------------------------------------------------------- */
/* ------------------------ Interface ------------------------ */
/* ----------------------------------------------------------- */
namespace interface {

struct DeletionQueue {
    std::deque<std::function<void()>> deletors;

    void push(std::function<void()>&& function);
    void flush();
};

struct DescriptorAllocator {
    VkDescriptorPool pool;
    struct PoolSizeRatio {
        VkDescriptorType type;
        float ratio;
    };

    void init(VkDevice device, uint32_t maxSets, std::span<PoolSizeRatio> poolRatios);
    void clear(VkDevice device);
    void destroy(VkDevice device);
    VkDescriptorSet allocate(VkDevice device, VkDescriptorSetLayout layout);
};

}; // namespace interface 

/* ------------------------------------------------------------ */
/* ------------------------ Non return ------------------------ */
/* ------------------------------------------------------------ */
void transitionImage(
    VkCommandBuffer cmd,
    VkImage image,
    VkImageLayout currentLayout,
    VkImageLayout newLayout
);

void copyImage(
    VkCommandBuffer cmd,
    VkImage source,
    VkImage destination,
    VkExtent2D srcSize,
    VkExtent2D dstSize
);

} // namespace moss::ext::vulkan::utils
