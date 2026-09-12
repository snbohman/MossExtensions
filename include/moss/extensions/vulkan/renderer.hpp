#pragma once

#include "moss/extensions/vulkan/meta.hpp"
#include "moss/extensions/vulkan/components.hpp"
#include "moss/extensions/vulkan/utils.hpp"


namespace moss::extensions::vulkan {

class Renderer : public moss::System {
public:
    void build(const Key<key::WRITE>& key, const DynamicView& entities) override;
    void tick(const Key<key::READ>& key) override;

private:
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
        utils::interface::DeletionQueue dqueue;
    };
    struct AllocatedImage {
        VkImage image;
        VkImageView imageView;
        VmaAllocation allocation;
        VkExtent2D drawExtent;
        VkExtent3D imageExtent;
        VkFormat imageFormat;
    };
    struct Descriptors {
        utils::interface::DescriptorAllocator allocator;
        VkDescriptorSet drawImage;
        VkDescriptorSetLayout drawImageLayout;
    };

    /* ------------------------------------------------------- */
    /* ----------------------- Members ----------------------- */
    /* ------------------------------------------------------- */
    Foundation m_foundation = {};
    Swapchain m_swapchain = {};
    FrameData m_frames[FrameData::FRAME_OVERLAP] = {};
    Update m_update = {};
    Descriptors m_descriptors = {};
    AllocatedImage m_drawImage;

    utils::interface::DeletionQueue m_dqueue;
    std::vector<VkSemaphore> m_renderSemaphore = {}; // Sized to swapImgCount
    VmaAllocator m_allocator;

    /* ------------------------------------------------------- */
    /* ----------------------- Methods ----------------------- */
    /* ------------------------------------------------------- */
    void initGlfw(WindowSettings windowSettings);
	void initVulkan(RenderSettings renderSettings);
	void initSwapchain(WindowSettings windowSettings);
	void initCommands();
	void initSyncStructures();
    void initDescriptors(const Bindings& bindings);
    void draw();
    void drawBackground(VkCommandBuffer cmd, u32 swapchainImageIndex);
    void cleanup();

    /* ------------------------------------------------------- */
    /* ----------------------- Inlined ----------------------- */
    /* ------------------------------------------------------- */
    FrameData& getCurrentFrame() {
        return m_frames[m_update.frameNumber % FrameData::FRAME_OVERLAP];
    }
};

}
