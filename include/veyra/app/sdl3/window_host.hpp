#ifndef VEYRA_APP_SDL3_WINDOW_HOST_HPP
#define VEYRA_APP_SDL3_WINDOW_HOST_HPP

#include "veyra/app/window_set.hpp"
#include "veyra/platform/core/window_router.hpp"
#include "veyra/platform/sdl3/sdl3_window.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace veyra::app::sdl3 {

class Sdl3WindowHost {
  public:
    static constexpr std::size_t kCapacity = 8;

    explicit Sdl3WindowHost(ShutdownMode shutdown = ShutdownMode::on_last_window_close) noexcept :
        windows_{slots_, shutdown} {
    }

    Sdl3WindowHost(const Sdl3WindowHost&) = delete;
    Sdl3WindowHost& operator=(const Sdl3WindowHost&) = delete;
    Sdl3WindowHost(Sdl3WindowHost&&) = delete;
    Sdl3WindowHost& operator=(Sdl3WindowHost&&) = delete;

    WindowSetResult create(
        const char* title,
        int width,
        int height,
        const WindowDesc& desc,
        WindowId& out_window
    ) noexcept;
    WindowSetResult show(WindowId window) noexcept;
    WindowSetResult request_close(WindowId window, CloseReason reason) noexcept;
    WindowSetResult close_with_result(WindowId window, bool accepted, std::uint64_t value) noexcept;
    WindowSetResult destroy(WindowId window) noexcept;
    WindowSetResult set_state(WindowId window, WindowState state) noexcept;
    bool bind_semantics(WindowId window, NodeTree& tree, NodeId root) noexcept;
    bool set_focused_node(WindowId window, NodeId node) noexcept;

    bool poll_event(WindowEvent& event, WindowId& logical_window) noexcept;

    Sdl3Window* backend(WindowId window) noexcept;
    const Sdl3Window* backend(WindowId window) const noexcept;
    WindowSet& windows() noexcept;
    const WindowSet& windows() const noexcept;
    WindowRouter& router() noexcept;
    const WindowRouter& router() const noexcept;

  private:
    std::size_t slot_for(WindowId window) const noexcept;
    WindowId logical_for_sdl(WindowId sdl_window) const noexcept;
    void sync_backends() noexcept;

    std::array<WindowSlot, kCapacity> slots_{};
    std::array<std::optional<Sdl3Window>, kCapacity> backends_{};
    std::array<WindowId, kCapacity> sdl_ids_{};
    std::array<WindowRouteState, kCapacity> route_storage_{};
    WindowRouter router_{route_storage_};
    WindowSet windows_{slots_};
};

}

namespace veyra::app {
using ::veyra::app::sdl3::Sdl3WindowHost;
}

#endif
