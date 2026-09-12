#include "moss/extensions/vulkan/renderer.hpp"
#include "moss/extensions/vulkan/components.hpp"
#include "moss/extensions/vulkan/utils.hpp"
#include "moss/extensions/vulkan/logs.hpp"
#include <vulkan/vulkan_core.h>

namespace moss::extensions::vulkan {

void Renderer::build(const Key<key::WRITE>& key, const DynamicView& entities) {
    auto [windowSettings, renderSettings] = cmd::DynamicQuery<
        With< moss::extensions::vulkan::WindowSettings, moss::extensions::vulkan::RenderSettings > >
    ::init(key).pool(entities);

    auto [bindings] = cmd::DynamicQuery<
        With< moss::extensions::vulkan::Bindings > >
    ::init(key).pool(entities);

    initGlfw(windowSettings);
    initVulkan(renderSettings);
    initSwapchain(windowSettings);
    initCommands();
    initSyncStructures();
    initDescriptors(bindings);
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

    // Initialize VMA allocator
    VmaAllocatorCreateInfo allocatorInfo = {};
    allocatorInfo.physicalDevice = m_foundation.physicalDevice;
    allocatorInfo.device = m_foundation.device;
    allocatorInfo.instance = m_foundation.instance;
    allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
    vmaCreateAllocator(&allocatorInfo, &m_allocator);

    m_dqueue.push([&]() {
        vmaDestroyAllocator(m_allocator);
    });

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

	// Draw image size will match the window
	VkExtent3D drawImageExtent = {
		windowSettings.width,
		windowSettings.height,
		1
	};

	// Hardcoding the draw format to 32 bit float
	m_drawImage.imageFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
	m_drawImage.imageExtent = drawImageExtent;

	VkImageUsageFlags drawImageUsages{};
	drawImageUsages |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	drawImageUsages |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	drawImageUsages |= VK_IMAGE_USAGE_STORAGE_BIT;
	drawImageUsages |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

	VkImageCreateInfo rimgInfo = utils::info::imageCreate(
        m_drawImage.imageFormat,
        drawImageUsages,
        drawImageExtent
    );

	// For the draw image, we want to allocate it from gpu local memory
	VmaAllocationCreateInfo rimgAllocInfo = {};
	rimgAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
	rimgAllocInfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	//allocate and create the image
	vmaCreateImage(
        m_allocator,
        &rimgInfo,
        &rimgAllocInfo,
        &m_drawImage.image,
        &m_drawImage.allocation,
        nullptr
    );

	// Build a image-view for the draw image to use for rendering
	VkImageViewCreateInfo rview_info = utils::info::imageViewCreate(
        m_drawImage.imageFormat,
        m_drawImage.image,
        VK_IMAGE_ASPECT_COLOR_BIT
    );

	VK_CHECK(vkCreateImageView(
        m_foundation.device,
        &rview_info,
        nullptr,
        &m_drawImage.imageView
    ));

	// Add to deletion queues
	m_dqueue.push([=]() {
		vkDestroyImageView(m_foundation.device, m_drawImage.imageView, nullptr);
		vmaDestroyImage(m_allocator, m_drawImage.image, m_drawImage.allocation);
	});
}

void Renderer::initCommands() {
	// Create a command pool for commands submitted to the graphics queue.
	// We also want the pool to allow for resetting of individual command buffers
	VkCommandPoolCreateInfo cmdPoolInfo = utils::info::cmdPoolCreate(
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
		VkCommandBufferAllocateInfo cmdAllocInfo = utils::info::cmdBufAllocate(
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
	VkFenceCreateInfo fenceCreateInfo = utils::info::fenceCreate(
        VK_FENCE_CREATE_SIGNALED_BIT
    );
	VkSemaphoreCreateInfo semCreateInfo = utils::info::semaphoreCreate(0);

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

/*
* Currently this is very provisional. Nothing in this scales, and will have
* to be generalized to correctly work with the moss philosophy.
*/
void Renderer::initDescriptors(const Bindings& bindings) {
	// Create a descriptor pool that will hold 10 sets with 1 image each
	std::vector<utils::interface::DescriptorAllocator::PoolSizeRatio> sizes = {
		{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1 }
	};

	m_descriptors.allocator.init(m_foundation.device, 10, sizes);

	// Fetch info and make the descriptor set layout for our compute draw
	{
        VkDescriptorSetLayoutCreateInfo info = utils::info::descriptorSetLayoutCreate(
            bindings.bindings,
            VK_SHADER_STAGE_COMPUTE_BIT
        );
        VK_CHECK(vkCreateDescriptorSetLayout(
            m_foundation.device,
            &info,
            nullptr,
            &m_descriptors.drawImageLayout
        ));
	}

	// Allocate a descriptor set for our draw image
	m_descriptors.drawImage = m_descriptors.allocator.allocate(
        m_foundation.device,
        m_descriptors.drawImageLayout
    );

	// Update descriptor sets with the current screen
	VkDescriptorImageInfo imgInfo = {};
	imgInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
	imgInfo.imageView = m_drawImage.imageView;
	
	VkWriteDescriptorSet drawImageWrite = {};
	drawImageWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	drawImageWrite.pNext = nullptr;
	
	drawImageWrite.dstBinding = 0;
	drawImageWrite.dstSet = m_descriptors.drawImage;
	drawImageWrite.descriptorCount = 1;
	drawImageWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	drawImageWrite.pImageInfo = &imgInfo;

	vkUpdateDescriptorSets(m_foundation.device, 1, &drawImageWrite, 0, nullptr);

	// Descriptor allocator and the new layout get pushed to dqueue
	m_dqueue.push([&]() {
	    m_descriptors.allocator.destroy(m_foundation.device);

		vkDestroyDescriptorSetLayout(m_foundation.device, m_descriptors.drawImageLayout, nullptr);
	});
};

void Renderer::draw() {
	// Wait until the gpu has finished rendering the last frame.
    // Timeout in nanoseconds :O
    VK_CHECK(vkWaitForFences(
        m_foundation.device, 1, &getCurrentFrame().renderFence, true, 1000000000
    ));

    // Flush objects in per frame dqueue
    getCurrentFrame().dqueue.flush();

    VK_CHECK(vkResetFences(
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

    // Mapping the drawing dimensions to the drawImageExtent.
	m_drawImage.drawExtent.width  = m_drawImage.imageExtent.width;
	m_drawImage.drawExtent.height = m_drawImage.imageExtent.height;

    VkCommandBufferBeginInfo cmdBeginInfo = utils::info::cmdBufBegin(
        VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
    );

    // Begin Command Sequence To Buffer (simply a BeginDraw())
	VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));	

	// Transition our main draw image into general layout so we can write into it
    utils::transitionImage(
        cmd, m_drawImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL
    );

    // Draw the sinoidal background
	drawBackground(cmd, swapchainImageIndex);

	// Transition the draw image and the swapchain image into their correct transfer layouts
	utils::transitionImage(
        cmd, m_drawImage.image,
        VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
    );
	utils::transitionImage(
        cmd, m_swapchain.images[swapchainImageIndex],
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
    );

	// Execute a copy from the draw image into the swapchain
    utils::copyImage(
        cmd, m_drawImage.image, m_swapchain.images[swapchainImageIndex],
        m_drawImage.drawExtent, m_swapchain.extent
    );

	// Set swapchain image layout to present so we can show it on the screen
    utils::transitionImage(
        cmd, m_swapchain.images[swapchainImageIndex],
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
    );

	// Finalize the command buffer
	VK_CHECK(vkEndCommandBuffer(cmd));

    // Prepare the submission to the queue.
    // we want to wait on the presentSemaphore, as that semaphore is
    // signaled when the swapchain is ready we will signal the
    // renderSemaphore, to signal that rendering has finished
    VkCommandBufferSubmitInfo cmdinfo = utils::info::cmdBufSubmit(cmd);	
	
	VkSemaphoreSubmitInfo waitInfo = utils::info::semaphoreSubmit(
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR,
        getCurrentFrame().swapchainSemaphore
    );

	VkSemaphoreSubmitInfo signalInfo = utils::info::semaphoreSubmit(
        VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT,
        m_renderSemaphore[swapchainImageIndex] // Replaced getCurrentFrame().rS
    );	
	
	VkSubmitInfo2 submit = utils::info::submit(&cmdinfo,&signalInfo,&waitInfo);	

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

void Renderer::drawBackground(VkCommandBuffer cmd, u32 swapchainImageIndex) {
    // Provisional code for a flashing screen interpolating between black and blue
	VkClearColorValue clearValue;
	float flash = std::abs(std::sin(m_update.frameNumber / 120.f));
	clearValue = { { 0.0f, 0.0f, flash, 1.0f } };

	VkImageSubresourceRange clearRange = utils::mask::subresourceRange(
        VK_IMAGE_ASPECT_COLOR_BIT
    );

    // Clear Color Image Command
    vkCmdClearColorImage(
        cmd,
        m_swapchain.images[swapchainImageIndex],
        VK_IMAGE_LAYOUT_GENERAL,
        &clearValue,
        1,
        &clearRange
    );
}

void Renderer::cleanup() {
	if (m_foundation.initialized) {
        // Wait for it to become quite
		vkDeviceWaitIdle(m_foundation.device);

        // Per frame Command Pool and per frame sync objects
		for (int i = 0; i < FrameData::FRAME_OVERLAP; i++) {
			vkDestroyCommandPool(m_foundation.device, m_frames[i].commandPool, nullptr);
            vkDestroyFence(m_foundation.device, m_frames[i].renderFence, nullptr);
            vkDestroySemaphore(m_foundation.device, m_frames[i].swapchainSemaphore, nullptr);

            m_frames[i].dqueue.flush(); // flush per frame data
        }

        m_dqueue.flush(); // flush game data

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
