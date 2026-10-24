#pragma once

#include <moss/meta/logs.hpp>
#include <vulkan/vk_enum_string_helper.h>

#include <cstdlib>

/*
 * M_FATAL / M_FATALF (log, then abort) come from moss/meta/logs.hpp: the error
 * policy of the extension is the policy of moss. No throw.
 */

/*
 * Aborts on any nonzero VkResult. Use for everything that is not acquire or
 * present.
 */
#define VK_CHECK(x)                                                     \
    do {                                                                \
        VkResult err = x;                                               \
        if (err) {                                                      \
            M_ERROR("Detected Vulkan Error: {}", string_VkResult(err)); \
            abort();                                                    \
        }                                                               \
    } while (0)

/*
 * For vkAcquireNextImageKHR and vkQueuePresentKHR only. VK_SUBOPTIMAL_KHR is
 * a success code there (the swapchain still works, it just no longer matches
 * the surface perfectly), so it must not abort. OUT_OF_DATE on a window that
 * is not resizable cannot be recovered from, so say so and abort. (Resizable
 * windows handle it in FrameBegin/FrameEnd before this is reached.) Anything
 * else is fatal as usual.
 */
#define VK_CHECK_SWAPCHAIN(x)                                                       \
    do {                                                                            \
        VkResult swapResult = x;                                                    \
        if (swapResult == VK_ERROR_OUT_OF_DATE_KHR) {                               \
            M_ERRORF("Swapchain is out of date and the window is not resizable");   \
            M_ERROR("Detected Vulkan Error: {}", string_VkResult(swapResult));      \
            std::abort();                                                           \
        }                                                                           \
        if (swapResult != VK_SUCCESS && swapResult != VK_SUBOPTIMAL_KHR) {          \
            M_ERROR("Detected Vulkan Error: {}", string_VkResult(swapResult));      \
            std::abort();                                                           \
        }                                                                           \
    } while (0)
