#pragma once

#include "moss/extensions/vulkan/meta.hpp"
#include "moss/extensions/vulkan/utils.hpp"
#include "moss/extensions/vulkan/core/allocated_image.hpp"

namespace moss::extensions::vulkan {

/*
 * Reserved resource name of the draw image. SimImage and SimBuffer may not use
 * it, and an image Binding with this name resolves to RenderCore::drawImage.
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
 *
 * Resize: the draw image, the swapchain image views and the render semaphores
 * have swapchain lifetime (swapchainDqueue) and are rebuilt together by
 * recreateSwapchain, which also bumps swapchainGeneration. An attachment that
 * caches anything derived from the draw image compares the generation with its
 * own copy at the start of tick and refreshes when it differs.
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
        // The size last asked for. FrameBegin compares the framebuffer size with
        // this, not with `extent`: a surface that clamps would otherwise rebuild
        // the swapchain every frame.
        VkExtent2D requested;
        // present said OUT_OF_DATE or SUBOPTIMAL: rebuild at the next FrameBegin
        bool dirty;
    };
    /* Copied from the settings components: needed again on every recreation */
    struct Config {
        VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
        VkFormat swapchainFormat = VK_FORMAT_B8G8R8A8_UNORM;
        VkFormat drawFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
        bool resizable = false;
        bool clearDraw = true;
        VkClearColorValue clearColor = { {0.0f, 0.0f, 0.0f, 1.0f } };
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
    Config config = {};
    FrameData frames[FrameData::FRAME_OVERLAP] = {};
    FrameState frame = {};
    AllocatedImage drawImage = {};
    VmaAllocator allocator = nullptr;

    // Incremented on every swapchain recreation (see the note above)
    u32 swapchainGeneration = 0;

    utils::interface::DeletionQueue dqueue;           // app lifetime, flushed LIFO in Core::exit
    utils::interface::DeletionQueue swapchainDqueue;  // swapchain lifetime: draw image, views, semaphores
    std::vector<VkSemaphore> renderSemaphores = {};   // sized to swapchain image count

    /* ------------------------------------------------------- */
    /* ----------------------- Inlined ----------------------- */
    /* ------------------------------------------------------- */
    FrameData& currentFrame() {
        return frames[frame.frameNumber % FrameData::FRAME_OVERLAP];
    }
};

}
