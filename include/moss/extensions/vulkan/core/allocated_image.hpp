#pragma once

#include "moss/extensions/vulkan/meta.hpp"

namespace moss::extensions::vulkan {

/*
 * Generic VMA-backed image + view. Reused for the draw image and for every
 * image an attachment creates (SimImage sides).
 */
struct AllocatedImage {
    VkImage image;
    VkImageView imageView;
    VmaAllocation allocation;
    VkExtent2D drawExtent;
    VkExtent3D imageExtent;
    VkFormat imageFormat;
};

namespace utils {

/*
 * Creates a 2D, one-mip, one-layer, optimal-tiling, GPU-only image with a
 * colour view. Takes everything by value and calls vkCreate* internally, so
 * nothing dangles. The caller owns the result and must push the deletor
 * (capture device, allocator and the AllocatedImage by value).
 */
AllocatedImage createImage(
    VkDevice device,
    VmaAllocator allocator,
    VkFormat format,
    VkExtent2D extent,
    VkImageUsageFlags usage
);

} // namespace utils

}
