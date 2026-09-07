#include <vulkan/vulkan_core.h>
#include <GLFW/glfw3.h>
#include <VkBootstrap.h>

#include <moss/moss.hpp>
#include "moss/extensions/vulkan/renderer.hpp"
#include "moss/extensions/vulkan/components.hpp"


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

void Renderer::initGlfw(WindowSettings windowSettings) {
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    if (!windowSettings.resize) glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    m_foundation.window = glfwCreateWindow(
        windowSettings.width,
        windowSettings.height,
        windowSettings.title,
        NULL, NULL
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

    //grab the instance 
    m_foundation.instance = vkbInst.instance;
    m_foundation.debugMessenger = vkbInst.debug_messenger;

    VkResult err = glfwCreateWindowSurface(
        m_foundation.instance, m_foundation.window, nullptr, &m_foundation.surface
    );

    // vulkan 1.3 features
    VkPhysicalDeviceVulkan13Features features{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES
    };
    features.dynamicRendering = true;
    features.synchronization2 = true;

    // vulkan 1.2 features
    VkPhysicalDeviceVulkan12Features features12{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES
    };
    features12.bufferDeviceAddress = true;
    features12.descriptorIndexing = true;

    // use vkbootstrap to select a gpu.
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

    // create the final vulkan device
    vkb::DeviceBuilder deviceBuilder{physicalDevice};
    vkb::Device vkbDevice = deviceBuilder.build().value();

    // Get the VkDevice handle used in the rest of a vulkan application
    m_foundation.device           = vkbDevice.device;
    m_foundation.physicalDevice   = physicalDevice.physical_device;

    m_foundation.initialized = true;
}

void Renderer::initSwapchain(WindowSettings windowSettings) {
    createSwapchain(windowSettings);
}

void Renderer::initCommands() { }
void Renderer::initSyncStructures() { }
void Renderer::cleanup() {
	if (m_foundation.initialized) {
		destroySwapchain();

		vkDestroySurfaceKHR(m_foundation.instance, m_foundation.surface, nullptr);
		vkDestroyDevice(m_foundation.device, nullptr);
		
		vkb::destroy_debug_utils_messenger(m_foundation.instance, m_foundation.debugMessenger);
		vkDestroyInstance(m_foundation.instance, nullptr);
		glfwDestroyWindow(m_foundation.window);
	}
}

void Renderer::createSwapchain(WindowSettings windowSettings) {
	m_swapchain.swapchainImageFormat  = VK_FORMAT_B8G8R8A8_UNORM;
	vkb::SwapchainBuilder swapchainBuilder{
        m_foundation.physicalDevice,
        m_foundation.device,
        m_foundation.surface
    };
	vkb::Swapchain vkbSwapchain = swapchainBuilder
		.set_desired_format(VkSurfaceFormatKHR{
            .format = m_swapchain.swapchainImageFormat,
            .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR
        })
		.set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
		.set_desired_extent(windowSettings.width, windowSettings.height)
		.add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT)
		.build()
		.value();

	m_swapchain.swapchainExtent = vkbSwapchain.extent;
	m_swapchain.swapchain = vkbSwapchain.swapchain;
	m_swapchain.swapchainImages = vkbSwapchain.get_images().value();
	m_swapchain.swapchainImageViews = vkbSwapchain.get_image_views().value();
}

void Renderer::destroySwapchain() {
	vkDestroySwapchainKHR(m_foundation.device, m_swapchain.swapchain, nullptr);

	for (int i = 0; i < m_swapchain.swapchainImageViews.size(); i++) {
		vkDestroyImageView(m_foundation.device, m_swapchain.swapchainImageViews[i], nullptr);
	}
}

}
