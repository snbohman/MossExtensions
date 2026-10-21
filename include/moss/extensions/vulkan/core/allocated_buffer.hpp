#pragma once

#include "moss/extensions/vulkan/meta.hpp"

namespace moss::extensions::vulkan {

/*
 * Generic VMA-backed buffer. Used for every side of a SimBuffer.
 */
struct AllocatedBuffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = nullptr;
    VkDeviceSize size = 0;
};

namespace utils {

/*
 * Creates a GPU-only, exclusive-sharing buffer. The caller owns the result and
 * must push the deletor (capture the allocator and the AllocatedBuffer by value).
 */
AllocatedBuffer createBuffer(
    VmaAllocator allocator,
    VkDeviceSize size,
    VkBufferUsageFlags usage
);

} // namespace utils

}
