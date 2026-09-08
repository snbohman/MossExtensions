#pragma once

#include <vulkan/vulkan_core.h>
#include <GLFW/glfw3.h>
#include <VkBootstrap.h>

#include <moss/moss.hpp>
#include "moss/extensions/vulkan/components.hpp"


namespace moss::extensions::vulkan {

class Renderer : public moss::System {
public:
    void build(const Key<key::WRITE>& key, const DynamicView& entities) override;
    void tick(const Key<key::READ>& key) override;

private:
    struct Foundation {
        GLFWwindow* window;
        VkInstance instance;
        VkDebugUtilsMessengerEXT debugMessenger;
        VkPhysicalDevice physicalDevice;
        VkDevice device;
        VkSurfaceKHR surface;
        VkQueue graphicsQueue;      // all in one graphics queue. only using one general
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
    struct Update {
        u32 frameNumber = 0;
        bool stopRendering = false;
    };
    struct FrameData {
        VkCommandPool commandPool;
        VkCommandBuffer mainCommandBuffer;
        VkSemaphore swapchainSemaphore;
        VkFence renderFence;
        static constexpr u32 FRAME_OVERLAP = 2;
    };

    // Sized to swapchain image count, NOT FRAME_OVERLAP
    // Finished Render Semaphore
    // On swapchain recreation (resize): destroy and recreate
    // renderFinishedSemaphores too, resized to the new image count
    //
    // Cleanup: destroy each semaphore in renderFinishedSemaphores
    // shutdown code, separately from per-FrameData cleanup loop.
    std::vector<VkSemaphore> m_renderSemaphore = {};

    Foundation m_foundation = {};
    Swapchain m_swapchain = {};
    FrameData m_frames[FrameData::FRAME_OVERLAP] = {};
    Update m_update = {};

    void initGlfw(WindowSettings windowSettings);
	void initVulkan(RenderSettings renderSettings);
	void initSwapchain(WindowSettings windowSettings);
	void initCommands();
	void initSyncStructures();
    void draw();
    void cleanup();

    FrameData& getCurrentFrame() { return m_frames[m_update.frameNumber % FrameData::FRAME_OVERLAP]; }
};

}
