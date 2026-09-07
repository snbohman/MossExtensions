#pragma once

#include <vulkan/vk_enum_string_helper.h>

#define VK_CHECK(x)                                                     \
    do {                                                                \
        VkResult err = x;                                               \
        if (err) {                                                      \
            M_ERROR("Detected Vulkan Error: {}", string_VkResult(err)); \
            abort();                                                    \
        }                                                               \
    } while (0)
