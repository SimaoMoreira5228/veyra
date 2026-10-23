#include "veyra/platform/glfw_window.hpp"

#include <vulkan/vulkan.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

namespace veyra {

WindowResult GlfwWindow::create_vulkan_surface(void* instance, void*& surface) noexcept {
    surface = nullptr;

    if (creation_result_ != WindowResult::ready) {
        return creation_result_;
    }

    if (instance == nullptr) {
        return WindowResult::invalid_input;
    }

    if (glfwGetPlatform() == GLFW_PLATFORM_NULL) {
        return WindowResult::unsupported;
    }

    if (glfwVulkanSupported() != GLFW_TRUE) {
        return WindowResult::unsupported;
    }

    auto native = VkSurfaceKHR{VK_NULL_HANDLE};

    if (glfwCreateWindowSurface(
            static_cast<VkInstance>(instance),
            window_.get(),
            nullptr,
            &native
        ) != VK_SUCCESS) {
        return WindowResult::creation_failed;
    }

    surface = reinterpret_cast<void*>(native);

    return WindowResult::ready;
}

}
