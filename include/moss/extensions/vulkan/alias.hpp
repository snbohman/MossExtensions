#pragma once

/*
 * Standalone on purpose: it declares the namespace before aliasing it, so it
 * can be included without pulling in any Vulkan or moss header.
 */
namespace moss::extensions::vulkan {}

namespace moss {

namespace vk = extensions::vulkan;
// namespace ig = extensions::imgui;

}
