#pragma once

#include <moss/extensions/vulkan/meta.hpp>
#include <vulkan/vulkan_core.h>

#include <concepts>
#include <string>
#include <utility>
#include <vector>

/*
 * Core settings and requirement components. Image and pass components live in
 * attachments/images/components.hpp and attachments/passes/components.hpp.
 *
 * Components are plain structs with no base class. Plain data is an aggregate
 * and is attached with designated initialisers (fields in declaration order,
 * omitted ones keep their default) or positionally:
 *
 *     .attach<vk::WindowSettings>({.title = "moss", .resize = true})
 *     .attach<vk::RendererSettings>()
 *
 * Example (a fluid-style setup, mechanics only; shader files are yours):
 *
 * mirror
 *     .connect<vk::Core, vk::FrameBegin, vk::ImageSystem, vk::PassSystem, vk::FrameEnd>()
 *     .create()
 *         .attach<vk::WindowSettings>({.title = "moss", .resize = true})
 *         .attach<vk::RendererSettings>()
 *     .create().attach<vk::SimImage>({.name = "pressure"}).attach<vk::DoubleBuffered>()
 *     .create().attach<vk::SimImage>({.name = "divergence"})
 *     .create()   // pressure solve, 20 iterations, the sides swap after each
 *         .attach<vk::ComputeShader>({.path = "shaders/jacobi.comp.spv"})
 *         .attach<vk::Bind>(
 *             vk::sample(0, "pressure"), vk::sample(1, "divergence"), vk::write(2, "pressure")
 *         )
 *         .attach<vk::Dispatch>("pressure")
 *         .attach<vk::Repeat>({.count = 20})
 *     .create()   // display
 *         .attach<vk::VertexShader>({.path = "shaders/fullscreen.vert.spv"})
 *         .attach<vk::FragmentShader>({.path = "shaders/display.frag.spv"})
 *         .attach<vk::Bind>(vk::sample(0, "pressure"));
 */

namespace moss::extensions::vulkan {

/* ------------------------------------------------------------------------ */
/* ---------------------------- Core settings ----------------------------- */
/* ------------------------------------------------------------------------ */

/*
 * Exactly one entity in the Context carries both WindowSettings and
 * RendererSettings (Core warns when there are more and uses the first).
 */
struct WindowSettings {
    const char* title = "moss";
    u32 width = 1280;
    u32 height = 720;
    u32 targetFPS = 60;   // reserved, unused: there is no frame limiting yet
    bool resize = false;
};

/*
 * Core-wide overrides. Present mode and swapchain format are DESIRED, not
 * required: vk-bootstrap falls back (FIFO is always available) and Core reads
 * the real swapchain format back. The draw format is checked: it must support
 * storage image, colour attachment, sampled image with linear filter and blit
 * source, or Core aborts with a message.
 */
struct RendererSettings {
    bool validationLayers                          = true;
    VkPresentModeKHR presentMode                   = VK_PRESENT_MODE_FIFO_KHR;
    VkFormat drawFormat                            = VK_FORMAT_R16G16B16A16_SFLOAT;
    VkFormat swapchainFormat                       = VK_FORMAT_B8G8R8A8_UNORM;
    std::optional<std::array<float, 4>> clearColor = std::array<float, 4>{
        0.0f, 0.0f, 0.0f, 1.0f
    };
};

/* ------------------------------------------------------------------------ */
/* ------------------------ Requirement components ------------------------ */
/* ------------------------------------------------------------------------ */

/*
 * Any entity in the Context may carry these; Core::build collects all of them
 * before creating the instance and device. Duplicate extension names are
 * de-duplicated and feature structs are OR-merged.
 *
 * These keep constructors (variadic names, forced sType), so they are attached
 * with arguments, not a braced list. The extension components take any number
 * of names. The constraint keeps the variadic constructor from hijacking copy
 * construction (a copy would otherwise match N&& with N = Self&).
 */
struct RequireInstanceExtension {
    template<std::convertible_to<std::string>... N>
    requires(sizeof...(N) > 0)
    explicit RequireInstanceExtension(N&&... ns)
        : names{ std::string(std::forward<N>(ns))... } { }

    std::vector<std::string> names;
};

struct RequireDeviceExtension {
    template<std::convertible_to<std::string>... N>
    requires(sizeof...(N) > 0)
    explicit RequireDeviceExtension(N&&... ns)
        : names{ std::string(std::forward<N>(ns))... } { }

    std::vector<std::string> names;
};

/*
 * Feature requirements. Pass a named struct (or a designated-initialiser
 * temporary), not a bare braced list.
 * Vulkan 1.1/1.2/1.3 structs get sType forced and pNext cleared.
 */
struct RequireFeatures10 {
    explicit RequireFeatures10(VkPhysicalDeviceFeatures f) : features(f) { }
    VkPhysicalDeviceFeatures features;
};

struct RequireFeatures11 {
    explicit RequireFeatures11(VkPhysicalDeviceVulkan11Features f) : features(f) {
        features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
        features.pNext = nullptr;
    }
    VkPhysicalDeviceVulkan11Features features;
};

struct RequireFeatures12 {
    explicit RequireFeatures12(VkPhysicalDeviceVulkan12Features f) : features(f) {
        features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
        features.pNext = nullptr;
    }
    VkPhysicalDeviceVulkan12Features features;
};

struct RequireFeatures13 {
    explicit RequireFeatures13(VkPhysicalDeviceVulkan13Features f) : features(f) {
        features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
        features.pNext = nullptr;
    }
    VkPhysicalDeviceVulkan13Features features;
};

}
