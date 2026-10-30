#include "veyra/app/sdl3/window_host.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

#if defined(VEYRA_APP_WITH_SDL_GPU)
void check_gpu_fallback() {
    auto host = veyra::app::sdl3::Sdl3WindowHost{};
    host.set_present_path(veyra::PresentPath::gpu);

    auto window = veyra::WindowId{};
    require(
        host.create("gpu fallback", 160, 120, {}, window) == veyra::WindowSetResult::ok,
        "gpu fallback window creates"
    );
    require(host.show(window) == veyra::WindowSetResult::ok, "gpu fallback window shows");

    auto width = std::uint32_t{};
    auto height = std::uint32_t{};
    require(host.surface_size(window, width, height), "gpu fallback size is available");

    auto backing = std::array<veyra::SoftwarePixel, std::size_t{160} * 120>{};
    auto display = std::array<veyra::DisplayCommand, 1>{veyra::DisplayCommand{veyra::FillRect{
        .bounds = {.width = 160.0F, .height = 120.0F},
        .color = {.red = 90, .green = 40, .blue = 20},
    }}};
    require(
        host.commit(window, display, width, height) == veyra::WindowSetResult::ok,
        "gpu fallback frame commits"
    );
    require(
        host.present(window, backing) == veyra::WindowSetResult::ok,
        "gpu fallback frame presents"
    );
    require(
        host.gpu_active(window) || host.gpu_fallback(),
        "gpu presentation either runs or reports fallback"
    );
    if (!host.gpu_active(window)) {
        require(host.gpu_fallback(), "unavailable gpu presentation falls back to software");
    }
}
#endif

}

int main() {
#if !defined(VEYRA_APP_WITH_SDL_GPU)
    return 77;
#else
    try {
        check_gpu_fallback();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
#endif
}
