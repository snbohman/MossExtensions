#pragma once

#include "moss/extensions/vulkan/meta.hpp"
#include "moss/extensions/vulkan/utils.hpp"
#include "moss/extensions/vulkan/core/allocated_image.hpp"

namespace moss::extensions::vulkan {

/*
 * Reserved resource name of the draw image. SimImage may not use it, and a
 * Binding with this name resolves to RenderCore::drawImage.
 */
inline constexpr const char* DRAW_IMAGE_NAME = "draw";

/*
 * The shared state of the Vulkan core. Stored as a moss resource:
 *
 *     auto& core = cmd::Resource<RenderCore>::init(key).get();
 *
 * Plain data with public fields on purpose: it is shared between the core
 * systems and every attachment. Created by Core::build, erased by Core::exit.
 *
 * Do not capture a RenderCore reference/pointer in a deletion-queue lambda.
 * Capture the handles you need (device, allocator, ...) by value instead.
 */
struct RenderCore {
    /* -------------------------------------------------------------------- */
    /* ----------------------- Categorising Structs ----------------------- */
    /* -------------------------------------------------------------------- */
    struct Foundation {
        GLFWwindow* window;
        VkInstance instance;
        VkDebugUtilsMessengerEXT debugMessenger;
        VkPhysicalDevice physicalDevice;
        VkDevice device;
        VkSurfaceKHR surface;
        VkQueue graphicsQueue;      // all in one graphics queue. only using 1 general
        u32 queueFamily;
        bool initialized;
    };
    struct Swapchain {
        VkSwapchainKHR swapchain;
        VkFormat imageFormat;
        std::vector<VkImage> images;
        std::vector<VkImageView> imageViews;
        VkExtent2D extent;
    };
    struct FrameData {
        VkCommandPool commandPool;
        VkCommandBuffer mainCommandBuffer;
        VkSemaphore swapchainSemaphore;
        VkFence renderFence;
        static constexpr u32 FRAME_OVERLAP = 2;
        utils::interface::DeletionQueue dqueue; // per-frame-in-flight lifetime
    };
    /* Filled by FrameBegin every frame, read by attachments while recording */
    struct FrameState {
        u32 frameNumber = 0;
        u32 swapchainImageIndex = 0;
        VkCommandBuffer cmd = VK_NULL_HANDLE;
    };

    /* ------------------------------------------------------- */
    /* ----------------------- Members ----------------------- */
    /* ------------------------------------------------------- */
    Foundation foundation = {};
    Swapchain swapchain = {};
    FrameData frames[FrameData::FRAME_OVERLAP] = {};
    FrameState frame = {};
    AllocatedImage drawImage = {};
    VmaAllocator allocator = nullptr;

    utils::interface::DeletionQueue dqueue;          // app lifetime, flushed LIFO in Core::exit
    std::vector<VkSemaphore> renderSemaphores = {};  // sized to swapchain image count

    /* ------------------------------------------------------- */
    /* ----------------------- Inlined ----------------------- */
    /* ------------------------------------------------------- */
    FrameData& currentFrame() {
        return frames[frame.frameNumber % FrameData::FRAME_OVERLAP];
    }
};

}
