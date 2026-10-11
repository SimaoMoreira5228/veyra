#ifndef VEYRA_PLATFORM_WINDOW_ROUTER_HPP
#define VEYRA_PLATFORM_WINDOW_ROUTER_HPP

#include "veyra/core/node_id_pool.hpp"
#include "veyra/platform/window.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace veyra {

struct WindowRouteState {
    WindowId id() const noexcept;
    NodeId focused_node() const noexcept;
    float logical_width() const noexcept;
    float logical_height() const noexcept;
    float scale_x() const noexcept;
    float scale_y() const noexcept;
    std::uint64_t scale_generation() const noexcept;
    bool focused() const noexcept;
    bool damaged() const noexcept;

  private:
    friend class WindowRouter;

    WindowId id_{kInvalidWindowId};
    NodeId focused_node_{};
    float logical_width_{};
    float logical_height_{};
    float scale_x_{1.0F};
    float scale_y_{1.0F};
    std::uint64_t scale_generation_{};
    bool focused_{};
    bool damaged_{true};
};

enum class WindowRouteResult : std::uint8_t {
    routed,
    invalid_window,
    unknown_window,
    invalid_geometry,
};

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
    bool set_focused_node(WindowId id, NodeId node) noexcept;
    bool mark_damaged(WindowId id) noexcept;
    bool mark_presented(WindowId id) noexcept;

    std::size_t size() const noexcept;

  private:
    std::span<WindowRouteState> storage_;
    std::size_t size_{};

    WindowRouteState* find_mutable(WindowId id) noexcept;
    static bool valid_geometry(float width, float height, float scale_x, float scale_y) noexcept;
};

}

#endif
