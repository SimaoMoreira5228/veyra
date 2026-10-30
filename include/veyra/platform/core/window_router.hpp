#ifndef VEYRA_PLATFORM_WINDOW_ROUTER_HPP
#define VEYRA_PLATFORM_WINDOW_ROUTER_HPP

#include "veyra/core/tree/node_id_pool.hpp"
#include "veyra/platform/core/window.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace veyra {

struct WindowRouteState {
    WindowId id() const noexcept;
    NodeId focused_node() const noexcept;
    float logical_width() const noexcept;
    float logical_height() const noexcept;
    float scale_x() const noexcept;
    float scale_y() const noexcept;
    std::uint64_t scale_generation() const noexcept;
    static constexpr std::size_t kPointerCaptureCapacity = 4;

    bool focused() const noexcept;
    bool damaged() const noexcept;
    std::size_t capture_count() const noexcept;
    std::uint32_t captured_pointer(std::size_t index) const noexcept;
    NodeId capture_target(std::size_t index) const noexcept;
    bool pointer_captured(std::uint32_t pointer) const noexcept;
    NodeId capture_target_for(std::uint32_t pointer) const noexcept;
    NodeId hovered_node() const noexcept;

  private:
    friend class WindowRouter;

    WindowId id_{kInvalidWindowId};
    NodeId focused_node_{};
    float logical_width_{};
    float logical_height_{};
    float scale_x_{1.0F};
    float scale_y_{1.0F};
    std::uint64_t scale_generation_{};
    struct Capture {
        std::uint32_t pointer{};
        NodeId target{};
    };
    std::array<Capture, kPointerCaptureCapacity> captures_{};
    std::size_t capture_count_{};
    NodeId hovered_node_{};
    bool focused_{};
    bool damaged_{true};
};

enum class WindowRouteResult : std::uint8_t {
    routed,
    invalid_window,
    unknown_window,
    invalid_geometry,
};

constexpr std::string_view window_route_result_name(WindowRouteResult result) noexcept {
    switch (result) {
    case WindowRouteResult::routed:
        return "OK";
    case WindowRouteResult::invalid_window:
        return "BAD";
    case WindowRouteResult::unknown_window:
        return "UNK";
    case WindowRouteResult::invalid_geometry:
        return "GEOM";
    }
    return "?";
}

using RouteObserver =
    void (*)(void* user, const WindowEvent& event, WindowRouteResult result) noexcept;

class WindowRouter {
  public:
    explicit WindowRouter(std::span<WindowRouteState> storage) noexcept;

    WindowRouteState* attach(
        WindowId id,
        float logical_width,
        float logical_height,
        float scale_x,
        float scale_y
    ) noexcept;
    bool detach(WindowId id) noexcept;
    const WindowRouteState* find(WindowId id) noexcept;
    const WindowRouteState* find(WindowId id) const noexcept;

    WindowRouteResult route(const WindowEvent& event) noexcept;
    bool activate(WindowId id) noexcept;
    void set_observer(RouteObserver observer, void* user) noexcept;
    bool set_focused_node(WindowId id, NodeId node) noexcept;
    bool capture_pointer(WindowId id, std::uint32_t pointer, NodeId target) noexcept;
    bool release_pointer(WindowId id, std::uint32_t pointer) noexcept;
    bool set_hover(WindowId id, NodeId node) noexcept;
    bool clear_hover(WindowId id) noexcept;
    bool mark_damaged(WindowId id) noexcept;
    bool mark_presented(WindowId id) noexcept;

    std::size_t size() const noexcept;

  private:
    std::span<WindowRouteState> storage_;
    std::size_t size_{};
    RouteObserver observer_{};
    void* observer_user_{};

    WindowRouteResult route_event(const WindowEvent& event) noexcept;
    WindowRouteState* find_mutable(WindowId id) noexcept;
    static bool valid_geometry(float width, float height, float scale_x, float scale_y) noexcept;
};

}

namespace veyra::platform {
using ::veyra::RouteObserver;
using ::veyra::WindowRouter;
using ::veyra::WindowRouteResult;
using ::veyra::WindowRouteState;
}

namespace veyra::platform::core {
using ::veyra::RouteObserver;
using ::veyra::WindowRouter;
using ::veyra::WindowRouteResult;
using ::veyra::WindowRouteState;
}

#endif
