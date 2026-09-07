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
    void tick(const Key<key::READ>& key) override {
        cleanup();
        commands::Quit::init(key).quit();
    }

private:
    struct Foundation {
        GLFWwindow* window;
        VkInstance instance;
        VkDebugUtilsMessengerEXT debugMessenger;
        VkPhysicalDevice physicalDevice;
        VkDevice device;
        VkSurfaceKHR surface;
        VkQueue queue;      // all in one graphics queue. only using one general
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
        u32 frameNumber;
        bool stopRendering;
    };
    struct FrameData {
        VkCommandPool commandPool;
        VkCommandBuffer mainCommandBuffer;
        static constexpr u32 FRAME_OVERLAP = 2;
    };

    Foundation m_foundation = {};
    Swapchain m_swapchain = {};
    FrameData m_frames[FrameData::FRAME_OVERLAP] = {};
    Update m_update = {};

    void initGlfw(WindowSettings windowSettings);
	void initVulkan(RenderSettings renderSettings);
	void initSwapchain(WindowSettings windowSettings);
	void initCommands();
	void initSyncStructures();
    void cleanup();

    FrameData& getCurrentFrame() { return m_frames[m_update.frameNumber % FrameData::FRAME_OVERLAP]; }
};

}
