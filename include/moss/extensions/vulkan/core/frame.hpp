#pragma once

#include "moss/extensions/vulkan/meta.hpp"
#include "moss/extensions/vulkan/utils.hpp"
#include "moss/extensions/vulkan/core/render_core.hpp"

namespace moss::extensions::vulkan {

/*
 * First half of the old Renderer::draw(): poll events, wait while the window
 * is minimised, rebuild the swapchain when the window was resized (only when
 * WindowSettings::resize is true), wait for the frame's fence, acquire a
 * swapchain image, begin the command buffer, and put the draw image into
 * GENERAL. Fills core.frame for the attachments.
 *
 * Connect it right after Core and before every attachment:
 *
 *     mirror.connect<
 *         vk::Core, vk::FrameBegin, vk::ImageSystem, vk::PassSystem,
 *         ...your attachments..., vk::FrameEnd
 *     >();
 */
class FrameBegin : public moss::System {
public:
    void tick(const Key<key::READ>& key) override;
};

/*
 * Second half of the old Renderer::draw(): draw image -> blit into the
 * swapchain image -> present, then advance the frame number. A present that
 * reports OUT_OF_DATE or SUBOPTIMAL on a resizable window marks the swapchain
 * for rebuilding at the next FrameBegin.
 *
 * Connect it LAST, after every attachment. It expects the draw image to be in
 * GENERAL.
 */
class FrameEnd : public moss::System {
public:
    void tick(const Key<key::READ>& key) override;
};

}
