#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "veyra/platform/glfw_window.hpp"

#include <vulkan/vulkan.h>

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

bool manual_surface_run() {
    const char* flag = std::getenv("VEYRA_MANUAL_SURFACE");

    return flag != nullptr && flag[0] != '\0';
}

VkInstance
make_instance(const char* const* extensions, std::uint32_t extension_count, bool& created) {
    const auto app = VkApplicationInfo{
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .apiVersion = VK_API_VERSION_1_0,
    };
    const auto info = VkInstanceCreateInfo{
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &app,
        .enabledExtensionCount = extension_count,
        .ppEnabledExtensionNames = extensions,
    };

    auto instance = VkInstance{VK_NULL_HANDLE};
    created = vkCreateInstance(&info, nullptr, &instance) == VK_SUCCESS;

    return instance;
}

void check_real_platform_creates_surface(veyra::GlfwWindow& window) {
    std::uint32_t extension_count = 0;
    const char** names = glfwGetRequiredInstanceExtensions(&extension_count);
    require(names != nullptr && extension_count > 0, "glfw names its surface extensions");

    bool created = false;
    const auto instance = make_instance(names, extension_count, created);
    require(created, "a windowed instance creates");

    void* surface = nullptr;
    require(
        window.create_vulkan_surface(reinterpret_cast<void*>(instance), surface) ==
            veyra::WindowResult::ready,
        "the real platform hands out a surface"
    );
    require(surface != nullptr, "the surface handle is not null");
    require(
        window.capabilities().vulkan_surface,
        "the real platform advertises the surface it just proved"
    );

    vkDestroySurfaceKHR(instance, reinterpret_cast<VkSurfaceKHR>(surface), nullptr);
    vkDestroyInstance(instance, nullptr);
}

void check_null_platform_reports_unsupported(veyra::GlfwWindow& window) {
    bool created = false;
    const auto instance = make_instance(nullptr, 0, created);

    if (!created) {
        return;
    }

    void* surface = reinterpret_cast<void*>(0x1);
    require(
        window.create_vulkan_surface(reinterpret_cast<void*>(instance), surface) ==
            veyra::WindowResult::unsupported,
        "the null platform answers unsupported for a real instance"
    );
    require(surface == nullptr, "no surface leaks out of the unsupported path");
    require(
        window.capabilities().vulkan_surface == false,
        "the null platform advertises no surface"
    );

    vkDestroyInstance(instance, nullptr);
}

void check_invalid_input() {
    veyra::GlfwWindow bad(veyra::kInvalidWindowId, "bad", 320, 240, false);
    require(
        bad.creation_result() == veyra::WindowResult::invalid_input,
        "a reserved id fails construction"
    );

    auto token = 0;
    void* surface = nullptr;
    require(
        bad.create_vulkan_surface(&token, surface) == veyra::WindowResult::invalid_input,
        "a failed window replays its creation result"
    );
    require(surface == nullptr, "a failed window hands out nothing");
}

void check_null_instance_rejected(veyra::GlfwWindow& window) {
    void* surface = nullptr;
    require(
        window.create_vulkan_surface(nullptr, surface) == veyra::WindowResult::invalid_input,
        "a null instance is invalid input"
    );
    require(surface == nullptr, "a null instance yields no surface");
}

}

int main() {
    const bool manual = manual_surface_run();

    if (!manual) {
        if (glfwPlatformSupported(GLFW_PLATFORM_NULL) != GLFW_TRUE) {
            return 77;
        }
        glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_NULL);
    }

    veyra::GlfwWindow window(41, "Veyra surface", 320, 240, false);

    if (window.creation_result() != veyra::WindowResult::ready) {
        std::cerr << "GLFW could not create a window in this environment\n";
        return 77;
    }

    try {
        if (manual) {
            check_real_platform_creates_surface(window);
        } else {
            check_null_platform_reports_unsupported(window);
        }
        check_invalid_input();
        check_null_instance_rejected(window);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }

    return 0;
}
