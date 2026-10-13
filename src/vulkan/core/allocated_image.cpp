#include "moss/extensions/vulkan/core/allocated_image.hpp"
#include "moss/extensions/vulkan/utils.hpp"
#include "moss/extensions/vulkan/logs.hpp"

namespace moss::extensions::vulkan::utils {

AllocatedImage createImage(
    VkDevice device,
    VmaAllocator allocator,
    VkFormat format,
    VkExtent2D extent,
    VkImageUsageFlags usage
) {
    AllocatedImage image = {};
    image.imageFormat = format;
    image.imageExtent = VkExtent3D{ extent.width, extent.height, 1 };
    image.drawExtent = extent;

    VkImageCreateInfo imageInfo = utils::info::imageCreate(
        image.imageFormat, usage, image.imageExtent
    );

    // GPU local memory: nothing here is ever read back by the cpu
    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    allocInfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    VK_CHECK(vmaCreateImage(
        allocator, &imageInfo, &allocInfo, &image.image, &image.allocation, nullptr
    ));

    VkImageViewCreateInfo viewInfo = utils::info::imageViewCreate(
        image.imageFormat, image.image, VK_IMAGE_ASPECT_COLOR_BIT
    );
    VK_CHECK(vkCreateImageView(device, &viewInfo, nullptr, &image.imageView));

    return image;
}

}
