#include "veyra/platform/sdl3_window.hpp"

#include <SDL3/SDL_vulkan.h>

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

void check_real_driver_creates_surface(veyra::Sdl3Window& window) {
    std::uint32_t extension_count = 0;
    const char* const* names = SDL_Vulkan_GetInstanceExtensions(&extension_count);
    require(names != nullptr && extension_count > 0, "sdl names its surface extensions");

    bool created = false;
    const auto instance = make_instance(names, extension_count, created);
    require(created, "a windowed instance creates");

    void* surface = nullptr;
    require(
        window.create_vulkan_surface(reinterpret_cast<void*>(instance), surface) ==
            veyra::WindowResult::ready,
        "the real driver hands out a surface"
    );
    require(surface != nullptr, "the surface handle is not null");
    require(
        window.capabilities().vulkan_surface,
        "the real driver advertises the surface it just proved"
    );

    SDL_Vulkan_DestroySurface(instance, reinterpret_cast<VkSurfaceKHR>(surface), nullptr);
    vkDestroyInstance(instance, nullptr);
}

void check_dummy_driver_reports_unsupported(veyra::Sdl3Window& window) {
    bool created = false;
    const auto instance = make_instance(nullptr, 0, created);

    if (!created) {
        return;
    }

    void* surface = reinterpret_cast<void*>(0x1);
    require(
        window.create_vulkan_surface(reinterpret_cast<void*>(instance), surface) ==
            veyra::WindowResult::unsupported,
        "the dummy driver answers unsupported for a real instance"
    );
    require(surface == nullptr, "no surface leaks out of the unsupported path");

    vkDestroyInstance(instance, nullptr);
}

void check_invalid_input() {
    veyra::Sdl3Window bad(nullptr, 320, 240);
    require(
        bad.creation_result() == veyra::WindowResult::invalid_input,
        "a null title fails construction"
    );

    auto token = 0;
    void* surface = nullptr;
    require(
        bad.create_vulkan_surface(&token, surface) == veyra::WindowResult::invalid_input,
        "a failed window replays its creation result"
    );
    require(surface == nullptr, "a failed window hands out nothing");
    require(bad.capabilities().vulkan_surface == false, "a failed window promises no surface");
}

void check_null_instance_rejected(veyra::Sdl3Window& window) {
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

    veyra::Sdl3Window window("Veyra surface", 320, 240);

    if (window.creation_result() != veyra::WindowResult::ready) {
        std::cerr << "SDL could not create a window in this environment\n";
        return 77;
    }

    if (!manual) {
        const char* driver = SDL_GetCurrentVideoDriver();
        std::string name = driver != nullptr ? driver : "unknown";

        if (name != "dummy") {
            std::cerr << "expected the dummy video driver, saw " << name << '\n';
            return 1;
        }
    }

    try {
        if (manual) {
            check_real_driver_creates_surface(window);
        } else {
            check_dummy_driver_reports_unsupported(window);
        }
        check_invalid_input();
        check_null_instance_rejected(window);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }

    return 0;
}
