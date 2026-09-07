#pragma once

#include <moss/moss.hpp>
#include <VkBootstrap.h>
#include <GLFW/glfw3.h>
#include <raylib.h>
#include <vulkan/vulkan_core.h>
#include <moss/extensions/vulkan/components.hpp>

namespace moss::extensions::vulkan::utils {

inline GLFWwindow* createWindow(Window winComp) {
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    if (!winComp.resize) glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    return glfwCreateWindow(winComp.width, winComp.height, winComp.title, NULL, NULL);
}

inline void destroyWindow(GLFWwindow* window) {
    glfwDestroyWindow(window);
    glfwTerminate();
}

inline VkSurfaceKHR createSurface(VkInstance instance, GLFWwindow* window, VkAllocationCallbacks* allocator = nullptr) {
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (glfwCreateWindowSurface(instance, window, nullptr, &surface)) {
        const char* error_msg;
        int ret = glfwGetError(&error_msg);
        if (ret != 0) {
            if (error_msg != nullptr) M_ERROR("{}:{} ", ret, error_msg);
            else M_ERROR("{}", ret);
        }
        surface = VK_NULL_HANDLE;
    }

    return surface;
}

};
