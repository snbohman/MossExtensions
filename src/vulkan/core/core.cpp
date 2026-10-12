#include "moss/commands/primitives.hpp"
#include "moss/extensions/vulkan/meta.hpp"
#include "moss/extensions/vulkan/core/core.hpp"
#include "moss/extensions/vulkan/components.hpp"
#include "moss/extensions/vulkan/utils.hpp"
#include "moss/extensions/vulkan/logs.hpp"
#include <vulkan/vulkan_core.h>

#include <cstdlib>

namespace moss::extensions::vulkan {

namespace {

/*
 * vk-bootstrap's Result::value() is unchecked in release builds, so a failed
 * builder turns into undefined behaviour. Unwrap with a readable error instead.
 */
template<typename T>
T unwrap(vkb::Result<T> result, const char* what) {
    if (!result) {
        M_ERROR(
            "vk-bootstrap: {} failed: {} (VkResult {})",
            what, result.error().message(), result.error().value()
        );

        std::abort();
    }

    return std::move(result.value());
}

}

void Core::build(const Key<key::WRITE>& key, const DynamicView& entities) {
    auto rows = cmd::DynamicQuery<With<
        WindowSettings, RendererSettings
    >, cmd::View<
        Include<WindowSettings, RendererSettings>
    >>::init(key).atlas(entities);

    if (rows.empty()) {
        M_ERRORF("Core needs one entity with WindowSettings and RendererSettings in its Context");
        std::abort();
    }
    auto [windowSettings, renderSettings] = rows[0];

    // Publish first, then initialise in place: deletors never point at a local
    RenderCore& core = cmd::Resource<RenderCore>::init(key).emplace();

    initGlfw(core, windowSettings);
    initVulkan(core, renderSettings);
    initSwapchain(core, windowSettings);
    initCommands(core);
    initSyncStructures(core);
}

void Core::initGlfw(RenderCore& core, WindowSettings windowSettings) {
    glfwSetErrorCallback([](int code, const char* message) {
        M_ERROR("GLFW error {}: {}", code, message);
    });

    if (!glfwInit()) {
        M_ERRORF("glfwInit failed");
        std::abort();
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    if (!windowSettings.resize) glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    core.foundation.window = glfwCreateWindow(
        windowSettings.width,
        windowSettings.height,
        windowSettings.title,
        nullptr, nullptr
    );

    if (core.foundation.window == nullptr) {
        M_ERRORF("glfwCreateWindow failed");
        std::abort();
    }
}

void Core::initVulkan(RenderCore& core, RendererSettings rendererSettings) {
    vkb::InstanceBuilder builder;
    vkb::Instance vkbInst = unwrap(
        builder.set_app_name("~Moss Window~")
            .request_validation_layers(rendererSettings.validationLayers)
            .use_default_debug_messenger()
            .require_api_version(1, 3, 0)
            .build(),
        "Instance creation"
    );

    // Grab the instance 
    core.foundation.instance = vkbInst.instance;
    core.foundation.debugMessenger = vkbInst.debug_messenger;

    VK_CHECK(glfwCreateWindowSurface(
        core.foundation.instance, core.foundation.window, nullptr, &core.foundation.surface
    ));

    // Vulkan 1.3 features
    VkPhysicalDeviceVulkan13Features features{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES
    };
    features.dynamicRendering = true;
    features.synchronization2 = true;

    // Vulkan 1.2 features
    VkPhysicalDeviceVulkan12Features features12{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES
    };
    features12.bufferDeviceAddress = true;
    features12.descriptorIndexing = true;

    // Use vkbootstrap to select a gpu.
    // We want a gpu that can write to the GLFW surface and supports vulkan 1.3
    // with the correct features
    vkb::PhysicalDeviceSelector selector{vkbInst};
    vkb::PhysicalDevice physicalDevice = unwrap(
        selector
            .set_minimum_version(1, 3)
            .set_required_features_13(features)
            .set_required_features_12(features12)
            .set_surface(core.foundation.surface)
            .select(),
        "Physical device selection"
    );
    M_INFO("GPU: {}", physicalDevice.name);

    // Create the final vulkan device
    vkb::DeviceBuilder deviceBuilder{physicalDevice};
    vkb::Device vkbDevice = unwrap(deviceBuilder.build(), "Device creation");

    // Get the VkDevice handle used in the rest of a vulkan application
    core.foundation.device           = vkbDevice.device;
    core.foundation.physicalDevice   = physicalDevice.physical_device;

    // Use vkboostrap to get a Graphics queue and family
    core.foundation.graphicsQueue = unwrap(
        vkbDevice.get_queue(vkb::QueueType::graphics), "Graphics queue"
    );
    core.foundation.queueFamily = unwrap(
        vkbDevice.get_queue_index(vkb::QueueType::graphics), "Graphics queue family"
    );

    // Initialize VMA allocator
    VmaAllocatorCreateInfo allocatorInfo = {};
    allocatorInfo.physicalDevice = core.foundation.physicalDevice;
    allocatorInfo.device = core.foundation.device;
    allocatorInfo.instance = core.foundation.instance;
    allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
    VK_CHECK(vmaCreateAllocator(&allocatorInfo, &core.allocator));

    // Pushed first, so it is destroyed last (queue is flushed LIFO)
    core.dqueue.push([allocator = core.allocator]() {
        vmaDestroyAllocator(allocator);
    });

    // Flag foundation as initialized
    core.foundation.initialized = true;
}

void Core::initSwapchain(RenderCore& core, WindowSettings windowSettings) {
	core.swapchain.imageFormat  = VK_FORMAT_B8G8R8A8_UNORM;
	vkb::SwapchainBuilder swapchainBuilder{
        core.foundation.physicalDevice,
        core.foundation.device,
        core.foundation.surface
    };

	vkb::Swapchain vkbSwapchain = unwrap(
        swapchainBuilder
            .set_desired_format(VkSurfaceFormatKHR{
                .format = core.swapchain.imageFormat,
                .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR
            })
            .set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
            .set_desired_extent(windowSettings.width, windowSettings.height)
            .add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT)
            .build(),
        "Swapchain creation"
    );

	core.swapchain.extent = vkbSwapchain.extent;
	core.swapchain.swapchain = vkbSwapchain.swapchain;
	core.swapchain.images = unwrap(vkbSwapchain.get_images(), "Swapchain images");
	core.swapchain.imageViews = unwrap(vkbSwapchain.get_image_views(), "Swapchain image views");

	// Draw image size will match the window
	VkExtent3D drawImageExtent = {
		windowSettings.width,
		windowSettings.height,
		1
	};

	// Hardcoding the draw format to 16 bit float per channel
	core.drawImage.imageFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
	core.drawImage.imageExtent = drawImageExtent;

	VkImageUsageFlags drawImageUsages{};
	drawImageUsages |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	drawImageUsages |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	drawImageUsages |= VK_IMAGE_USAGE_STORAGE_BIT;
	drawImageUsages |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

	VkImageCreateInfo rimgInfo = utils::info::imageCreate(
        core.drawImage.imageFormat,
        drawImageUsages,
        drawImageExtent
    );

	// For the draw image, we want to allocate it from gpu local memory
	VmaAllocationCreateInfo rimgAllocInfo = {};
	rimgAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
	rimgAllocInfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	//allocate and create the image
	VK_CHECK(vmaCreateImage(
        core.allocator,
        &rimgInfo,
        &rimgAllocInfo,
        &core.drawImage.image,
        &core.drawImage.allocation,
        nullptr
    ));

	// Build a image-view for the draw image to use for rendering
	VkImageViewCreateInfo rview_info = utils::info::imageViewCreate(
        core.drawImage.imageFormat,
        core.drawImage.image,
        VK_IMAGE_ASPECT_COLOR_BIT
    );

	VK_CHECK(vkCreateImageView(
        core.foundation.device,
        &rview_info,
        nullptr,
        &core.drawImage.imageView
    ));

	// Add to deletion queue (everything captured by value)
	core.dqueue.push([device = core.foundation.device,
                      allocator = core.allocator,
                      image = core.drawImage]() {
		vkDestroyImageView(device, image.imageView, nullptr);
		vmaDestroyImage(allocator, image.image, image.allocation);
	});
}

void Core::initCommands(RenderCore& core) {
	// Create a command pool for commands submitted to the graphics queue.
	// We also want the pool to allow for resetting of individual command buffers
	VkCommandPoolCreateInfo cmdPoolInfo = utils::info::cmdPoolCreate(
        core.foundation.queueFamily,
        VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT
    );
	
	for (u32 i = 0; i < RenderCore::FrameData::FRAME_OVERLAP; i++) {
		VK_CHECK(vkCreateCommandPool(
            core.foundation.device,
            &cmdPoolInfo,
            nullptr,
            &core.frames[i].commandPool
        ));

		// Allocate the default command buffer that we will use for rendering
		VkCommandBufferAllocateInfo cmdAllocInfo = utils::info::cmdBufAllocate(
            core.frames[i].commandPool,
            1
        );

        // Allocate command buffer for every frame (2 frames)
        VK_CHECK(vkAllocateCommandBuffers(
            core.foundation.device,
            &cmdAllocInfo,
            &core.frames[i].mainCommandBuffer
        ));
	}
}

void Core::initSyncStructures(RenderCore& core) {
	// One fence to control when the gpu has finished rendering the frame,
	// and a semaphore to syncronize rendering with the swapchain.
	// We want the fence to start signalled so we can wait on it on the first frame
	VkFenceCreateInfo fenceCreateInfo = utils::info::fenceCreate(
        VK_FENCE_CREATE_SIGNALED_BIT
    );
	VkSemaphoreCreateInfo semCreateInfo = utils::info::semaphoreCreate(0);

	for (u32 i = 0; i < RenderCore::FrameData::FRAME_OVERLAP; i++) {
		VK_CHECK(vkCreateFence(
            core.foundation.device, &fenceCreateInfo, nullptr, &core.frames[i].renderFence
        ));
        VK_CHECK(vkCreateSemaphore(
            core.foundation.device, &semCreateInfo, nullptr, &core.frames[i].swapchainSemaphore
        ));
	}

    /*
     * Create a seperate renderSemaphore per swapchain image because
     * swapchain.images.size() != FRAME_OVERLAP. Indexed by acquired image index.
     */
    core.renderSemaphores.resize(core.swapchain.images.size());
    for (auto& sem : core.renderSemaphores) {
        VK_CHECK(vkCreateSemaphore(core.foundation.device, &semCreateInfo, nullptr, &sem));
    }
}

/*
 * Teardown (the old Renderer::cleanup, which was never called). Attachments
 * need no exit(): everything they create has a deletor in core.dqueue, which
 * is flushed here in reverse order of creation.
 */
void Core::exit(const Key<key::WRITE>& key) {
    auto resource = cmd::Resource<RenderCore>::init(key);
    RenderCore* found = resource.find();
    if (found == nullptr || !found->foundation.initialized) return;

    RenderCore& core = *found;
    VkDevice device = core.foundation.device;

    // Wait for it to become quiet
    vkDeviceWaitIdle(device);

    // Per frame deletors, then per frame command pool and sync objects
    for (u32 i = 0; i < RenderCore::FrameData::FRAME_OVERLAP; i++) {
        core.frames[i].dqueue.flush();

        vkDestroyCommandPool(device, core.frames[i].commandPool, nullptr);
        vkDestroyFence(device, core.frames[i].renderFence, nullptr);
        vkDestroySemaphore(device, core.frames[i].swapchainSemaphore, nullptr);
    }

    // App lifetime deletors: attachments first, allocator last
    core.dqueue.flush();

    // Separately allocated per swapchain image render semaphores
    for (auto& sem : core.renderSemaphores)
        vkDestroySemaphore(device, sem, nullptr);

    // Image views before the swapchain they were created from
    for (auto& view : core.swapchain.imageViews)
        vkDestroyImageView(device, view, nullptr);
    vkDestroySwapchainKHR(device, core.swapchain.swapchain, nullptr);

    // Surface and device
    vkDestroySurfaceKHR(core.foundation.instance, core.foundation.surface, nullptr);
    vkDestroyDevice(device, nullptr);

    // Messenger, instance
    vkb::destroy_debug_utils_messenger(core.foundation.instance, core.foundation.debugMessenger);
    vkDestroyInstance(core.foundation.instance, nullptr);

    // GLFW
    glfwDestroyWindow(core.foundation.window);
    glfwTerminate();

    resource.erase();
}

}
