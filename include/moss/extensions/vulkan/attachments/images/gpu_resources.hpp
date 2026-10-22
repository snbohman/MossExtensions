#pragma once

#include "moss/extensions/vulkan/meta.hpp"
#include "moss/extensions/vulkan/core/allocated_image.hpp"
#include "moss/extensions/vulkan/core/allocated_buffer.hpp"

#include <string>
#include <unordered_map>

namespace moss::extensions::vulkan {

/*
 * Shared GPU services, stored as a moss resource next to RenderCore:
 *
 *     auto& gpu = cmd::Resource<GpuResources>::init(key).get();
 *
 * Created by ImageSystem::build, erased by ImageSystem::exit. Plain data with
 * public fields on purpose: any attachment (passes, particles, post-processing,
 * ImGui) can bind the same images and buffers. It holds images, buffers and the
 * sampler only; the descriptor allocator stays private to each attachment that
 * needs one. Image and buffer names share one namespace.
 *
 * Do not capture a reference in a deletion-queue lambda, and do not keep one
 * across frames: re-fetch it in tick.
 */
struct GpuResources {
    struct Image {
        AllocatedImage sides[2] = {};   // sides[1] is only valid when doubleBuffered
        bool doubleBuffered = false;
        u32 readSide = 0;               // current read side; the write side is 1 - readSide
    };

    struct Buffer {
        AllocatedBuffer sides[2] = {};  // sides[1] is only valid when doubleBuffered
        bool doubleBuffered = false;
        u32 readSide = 0;               // current read side; the write side is 1 - readSide
    };

    std::unordered_map<std::string, Image> images;     // keyed by SimImage name
    std::unordered_map<std::string, Buffer> buffers;   // keyed by SimBuffer name
    VkSampler sampler = VK_NULL_HANDLE;                // shared by every sample binding
};

}
