#include "veyra/app/sdl3/application.hpp"

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

struct TeardownLog {
    std::array<veyra::app::sdl3::TeardownStage, 3> stages{};
    int count{};
};

void check_teardown_order() {
    auto log = TeardownLog{};
    {
        check_teardown_order();
        auto app = veyra::app::sdl3::Sdl3Application{};
        app.host().set_teardown_observer(
            [](void* user, veyra::app::sdl3::TeardownStage stage) noexcept {
                auto* value = static_cast<TeardownLog*>(user);
                if (value->count < static_cast<int>(value->stages.size())) {
                    value->stages[static_cast<std::size_t>(value->count)] = stage;
                }
                ++value->count;
            },
            &log
        );

        auto first = veyra::WindowId{};
        auto second = veyra::WindowId{};
        require(
            app.create("teardown first", 160, 120, {}, first) == veyra::WindowSetResult::ok,
            "teardown first window creates"
        );
        require(
            app.create("teardown second", 120, 90, {.owner = first}, second) ==
                veyra::WindowSetResult::ok,
            "teardown second window creates"
        );
        require(app.show(first) == veyra::WindowSetResult::ok, "teardown first shows");
        require(app.show(second) == veyra::WindowSetResult::ok, "teardown second shows");
    }

    require(log.count == 3, "teardown reports every stage once");
    require(log.stages[0] == veyra::app::sdl3::TeardownStage::decoders, "decoders stop first");
    require(
        log.stages[1] == veyra::app::sdl3::TeardownStage::renderers,
        "render workers stop second"
    );
    require(
        log.stages[2] == veyra::app::sdl3::TeardownStage::windows,
        "window resources release last"
    );

    auto app = veyra::app::sdl3::Sdl3Application{};
    auto window = veyra::WindowId{};
    require(
        app.create("teardown restart", 160, 120, {}, window) == veyra::WindowSetResult::ok,
        "a new application creates a window after teardown"
    );
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
