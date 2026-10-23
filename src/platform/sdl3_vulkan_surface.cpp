#include "veyra/platform/sdl3_window.hpp"

#include <SDL3/SDL_vulkan.h>

#include <vulkan/vulkan.h>

namespace veyra {

WindowResult Sdl3Window::create_vulkan_surface(void* instance, void*& surface) noexcept {
    surface = nullptr;

    if (creation_result_ != WindowResult::ready) {
        return creation_result_;
    }

    if (instance == nullptr) {
        return WindowResult::invalid_input;
    }

    auto native = VkSurfaceKHR{VK_NULL_HANDLE};

    // NOTE: a false return means this video driver cannot surface this window at
    // all, so it reports unsupported rather than a transient creation failure.
    if (!SDL_Vulkan_CreateSurface(
            window_.get(),
            static_cast<VkInstance>(instance),
            nullptr,
            &native
        )) {
        return WindowResult::unsupported;
    }

    surface = reinterpret_cast<void*>(native);

    return WindowResult::ready;
}

}
