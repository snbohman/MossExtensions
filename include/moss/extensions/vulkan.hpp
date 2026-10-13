#pragma once

/*
 * Umbrella include for the Vulkan extension ("Core + Attachments").
 *
 * Deliberately NOT included here:
 *   - attachments/clear_attachment.hpp: a minimal example/test; include it
 *     explicitly when you want it.
 */

#include <moss/extensions/vulkan/alias.hpp>
#include <moss/extensions/vulkan/components.hpp>
#include <moss/extensions/vulkan/core/render_core.hpp>
#include <moss/extensions/vulkan/core/core.hpp>
#include <moss/extensions/vulkan/core/frame.hpp>
#include <moss/extensions/vulkan/attachments/images/components.hpp>
#include <moss/extensions/vulkan/attachments/images/gpu_resources.hpp>
#include <moss/extensions/vulkan/attachments/images/image_system.hpp>
#include <moss/extensions/vulkan/attachments/passes/components.hpp>
#include <moss/extensions/vulkan/attachments/passes/pass_system.hpp>
