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
 * Example (a fluid-style setup, mechanics only; shader files are yours):
 *
 * mirror
 *     .connect<vk::Core, vk::FrameBegin, vk::ImageSystem, vk::PassSystem, vk::FrameEnd>()
 *     .create()
 *         .attach<vk::WindowSettings>("moss", 1280, 720)
 *         .attach<vk::RendererSettings>(true)
 *     .create().attach<vk::SimImage>("pressure").attach<vk::DoubleBuffered>()
 *     .create().attach<vk::SimImage>("divergence")
 *     .create()   // pressure solve, 20 iterations, the sides swap after each
 *         .attach<vk::ComputeShader>("shaders/jacobi.comp.spv")
 *         .attach<vk::Bind>(
 *             vk::sample(0, "pressure"), vk::sample(1, "divergence"), vk::write(2, "pressure")
 *         )
 *         .attach<vk::Dispatch>("pressure")
 *         .attach<vk::Repeat>(20u)
 *     .create()   // display
 *         .attach<vk::VertexShader>("shaders/fullscreen.vert.spv")
 *         .attach<vk::FragmentShader>("shaders/display.frag.spv")
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
struct WindowSettings : public Component {
    WindowSettings(const char* t, i32 w, i32 h, i32 fps = 60, bool r = false)
        : title(t), width(w), height(h), targetFPS(fps), resize(r) { }

    const char* title;
    u32 width;
    u32 height;
    u32 targetFPS;   // reserved, unused: there is no frame limiting yet
    bool resize;
};

/*
 * Core-wide overrides, as defaulted constructor parameters. Present mode and
 * swapchain format are DESIRED, not required: vk-bootstrap falls back (FIFO is
 * always available) and Core reads the real swapchain format back. The draw
 * format is checked: it must support storage image, colour attachment, sampled
 * image with linear filter and blit source, or Core aborts with a message.
 */
struct RendererSettings : public Component {
    RendererSettings(
        bool vl = true,
        VkPresentModeKHR pm = VK_PRESENT_MODE_FIFO_KHR,
        VkFormat draw = VK_FORMAT_R16G16B16A16_SFLOAT,
        VkFormat swapchain = VK_FORMAT_B8G8R8A8_UNORM
    ) : validationLayers(vl), presentMode(pm), drawFormat(draw), swapchainFormat(swapchain) { }

    bool validationLayers;
    VkPresentModeKHR presentMode;
    VkFormat drawFormat;
    VkFormat swapchainFormat;
};

/* ------------------------------------------------------------------------ */
/* ------------------------ Requirement components ------------------------ */
/* ------------------------------------------------------------------------ */

/*
 * Any entity in the Context may carry these; Core::build collects all of them
 * before creating the instance and device. Duplicate extension names are
 * de-duplicated and feature structs are OR-merged.
 *
 * The extension components take any number of names. The constraint keeps the
 * variadic constructor from hijacking copy construction (attach passes
 * lvalues, so a copy would otherwise match N&& with N = Self&).
 */
struct RequireInstanceExtension : public Component {
    template<std::convertible_to<std::string>... N>
    requires(sizeof...(N) > 0)
    explicit RequireInstanceExtension(N&&... ns)
        : names{ std::string(std::forward<N>(ns))... } { }

    std::vector<std::string> names;
};

struct RequireDeviceExtension : public Component {
    template<std::convertible_to<std::string>... N>
    requires(sizeof...(N) > 0)
    explicit RequireDeviceExtension(N&&... ns)
        : names{ std::string(std::forward<N>(ns))... } { }

    std::vector<std::string> names;
};

/*
 * Feature requirements. Pass a named struct (or a designated-initialiser
 * temporary), not a bare braced list: attach cannot forward those.
 * Vulkan 1.1/1.2/1.3 structs get sType forced and pNext cleared.
 */
struct RequireFeatures10 : public Component {
    explicit RequireFeatures10(VkPhysicalDeviceFeatures f) : features(f) { }
    VkPhysicalDeviceFeatures features;
};

struct RequireFeatures11 : public Component {
    explicit RequireFeatures11(VkPhysicalDeviceVulkan11Features f) : features(f) {
        features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
        features.pNext = nullptr;
    }
    VkPhysicalDeviceVulkan11Features features;
};

struct RequireFeatures12 : public Component {
    explicit RequireFeatures12(VkPhysicalDeviceVulkan12Features f) : features(f) {
        features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
        features.pNext = nullptr;
    }
    VkPhysicalDeviceVulkan12Features features;
};

struct RequireFeatures13 : public Component {
    explicit RequireFeatures13(VkPhysicalDeviceVulkan13Features f) : features(f) {
        features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
        features.pNext = nullptr;
    }
    VkPhysicalDeviceVulkan13Features features;
};

}
