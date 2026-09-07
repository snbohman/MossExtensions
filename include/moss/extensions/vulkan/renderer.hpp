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
    void initGlfw(WindowSettings windowSettings);
	void initVulkan(RenderSettings renderSettings);
	void initSwapchain(WindowSettings windowSettings);
	void initCommands();
	void initSyncStructures();
    void cleanup();

    void createSwapchain(WindowSettings windowSettings);
    void destroySwapchain();

    struct Foundation {
        GLFWwindow* window;
        VkInstance instance;
        VkDebugUtilsMessengerEXT debugMessenger;
        VkPhysicalDevice physicalDevice;
        VkDevice device;
        VkSurfaceKHR surface;
        bool initialized;
    };
    struct Swapchain {
        VkSwapchainKHR swapchain;
        VkFormat swapchainImageFormat;
        std::vector<VkImage> swapchainImages;
        std::vector<VkImageView> swapchainImageViews;
        VkExtent2D swapchainExtent;
    };
    struct FrameData { };

    Foundation m_foundation;
    Swapchain m_swapchain;
    FrameData m_frameData;
};

}
