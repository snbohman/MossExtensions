#pragma once

#include "moss/extensions/vulkan/utils.hpp"
#include "moss/meta/logs.hpp"
#include <moss/moss.hpp>
#include <VkBootstrap.h>
#include <GLFW/glfw3.h>
#include <raylib.h>
#include <vulkan/vulkan_core.h>
#include <moss/extensions/vulkan/components.hpp>


namespace moss::extensions::vulkan {

class Renderer : public moss::System {
public:
    void build(const Key<key::WRITE>& key, const DynamicView& entities) override { }

private:
    struct Components {
        Window window;
    };

    struct Init {
        GLFWwindow* window;
        vkb::Instance instance;
        vkb::InstanceDispatchTable instDisp;
        VkSurfaceKHR surface;
        vkb::Device device;
        vkb::DispatchTable disp;
        vkb::Swapchain swapchain;
    };

    struct RenderData {
        VkQueue graphics_queue;
        VkQueue present_queue;

        std::vector<VkImage> swapchain_images;
        std::vector<VkImageView> swapchain_image_views;
        std::vector<VkFramebuffer> framebuffers;

        VkRenderPass render_pass;
        VkPipelineLayout pipeline_layout;
        VkPipeline graphics_pipeline;

        VkCommandPool command_pool;
        std::vector<VkCommandBuffer> command_buffers;

        std::vector<VkSemaphore> available_semaphores;
        std::vector<VkSemaphore> finished_semaphore;
        std::vector<VkFence> in_flight_fences;
        std::vector<VkFence> image_in_flight;
        i32 current_frame = 0;
    };

    int initializeDevice(Init& init, Components& comps) {
        init.window = utils::createWindow(comps.window);

        vkb::InstanceBuilder instanceBuilder;
        auto iRet = instanceBuilder
            .use_default_debug_messenger()
            .request_validation_layers()
            .build();
        if (iRet) { M_ERROR("{}", iRet.error().message()); return -1; }

        init.instance = iRet.value();
        init.instDisp = init.instance.make_table();
        init.surface = utils::createSurface(init.instance, init.window);

        vkb::PhysicalDeviceSelector physicalDeviceSelector(init.instance);
        auto pdRet = physicalDeviceSelector
            .set_surface(init.surface)
            .select();
        if (!pdRet) {
            M_ERROR("{}", pdRet.error().message());
            if (pdRet.error() == vkb::PhysicalDeviceError::no_suitable_device) {
                const auto& detailed = pdRet.detailed_failure_reasons();
                if (!detailed.empty()) {
                    M_ERROR("GPU Selection failure reasons:");
                    for (const std::string& reason : detailed) {
                        M_ERROR("{}", reason);
                    }
                }
            }
            return -1;
        }

        vkb::PhysicalDevice physicalDevice = pdRet.value();
        vkb::DeviceBuilder deviceBuilder { physicalDevice };

        auto dRet = deviceBuilder.build();
        if (!dRet) {
            M_ERROR("{}", dRet.error().message());
            return -1;
        }

        init.device = dRet.value();
        init.disp = init.device.make_table();

        return 0;
    }

};

}
