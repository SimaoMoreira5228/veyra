#include "veyra/app/sdl3/application.hpp"

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

}

int main() {
    try {
        auto app = veyra::app::sdl3::Sdl3Application{};
        auto main_window = veyra::WindowId{};
        auto side_window = veyra::WindowId{};
        require(
            app.create("main", 160, 120, {}, main_window) == veyra::WindowSetResult::ok,
            "main window creates"
        );
        require(
            app.create("side", 120, 90, {.owner = main_window}, side_window) ==
                veyra::WindowSetResult::ok,
            "side window creates"
        );
        require(app.show(main_window) == veyra::WindowSetResult::ok, "main shows");
        require(app.show(side_window) == veyra::WindowSetResult::ok, "side shows");

        auto main_frames = std::uint32_t{};
        auto side_frames = std::uint32_t{};
        auto on_event = [](veyra::WindowId, veyra::WindowEvent&) noexcept {
            return false;
        };
        auto on_frame = [&](veyra::WindowId window, bool) noexcept -> veyra::FrameAction {
            if (window == main_window) {
                ++main_frames;
            } else if (window == side_window) {
                ++side_frames;
            }
            app.scheduler().request_immediate();
            return {.rendered = true};
        };

        const auto result = app.run(on_event, on_frame, nullptr, 2);
        require(result == veyra::LoopResult::capped, "frame cap stops the loop");
        require(main_frames == 2 && side_frames == 2, "both windows receive frames");

        require(
            app.request_close(main_window, veyra::CloseReason::application) ==
                veyra::WindowSetResult::ok,
            "main closes"
        );
        require(app.should_shutdown(), "application shuts down with the last window");
        require(app.host().backend(main_window) == nullptr, "main backend is released");
        require(app.host().backend(side_window) == nullptr, "side backend is released");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }

    return 0;
}
