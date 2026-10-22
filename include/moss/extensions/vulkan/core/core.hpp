#pragma once

#include "moss/extensions/vulkan/meta.hpp"
#include "moss/extensions/vulkan/components.hpp"
#include "moss/extensions/vulkan/utils.hpp"
#include "moss/extensions/vulkan/core/render_core.hpp"

namespace moss::extensions::vulkan {

/*
 * Builds the RenderCore (window, instance, device, swapchain, draw image,
 * frames in flight, sync objects), publishes it as a moss resource, and tears
 * it down in exit(). Connect it FIRST in a Context:
 *
 *     mirror.connect<
 *         vk::Core, vk::FrameBegin, vk::ImageSystem, vk::PassSystem,
 *         ...your attachments..., vk::FrameEnd
 *     >();
 *
 * Needs one entity in the same Context carrying WindowSettings and
 * RendererSettings. Entities carrying RequireInstanceExtension,
 * RequireDeviceExtension or RequireFeatures10/11/12/13 are collected before
 * the instance and device are created.
 */
class Core : public moss::System {
public:
    void build(const Key<key::WRITE>& key, const DynamicView& entities) override;
    void exit(const Key<key::WRITE>& key) override;

private:
    // De-duplicated extensions and OR-merged features (defined in core.cpp)
    struct Requirements;

    static Requirements collectRequirements(const Key<key::WRITE>& key, const DynamicView& entities);
    static void diagnoseDevices(VkInstance instance, const Requirements& requirements);

    void initGlfw(RenderCore& core, WindowSettings windowSettings);
    void initVulkan(RenderCore& core, RendererSettings rendererSettings, const Requirements& requirements);
    void initSwapchain(RenderCore& core, WindowSettings windowSettings, RendererSettings rendererSettings);
    void initCommands(RenderCore& core);
    void initSyncStructures(RenderCore& core);
};

/*
 * Rebuilds everything with swapchain lifetime for a new framebuffer size: waits
 * for the device to go idle, flushes core.swapchainDqueue, builds the new
 * swapchain with the old one as oldSwapchain, creates the draw image and the
 * render semaphores again, destroys the old swapchain and bumps
 * core.swapchainGeneration. Called by FrameBegin only, before it begins the
 * command buffer. The draw image content is lost.
 */
void recreateSwapchain(RenderCore& core, VkExtent2D desired);

}
