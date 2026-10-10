#pragma once

#include "moss/extensions/vulkan/meta.hpp"
#include "moss/extensions/vulkan/components.hpp"
#include "moss/extensions/vulkan/utils.hpp"
#include "moss/extensions/vulkan/pipelines.hpp"

#include <string>
#include <unordered_map>
#include <vector>

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
        VkSampler sampler; // shared linear/clamp sampler for COMBINED_IMAGE_SAMPLER bindings
    };

    /* Plain copies of ECS data, so the queries are separate from Vulkan init */
    struct ImageConfig {
        std::string name;
        VkFormat format;
    };
    struct PassConfig {
        int order = 0;
        PipelineKind kind = PipelineKind::Compute;

        std::string computePath;
        std::string vertexPath;
        std::string fragmentPath;

        std::vector<VkDescriptorSetLayoutBinding> layout; // parallel to resources
        std::vector<Binding> resources;

        // Graphics fixed-function state (copied out of PipelineState)
        VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkCullModeFlags cullMode = VK_CULL_MODE_NONE;
        VkFrontFace frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        bool blendEnable = false;

        u32 groupX = 1, groupY = 1, groupZ = 1;        // compute
        u32 vertexCount = 3, instanceCount = 1;        // graphics

        u32 repeat = 1;                                // PassRepeat
        bool pingPong = false;                         // PingPong
        std::string pingA;
        std::string pingB;
    };
    /* Everything the draw loop needs for one pass, sorted by PassOrder */
    struct CompiledPass {
        int order;
        PipelineKind kind;

        VkPipeline pipeline;
        VkPipelineLayout layout;
        VkDescriptorSetLayout setLayout;
        VkDescriptorSet sets[2];   // [1] only used when ping-ponging
        u32 setCount;

        u32 repeat;
        u32 groupX, groupY, groupZ;
        u32 vertexCount, instanceCount;

        // Barrier bookkeeping
        std::vector<std::string> touched;   // resolved resource keys (both ping-pong variants)
        VkPipelineStageFlags2 stage;
        VkAccessFlags2 writeAccess;
        VkAccessFlags2 readWriteAccess;
    };

    /* ------------------------------------------------------- */
    /* ----------------------- Members ----------------------- */
    /* ------------------------------------------------------- */
    static constexpr const char* DRAW_IMAGE_NAME = "draw";

    Foundation m_foundation = {};
    Swapchain m_swapchain = {};
    FrameData m_frames[FrameData::FRAME_OVERLAP] = {};
    Update m_update = {};
    Descriptors m_descriptors = {};
    AllocatedImage m_drawImage;

    std::unordered_map<std::string, AllocatedImage> m_images = {}; // SimImage entities
    std::vector<CompiledPass> m_passes = {};                       // sorted by PassOrder
    bool m_simImagesReady = false;                                 // one-time layout/clear done?

    utils::interface::DeletionQueue m_dqueue;
    std::vector<VkSemaphore> m_renderSemaphore = {}; // Sized to swapImgCount
    VmaAllocator m_allocator;

    /* ------------------------------------------------------- */
    /* ----------------------- Methods ----------------------- */
    /* ------------------------------------------------------- */
    std::vector<ImageConfig> gatherImages(const Key<key::WRITE>& key, const DynamicView& entities);
    std::vector<PassConfig> gatherPasses(const Key<key::WRITE>& key, const DynamicView& entities);

    void initGlfw(WindowSettings windowSettings);
	void initVulkan(RendererSettings rendererSettings);
	void initSwapchain(WindowSettings windowSettings);
	void initCommands();
	void initSyncStructures();
    void initImages(const std::vector<ImageConfig>& configs);
    void initDescriptors(const std::vector<PassConfig>& configs);
    void initPipelines(const std::vector<PassConfig>& configs);

    std::string bindingKey(const PassConfig& config, const Binding& binding, bool swapped) const;
    const AllocatedImage& resolveImage(const std::string& key) const;
    void writeDescriptorSet(VkDescriptorSet set, const PassConfig& config, bool swapped);

    void draw();
    void drawPasses(VkCommandBuffer cmd);
    void cleanup();

    /* ------------------------------------------------------- */
    /* ----------------------- Inlined ----------------------- */
    /* ------------------------------------------------------- */
    FrameData& getCurrentFrame() {
        return m_frames[m_update.frameNumber % FrameData::FRAME_OVERLAP];
    }
};

}
