#include "moss/extensions/vulkan/core/allocated_buffer.hpp"
#include "moss/extensions/vulkan/logs.hpp"

namespace moss::extensions::vulkan::utils {

AllocatedBuffer createBuffer(
    VmaAllocator allocator,
    VkDeviceSize size,
    VkBufferUsageFlags usage
) {
    AllocatedBuffer result = {};
    result.size = size;

    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.pNext = nullptr;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    // GPU local memory: nothing here is ever read back by the cpu
    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    allocInfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    VK_CHECK(vmaCreateBuffer(
        allocator, &bufferInfo, &allocInfo, &result.buffer, &result.allocation, nullptr
    ));

    return result;
}

}
