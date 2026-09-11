#pragma once

#include "moss/extensions/vulkan/meta.hpp"
#include "moss/extensions/vulkan/components.hpp"


namespace moss::extensions::vulkan {

class Renderer : public moss::System {
public:
    void build(const Key<key::WRITE>& key, const DynamicView& entities) override;
    void tick(const Key<key::READ>& key) override;

private:
    /*
     * From VkGuide, 2. Drawing Compute: Improving the render loop,
     * **(https://vkguide.dev/docs/new_chapter_2/vulkan_new_rendering/)**:
     * "Doing callbacks like this is inneficient at scale, because we are storing
     * whole std::functions for every object we are deleting, which is not going
     * to be optimal. For the amount of objects we will use in this tutorial,
     * its going to be fine. but if you need to delete thousands of objects and
     * want them deleted faster, a better implementation would be to store
     * arrays of vulkan handles of various types such as VkImage, VkBuffer, and
     * so on. And then delete those from a loop."
     */
    struct DeletionQueue {
        std::deque<std::function<void()>> deletors;

        void push(std::function<void()>&& function) {
            deletors.push_back(function);
        }

        void flush() {
            // Reverse iterate the deletion queue to execute all the functions
            for (auto it = deletors.rbegin(); it != deletors.rend(); it++)
                (*it)();
            deletors.clear();
        }
    };

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
        DeletionQueue dqueue;
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
        DeletionQueue dqueue;
    };
    struct AllocatedImage {
        VkImage image;
        VkImageView imageView;
        VmaAllocation allocation;
        VkExtent3D imageExtent;
        VkFormat imageFormat;
    };

    DeletionQueue m_dqueue;
    Foundation m_foundation = {};
    Swapchain m_swapchain = {};
    FrameData m_frames[FrameData::FRAME_OVERLAP] = {};
    std::vector<VkSemaphore> m_renderSemaphore = {}; // Sized to swapImgCount
    Update m_update = {};
    VmaAllocator m_allocator;
    AllocatedImage m_drawImage;
    VkExtent2D m_drawExtent;

    void initGlfw(WindowSettings windowSettings);
	void initVulkan(RenderSettings renderSettings);
	void initSwapchain(WindowSettings windowSettings);
	void initCommands();
	void initSyncStructures();
    void draw();
    void drawBackground(VkCommandBuffer cmd, u32 swapchainImageIndex);
    void cleanup();

    FrameData& getCurrentFrame() {
        return m_frames[m_update.frameNumber % FrameData::FRAME_OVERLAP];
    }
};

}
