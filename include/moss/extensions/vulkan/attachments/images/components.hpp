#pragma once

#include "moss/extensions/vulkan/meta.hpp"

#include <string>
#include <utility>

namespace moss::extensions::vulkan {

/*
 * One SimImage entity creates one named image (two sides if it also carries
 * DoubleBuffered). Extent is the draw image extent times ImageScale, rounded
 * to nearest, minimum 1. Images are 2D, one mip, one layer, GPU-only, usage
 * storage + sampled + transfer src + transfer dst. They are zero-cleared once
 * on the first frame and then stay in GENERAL for their whole life, so sim
 * state persists across frames.
 *
 * Name rules: non-empty, unique, and not DRAW_IMAGE_NAME ("draw").
 */
struct SimImage : public Component {
    SimImage(std::string n, VkFormat f = VK_FORMAT_R16G16B16A16_SFLOAT)
        : name(std::move(n)), format(f) { }

    std::string name;
    VkFormat format;
};

// Optional on a SimImage entity. Default 1.0, must be greater than 0.
struct ImageScale : public Component {
    explicit ImageScale(float s) : scale(s) { }

    float scale;
};

/*
 * Optional tag on a SimImage entity. The image gets two sides. Reading gives
 * the current side, writing gives the other one, and the sides swap after each
 * pass iteration that writes the image.
 */
struct DoubleBuffered : public Component { };

/*
 * Optional, on at most one entity in the Context. Settings of the one shared
 * sampler that serves every sample binding.
 */
struct SamplerSettings : public Component {
    SamplerSettings(
        VkFilter f = VK_FILTER_LINEAR,
        VkSamplerAddressMode a = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
    ) : filter(f), address(a) { }

    VkFilter filter;
    VkSamplerAddressMode address;
};

}
