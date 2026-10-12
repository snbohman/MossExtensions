#pragma once

#include "moss/extensions/vulkan/core/render_core.hpp"

#include <cmath>

namespace moss::extensions::vulkan {

/*
 * THROWAWAY test attachment (the old drawBackground, as a System).
 * Proves an attachment needs no edits to Core, FrameBegin or FrameEnd.
 *
 * Contract it follows:
 *   - connected between FrameBegin and FrameEnd
 *   - fetches the core as a resource every tick
 *   - starts recording with utils::syncAll
 *   - leaves the draw image in GENERAL
 */
class ClearAttachment : public moss::System {
public:
    void tick(const Key<key::READ>& key) override {
        RenderCore& core = cmd::Resource<RenderCore>::init(key).get();
        utils::syncAll(core.frame.cmd);

        float flash = std::abs(std::sin(core.frame.frameNumber / 120.f));
        VkClearColorValue color = { { 0.0f, 0.0f, flash, 1.0f } };
        VkImageSubresourceRange range = utils::mask::subresourceRange(VK_IMAGE_ASPECT_COLOR_BIT);

        vkCmdClearColorImage(
            core.frame.cmd, core.drawImage.image,
            VK_IMAGE_LAYOUT_GENERAL, &color, 1, &range
        );
    }
};

}
