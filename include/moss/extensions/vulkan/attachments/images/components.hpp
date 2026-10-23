#pragma once

#include "moss/extensions/vulkan/meta.hpp"

#include <string>
#include <utility>

namespace moss::extensions::vulkan {

/*
 * One SimImage entity creates one named image (two sides if it also carries
 * DoubleBuffered). Extent: ImageExtent if present, else the initial draw image
 * extent times ImageScale (rounded to nearest, minimum 1), else the initial
 * draw image extent. Images are 2D, one mip, one layer, GPU-only, usage
 * storage + sampled + transfer src + transfer dst. They are zero-cleared once
 * on the first frame and then stay in GENERAL for their whole life, so sim
 * state persists across frames. They are fixed resolution: they do NOT follow
 * the window when it is resized.
 *
 * Name rules: non-empty, unique across SimImage AND SimBuffer, and not
 * DRAW_IMAGE_NAME ("draw").
 */
struct SimImage {
    std::string name;
    VkFormat format = VK_FORMAT_R16G16B16A16_SFLOAT;
};

/*
 * Optional on a SimImage entity. Relative to the INITIAL draw extent (the one
 * at build time). Must be greater than 0. Not combinable with ImageExtent.
 */

struct ImageScale {
    float scale = 1.0f;
};

/*
 * Optional on a SimImage entity. Explicit size in pixels, both at least 1 and
 * at most the device's maxImageDimension2D. Not combinable with ImageScale.
 * Graphics passes render into the window-sized draw image, so a fixed image can
 * have a different aspect ratio: handling that is the shader's job.
 */
struct ImageExtent {
    u32 width = 0;
    u32 height = 0;
};

/*
 * One SimBuffer entity creates one named buffer of `size` bytes (two sides if
 * it also carries DoubleBuffered). GPU-only, usage uniform + storage + transfer
 * src + transfer dst. It is zero-filled once on the first frame (vkCmdFillBuffer)
 * and then keeps its content across frames. There is no initial data upload yet:
 * seed it with a Once pass.
 *
 * Size rules: greater than 0 and a multiple of 4. A buffer bound as a uniform
 * buffer must also fit maxUniformBufferRange, one bound as a storage buffer
 * maxStorageBufferRange (checked per binding). Name rules as for SimImage.
 */
struct SimBuffer {
    std::string name;
    u64 size = 0;
};

/*
 * Optional tag on a SimImage or SimBuffer entity. The resource gets two sides.
 * Reading gives the current side, writing gives the other one, and the sides
 * swap after each pass iteration that writes the resource.
 */
struct DoubleBuffered { char tag = 0; };

/*
 * Optional, on at most one entity in the Context. Settings of the one shared
 * sampler that serves every sample binding.
 */
struct SamplerSettings {
    VkFilter filter = VK_FILTER_LINEAR;
    VkSamplerAddressMode address = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
};

}
