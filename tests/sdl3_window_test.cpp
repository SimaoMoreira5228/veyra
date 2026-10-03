#include "veyra/platform/sdl3_window.hpp"

#include <array>
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

void check_invalid_construction() {
    const veyra::Sdl3Window window(nullptr, 320, 240);
    require(
        window.creation_result() == veyra::SdlWindowResult::invalid_input,
        "window creation rejects a null title"
    );
    require(window.native_window() == nullptr, "invalid construction owns no window");
}

void check_software_frame_lifecycle() {
    veyra::Sdl3Window window("Veyra test", 320, 240);
    require(
        window.creation_result() == veyra::SdlWindowResult::ready,
        "SDL creates a window and renderer"
    );
    SDL_HideWindow(window.native_window());

    veyra::SoftwareSurface surface;
    require(
        window.acquire_surface(surface) == veyra::SdlWindowResult::ready,
        "window provides a software surface"
    );
    require(
        surface.width == 320 && surface.height == 240 && surface.stride >= surface.width &&
            surface.pixels.size() >= surface.stride * surface.height,
        "software surface matches the renderer output"
    );

    const auto* acquired_pixels = surface.pixels.data();
    require(
        window.acquire_surface(surface) == veyra::SdlWindowResult::invalid_state,
        "a second acquisition cannot lock the same frame"
    );
    require(
        surface.pixels.data() == acquired_pixels,
        "failed acquisition keeps the active surface"
    );

    const std::array commands{
        veyra::DisplayCommand{veyra::FillRect{
            .bounds = {.x = 0.0F, .y = 0.0F, .width = 320.0F, .height = 240.0F},
            .color = {.red = 20, .green = 40, .blue = 60, .alpha = 255},
        }},
    };
    require(
        veyra::render_software(commands, surface, {}) == veyra::SoftwareRenderResult::complete,
        "software renderer draws into the window surface"
    );
    require(
        window.present() == veyra::SdlWindowResult::ready,
        "software surface presents through SDL"
    );
    require(
        window.present() == veyra::SdlWindowResult::invalid_state,
        "a surface must be acquired before each present"
    );

    const SDL_Rect input_area{.x = 8, .y = 12, .w = 80, .h = 24};
    require(
        window.set_text_input_area(input_area, 16) == veyra::SdlWindowResult::ready,
        "text input area can be placed relative to the window"
    );
    require(
        window.start_text_input() == veyra::SdlWindowResult::ready,
        "window enables text and composition events"
    );
    require(
        window.stop_text_input() == veyra::SdlWindowResult::ready,
        "window stops text and composition events"
    );
    require(
        window.clear_text_input_area() == veyra::SdlWindowResult::ready,
        "text input area can be cleared"
    );
    const SDL_Rect invalid_input_area{.x = 0, .y = 0, .w = -1, .h = 20};
    require(
        window.set_text_input_area(invalid_input_area, 0) == veyra::SdlWindowResult::invalid_input,
        "invalid text input geometry is rejected"
    );

    SDL_Event event{};
    require(SDL_SetWindowSize(window.native_window(), 160, 120), "test can resize the window");
    while (window.poll_event(event)) {
    }
    veyra::SoftwareSurface resized_surface;
    require(
        window.acquire_surface(resized_surface) == veyra::SdlWindowResult::ready,
        "resized window provides a new software surface"
    );
    require(
        resized_surface.width == 160 && resized_surface.height == 120,
        "software surface follows the resized renderer output"
    );
    require(
        veyra::render_software(commands, resized_surface, {}) ==
            veyra::SoftwareRenderResult::complete,
        "software renderer draws at the resized dimensions"
    );
    require(
        window.present() == veyra::SdlWindowResult::ready,
        "resized software surface presents through SDL"
    );

    SDL_Event expected_event{};
    expected_event.type = SDL_EVENT_QUIT;
    require(SDL_PushEvent(&expected_event), "test can queue an SDL event");

    auto found_quit = false;
    while (window.poll_event(event)) {
        found_quit = found_quit || event.type == SDL_EVENT_QUIT;
    }
    require(found_quit, "window exposes SDL events to the application");
}

}

int main() {
    try {
        check_invalid_construction();
        check_software_frame_lifecycle();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
