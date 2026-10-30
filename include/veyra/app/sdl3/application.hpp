#ifndef VEYRA_APP_SDL3_APPLICATION_HPP
#define VEYRA_APP_SDL3_APPLICATION_HPP

#include "veyra/app/sdl3/window_host.hpp"
#include "veyra/app/window_loop.hpp"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>

namespace veyra::app::sdl3 {

class Sdl3Application {
  public:
    explicit Sdl3Application(ShutdownMode shutdown = ShutdownMode::on_last_window_close) noexcept :
        host_(shutdown) {
        host_.set_present_observer(&Sdl3Application::on_presented, this);
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

    WindowSetResult commit(
        WindowId window,
        std::span<const DisplayCommand> commands,
        std::uint32_t width,
        std::uint32_t height
    ) noexcept {
        const auto result = host_.commit(window, commands, width, height);
        if (result == WindowSetResult::ok) {
            scheduler_.request_immediate();
        }
        return result;
    }

    WindowSetResult present(WindowId window, std::span<SoftwarePixel> backing) noexcept {
        return host_.present(window, backing);
    }

    WindowSetResult present_async(WindowId window, std::span<SoftwarePixel> backing) noexcept {
        return host_.present_async(window, backing);
    }

    bool wait_presented() noexcept {
        return host_.wait_presented();
    }

    bool take_present_result(WindowId window, WindowSetResult& out) noexcept {
        return host_.take_present_result(window, out);
    }

    WindowSetResult recover_device(WindowId window) noexcept {
        return host_.recover_device(window);
    }

    bool surface_size(WindowId window, std::uint32_t& width, std::uint32_t& height) noexcept {
        return host_.surface_size(window, width, height);
    }

    NodeId activate(WindowId window) noexcept {
        const auto node = host_.activate(window);
        scheduler_.request_immediate();
        return node;
    }

    HitTestResult hit_test_and_hover(
        WindowId window,
        LayoutPoint point,
        std::span<const LayoutRect> bounds_by_node_index,
        NodeId& target
    ) noexcept {
        const auto result = host_.hit_test_and_hover(window, point, bounds_by_node_index, target);
        scheduler_.request_immediate();
        return result;
    }

    HitTestResult press_pointer(
        WindowId window,
        std::uint32_t pointer,
        LayoutPoint point,
        std::span<const LayoutRect> bounds_by_node_index,
        NodeId& target
    ) noexcept {
        const auto result =
            host_.press_pointer(window, pointer, point, bounds_by_node_index, target);
        scheduler_.request_immediate();
        return result;
    }

    void set_route_observer(RouteObserver observer, void* user) noexcept {
        host_.set_route_observer(observer, user);
    }

    bool bind_semantics(WindowId window, NodeTree& tree, NodeId root) noexcept {
        const auto result = host_.bind_semantics(window, tree, root);
        if (result) {
            scheduler_.request_immediate();
        }
        return result;
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
        config.wait_ms = &Sdl3Application::wait_for;
        config.wait_user = this;
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
    static void on_presented(void* user) noexcept {
        auto* app = static_cast<Sdl3Application*>(user);
        app->scheduler_.request_immediate();
        {
            std::lock_guard lock(app->wake_mutex_);
            ++app->wake_generation_;
        }
        app->wake_cv_.notify_one();
    }

    static void wait_for(void* user, unsigned ms) noexcept {
        auto* app = static_cast<Sdl3Application*>(user);
        std::unique_lock lock(app->wake_mutex_);
        app->wake_cv_.wait_for(lock, std::chrono::milliseconds(ms), [&] {
            return app->wake_generation_ != 0;
        });
        app->wake_generation_ = 0;
    }

    FrameScheduler scheduler_;
    std::mutex wake_mutex_;
    std::condition_variable wake_cv_;
    std::uint64_t wake_generation_{};
    Sdl3WindowHost host_;
};

}

namespace veyra::app {
using ::veyra::app::sdl3::Sdl3Application;
}

#endif
