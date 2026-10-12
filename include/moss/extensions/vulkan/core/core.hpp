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
 *     mirror.connect<vk::Core, vk::FrameBegin, ...attachments..., vk::FrameEnd>();
 *
 * Needs one entity in the same Context carrying WindowSettings and
 * RendererSettings.
 */
class Core : public moss::System {
public:
    void build(const Key<key::WRITE>& key, const DynamicView& entities) override;
    void exit(const Key<key::WRITE>& key) override;

private:
    void initGlfw(RenderCore& core, WindowSettings windowSettings);
    void initVulkan(RenderCore& core, RendererSettings rendererSettings);
    void initSwapchain(RenderCore& core, WindowSettings windowSettings);
    void initCommands(RenderCore& core);
    void initSyncStructures(RenderCore& core);
};

}
