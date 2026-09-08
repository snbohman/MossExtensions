#include <vulkan/vulkan_core.h>
#include <GLFW/glfw3.h>
#include <VkBootstrap.h>

#include <moss/moss.hpp>
#include "moss/extensions/vulkan/renderer.hpp"
#include "moss/extensions/vulkan/components.hpp"
#include "moss/extensions/vulkan/logs.hpp"
#include "moss/extensions/vulkan/utils.hpp"


namespace moss::extensions::vulkan {

void Renderer::build(const Key<key::WRITE>& key, const DynamicView& entities) {
    auto [windowSettings] = cmd::DynamicQuery<
        With< moss::extensions::vulkan::WindowSettings> >
    ::init(key).pool(entities);

    auto [renderSettings] = cmd::DynamicQuery<
        With< moss::extensions::vulkan::RenderSettings> >
    ::init(key).pool(entities);

    initGlfw(windowSettings);
    initVulkan(renderSettings);
    initSwapchain(windowSettings);
    initCommands();
    initSyncStructures();
}

void Renderer::tick(const Key<key::READ>& key) {
    if (glfwWindowShouldClose(m_foundation.window)) {
        moss::cmd::Quit::init(key).quit();
    }

    glfwPollEvents();
    draw();
} 

void Renderer::initGlfw(WindowSettings windowSettings) {
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    if (!windowSettings.resize) glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    m_foundation.window = glfwCreateWindow(
        windowSettings.width,
        windowSettings.height,
        windowSettings.title,
        nullptr, nullptr
    );
}

void Renderer::initVulkan(RenderSettings renderSettings) {
    vkb::InstanceBuilder builder;
    vkb::Instance vkbInst = builder.set_app_name("~Moss Window~")
        .request_validation_layers(renderSettings.validationLayers)
        .use_default_debug_messenger()
        .require_api_version(1, 3, 0)
        .build()
        .value();

    // Grab the instance 
    m_foundation.instance = vkbInst.instance;
    m_foundation.debugMessenger = vkbInst.debug_messenger;

    VkResult err = glfwCreateWindowSurface(
        m_foundation.instance, m_foundation.window, nullptr, &m_foundation.surface
    );

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
    vkb::PhysicalDevice physicalDevice = selector
        .set_minimum_version(1, 3)
        .set_required_features_13(features)
        .set_required_features_12(features12)
        .set_surface(m_foundation.surface)
        .select()
        .value();

    // Create the final vulkan device
    vkb::DeviceBuilder deviceBuilder{physicalDevice};
    vkb::Device vkbDevice = deviceBuilder.build().value();

    // Get the VkDevice handle used in the rest of a vulkan application
    m_foundation.device           = vkbDevice.device;
    m_foundation.physicalDevice   = physicalDevice.physical_device;

    // Use vkboostrap to get a Graphics queue and family
    m_foundation.graphicsQueue = vkbDevice.get_queue(vkb::QueueType::graphics).value();
    m_foundation.queueFamily = vkbDevice.get_queue_index(vkb::QueueType::graphics).value();

    // Flag foundation as initialized
    m_foundation.initialized = true;
}

void Renderer::initSwapchain(WindowSettings windowSettings) {
	m_swapchain.imageFormat  = VK_FORMAT_B8G8R8A8_UNORM;
	vkb::SwapchainBuilder swapchainBuilder{
        m_foundation.physicalDevice,
        m_foundation.device,
        m_foundation.surface
    };
	vkb::Swapchain vkbSwapchain = swapchainBuilder
		.set_desired_format(VkSurfaceFormatKHR{
            .format = m_swapchain.imageFormat,
            .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR
        })
		.set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
		.set_desired_extent(windowSettings.width, windowSettings.height)
		.add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT)
		.build()
		.value();

	m_swapchain.extent = vkbSwapchain.extent;
	m_swapchain.swapchain = vkbSwapchain.swapchain;
	m_swapchain.images = vkbSwapchain.get_images().value();
	m_swapchain.imageViews = vkbSwapchain.get_image_views().value();
}

void Renderer::initCommands() {
	// Create a command pool for commands submitted to the graphics queue.
	// We also want the pool to allow for resetting of individual command buffers
	VkCommandPoolCreateInfo cmdPoolInfo = utils::init::cmdPoolCreateInfo(
        m_foundation.queueFamily,
        VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT
    );
	
	for (int i = 0; i < FrameData::FRAME_OVERLAP; i++) {
		VK_CHECK(vkCreateCommandPool(
            m_foundation.device,
            &cmdPoolInfo,
            nullptr,
            &m_frames[i].commandPool
        ));

		// Allocate the default command buffer that we will use for rendering
		VkCommandBufferAllocateInfo cmdAllocInfo = utils::init::cmdBufAllocateInfo(
            m_frames[i].commandPool,
            1
        );

        // Allocate command buffer for every frame (2 frames)
        VK_CHECK(vkAllocateCommandBuffers(
            m_foundation.device,
            &cmdAllocInfo,
            &m_frames[i].mainCommandBuffer
        ));
	}
}

void Renderer::initSyncStructures() {
	// Create syncronization structures
	// One fence to control when the gpu has finished rendering the frame,
	// and 2 semaphores to syncronize rendering with swapchain
	// We want the fence to start signalled so we can wait on it on the first frame
	VkFenceCreateInfo fenceCreateInfo = utils::init::fenceCreateInfo(
        VK_FENCE_CREATE_SIGNALED_BIT
    );
	VkSemaphoreCreateInfo semCreateInfo = utils::init::semCreateInfo(0);

	for (int i = 0; i < FrameData::FRAME_OVERLAP; i++) {
		VK_CHECK(vkCreateFence(
            m_foundation.device, &fenceCreateInfo, nullptr, &m_frames[i].renderFence
        )); VK_CHECK(vkCreateSemaphore(
            m_foundation.device, &semCreateInfo, nullptr, &m_frames[i].swapchainSemaphore
        ));
	}

    /*
     * Create a seperate renderSemaphore becuase swapchain.images.size() !=
     * FRAME_OVERLAP
     */
    m_renderSemaphore.resize(m_swapchain.images.size());
    for (auto& sem : m_renderSemaphore) {
        VK_CHECK(vkCreateSemaphore(m_foundation.device, &semCreateInfo, nullptr, &sem));
    }
}

void Renderer::draw() {
	// Wait until the gpu has finished rendering the last frame.
    // Timeout in nanoseconds :O
    VK_CHECK(vkWaitForFences(
        m_foundation.device, 1, &getCurrentFrame().renderFence, true, 1000000000
    )); VK_CHECK(vkResetFences(
        m_foundation.device, 1, &getCurrentFrame().renderFence
    ));

    // Requesting image from the swapchain
	u32 swapchainImageIndex;
	VK_CHECK(vkAcquireNextImageKHR(
        m_foundation.device,
        m_swapchain.swapchain,
        1000000000,
        getCurrentFrame().swapchainSemaphore,
        nullptr,
        &swapchainImageIndex
    ));

    // Resetting current frame mainCommandBuffer after waiting for semaphores and fences
    VkCommandBuffer cmd = getCurrentFrame().mainCommandBuffer;
    VK_CHECK(vkResetCommandBuffer(cmd, 0));

    // Command Start Info
    VkCommandBufferBeginInfo cmdBeginInfo = utils::init::cmdBufBeginInfo(
        VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
    );
    VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

    /*
     * Now its time to start drawing!
     * First make swapchain image into writeable mode before rendering.
     * Then render a clear-color frame number flashing on screen
     * Then finilize the swapchain image into presentable mode
     *
     * The target layout we want is VK_IMAGE_LAYOUT_GENERAL. This is a general
     * purpose layout, which allows reading and writing from the image. Its not
     * the most optimal layout for rendering, but it is the one we want for
     * vkCmdClearColorImage . This is the image layout you want to use if you
     * want to write a image from a compute shader. If you want a read-only
     * image or a image to be used with rasterization commands, there are better
     * options
     * (https://docs.vulkan.org/spec/latest/chapters/resources.html#resources-image-layouts)
     */
    utils::transitionImage(
        cmd,
        m_swapchain.images[swapchainImageIndex],
        VK_IMAGE_LAYOUT_UNDEFINED, // old layout: undefined
        VK_IMAGE_LAYOUT_GENERAL    // new layout: writeable mode 
    );

    // Provisional code for a flashing screen interpolating between black and blue
	VkClearColorValue clearValue;
	float flash = std::abs(std::sin(m_update.frameNumber / 120.f));
	clearValue = { { 0.0f, 0.0f, flash, 1.0f } };

	VkImageSubresourceRange clearRange = utils::init::subresourceRange(
        VK_IMAGE_ASPECT_COLOR_BIT
    );

    // Clear Color Image Command! First drawing command!!
    vkCmdClearColorImage(
        cmd,
        m_swapchain.images[swapchainImageIndex],
        VK_IMAGE_LAYOUT_GENERAL,
        &clearValue,
        1,
        &clearRange
    );

    // Present swapchain with transition image
	utils::transitionImage(
        cmd,
        m_swapchain.images[swapchainImageIndex],
        VK_IMAGE_LAYOUT_GENERAL,        // old layout: writable mode
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR // new layout: present mode
    );

	/*
     * Finalize the command buffer (we can no longer add commands,
     * but it can now be executed)
     *
     * With this, we now have a fine command buffer that is recorded and
     * ready to be dispatched into the gpu. We could call VkQueueSubmit
     * already, but its going to be of little use right now as we need to
     * also connect the syncronization structures for the logic to interact
     * correctly with the swapchain.
     */
	VK_CHECK(vkEndCommandBuffer(cmd));

    // Prepare the submission to the queue.
    // we want to wait on the presentSemaphore, as that semaphore is
    // signaled when the swapchain is ready we will signal the
    // renderSemaphore, to signal that rendering has finished
    VkCommandBufferSubmitInfo cmdinfo = utils::init::cmdBufSubmitInfo(cmd);	
	
	VkSemaphoreSubmitInfo waitInfo = utils::init::semSubmitInfo(
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR,
        getCurrentFrame().swapchainSemaphore
    );

	VkSemaphoreSubmitInfo signalInfo = utils::init::semSubmitInfo(
        VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT,
        m_renderSemaphore[swapchainImageIndex] // Replaced getCurrentFrame().rS
    );	
	
	VkSubmitInfo2 submit = utils::init::submitInfo(&cmdinfo,&signalInfo,&waitInfo);	

	// Submit command buffer to the queue and execute it.
	// renderFence will now block until the graphic commands finish execution
	VK_CHECK(vkQueueSubmit2(
        m_foundation.graphicsQueue,
        1,
        &submit,
        getCurrentFrame().renderFence
    ));

    // Prepare present: This will put the image we just rendered to into the
    // visible window. We want to wait on the renderSemaphore for that, as
    // its necessary that drawing commands have finished before the image is
    // displayed to the user.
    VkPresentInfoKHR presentInfo = {};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presentInfo.pNext = nullptr;
	presentInfo.pSwapchains = &m_swapchain.swapchain;
	presentInfo.swapchainCount = 1;

    // Replaced getCurrentFrame().rS
	presentInfo.pWaitSemaphores = &m_renderSemaphore[swapchainImageIndex];
	presentInfo.waitSemaphoreCount = 1;

	presentInfo.pImageIndices = &swapchainImageIndex;

	VK_CHECK(vkQueuePresentKHR(m_foundation.graphicsQueue, &presentInfo));

	// Tick the frame number as a final last step for the draw function
	m_update.frameNumber++;
}

void Renderer::cleanup() {
	if (m_foundation.initialized) {
        // Per frame Command Pool and per frame sync objects
		vkDeviceWaitIdle(m_foundation.device);
		for (int i = 0; i < FrameData::FRAME_OVERLAP; i++) {
			vkDestroyCommandPool(m_foundation.device, m_frames[i].commandPool, nullptr);
            vkDestroyFence(m_foundation.device, m_frames[i].renderFence, nullptr);
            vkDestroySemaphore(m_foundation.device, m_frames[i].swapchainSemaphore, nullptr);
        }

        // Destroy seperatly allocated per swapchain image render semaphore
        for (auto& sem : m_renderSemaphore)
            vkDestroySemaphore(m_foundation.device, sem, nullptr);

        // Swapchain and image views
        vkDestroySwapchainKHR(m_foundation.device, m_swapchain.swapchain, nullptr);
        for (int i = 0; i < m_swapchain.imageViews.size(); i++)
            vkDestroyImageView(m_foundation.device, m_swapchain.imageViews[i], nullptr);

        // Surface and device
		vkDestroySurfaceKHR(m_foundation.instance, m_foundation.surface, nullptr);
		vkDestroyDevice(m_foundation.device, nullptr);
		
        // Messengar, instance
		vkb::destroy_debug_utils_messenger(m_foundation.instance, m_foundation.debugMessenger);
		vkDestroyInstance(m_foundation.instance, nullptr);

        // GLFW
		glfwDestroyWindow(m_foundation.window);
        glfwTerminate();
	}
}

}
