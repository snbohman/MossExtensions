#pragma once

#include <moss/meta/logs.hpp>
#include <vulkan/vk_enum_string_helper.h>

#include <cstdlib>

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
 * the surface perfectly), so it must not abort. OUT_OF_DATE means the surface
 * changed under us; resize is unsupported, so say so and abort. Anything else
 * is fatal as usual.
 */
#define VK_CHECK_SWAPCHAIN(x)                                                       \
    do {                                                                            \
        VkResult swapResult = x;                                                    \
        if (swapResult == VK_ERROR_OUT_OF_DATE_KHR) {                               \
            M_ERRORF("Swapchain is out of date and swapchain resize is unsupported"); \
            M_ERROR("Detected Vulkan Error: {}", string_VkResult(swapResult));      \
            std::abort();                                                           \
        }                                                                           \
        if (swapResult != VK_SUCCESS && swapResult != VK_SUBOPTIMAL_KHR) {          \
            M_ERROR("Detected Vulkan Error: {}", string_VkResult(swapResult));      \
            std::abort();                                                           \
        }                                                                           \
    } while (0)

/*
 * Error policy of the extension: log, then abort. No throw.
 * The format string must be a compile-time literal with {} placeholders; use
 * M_FATALF when there are no arguments.
 */
#define M_FATAL(message, ...)                   \
    do {                                        \
        M_ERROR(message, ##__VA_ARGS__);        \
        std::abort();                           \
    } while (0)

#define M_FATALF(message)                       \
    do {                                        \
        M_ERRORF(message);                      \
        std::abort();                           \
    } while (0)
