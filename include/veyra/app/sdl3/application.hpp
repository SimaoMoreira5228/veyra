#ifndef VEYRA_APP_SDL3_APPLICATION_HPP
#define VEYRA_APP_SDL3_APPLICATION_HPP

#include "veyra/app/sdl3/window_host.hpp"
#include "veyra/app/window_loop.hpp"

#include <cstdint>

namespace veyra::app::sdl3 {

class Sdl3Application {
  public:
    explicit Sdl3Application(ShutdownMode shutdown = ShutdownMode::on_last_window_close) noexcept :
        host_(shutdown) {
        scheduler_.request_immediate();
    }

    Sdl3Application(const Sdl3Application&) = delete;
    Sdl3Application& operator=(const Sdl3Application&) = delete;

    WindowSetResult create(
        const char* title,
        int width,
        int height,
        const WindowDesc& desc,
        WindowId& out_window
    ) noexcept {
        const auto result = host_.create(title, width, height, desc, out_window);
        if (result == WindowSetResult::ok) {
            scheduler_.request_immediate();
        }
        return result;
    }

    WindowSetResult show(WindowId window) noexcept {
        const auto result = host_.show(window);
        if (result == WindowSetResult::ok) {
            scheduler_.request_immediate();
        }
        return result;
    }

    WindowSetResult request_close(WindowId window, CloseReason reason) noexcept {
        const auto result = host_.request_close(window, reason);
        scheduler_.request_immediate();
        return result;
    }

    WindowSetResult
    close_with_result(WindowId window, bool accepted, std::uint64_t value) noexcept {
        const auto result = host_.close_with_result(window, accepted, value);
        scheduler_.request_immediate();
        return result;
    }

    WindowSetResult destroy(WindowId window) noexcept {
        const auto result = host_.destroy(window);
        scheduler_.request_immediate();
        return result;
    }

    bool set_focused_node(WindowId window, NodeId node) noexcept {
        const auto result = host_.set_focused_node(window, node);
        if (result) {
            scheduler_.request_immediate();
        }
        return result;
    }

    bool set_accessibility_root(WindowId window, NodeId node) noexcept {
        return host_.set_accessibility_root(window, node);
    }

    bool poll_event(WindowEvent& event, WindowId& window) noexcept {
        return host_.poll_event(event, window);
    }

    template <typename OnEvent, typename OnFrame>
    LoopResult
    run(OnEvent on_event,
        OnFrame on_frame,
        void (*sleep_ms)(unsigned) noexcept,
        std::uint64_t frame_cap = 0) noexcept {
        auto config = WindowLoopConfig{
            host_,
            scheduler_,
            on_event,
            on_frame,
            sleep_ms,
        };
        config.frame_cap = frame_cap;
        return run_until_quit(config);
    }

    WindowSet& windows() noexcept {
        return host_.windows();
    }

    const WindowSet& windows() const noexcept {
        return host_.windows();
    }

    FrameScheduler& scheduler() noexcept {
        return scheduler_;
    }

    Sdl3WindowHost& host() noexcept {
        return host_;
    }

    bool should_shutdown() const noexcept {
        return host_.windows().should_shutdown();
    }

  private:
    Sdl3WindowHost host_;
    FrameScheduler scheduler_;
};

}

namespace veyra::app {
using ::veyra::app::sdl3::Sdl3Application;
}

#endif
