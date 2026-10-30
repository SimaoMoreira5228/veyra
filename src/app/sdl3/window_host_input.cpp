#include "veyra/app/sdl3/window_host.hpp"

namespace veyra::app::sdl3 {

NodeId Sdl3WindowHost::activate(WindowId window) noexcept {
    std::lock_guard lock(state_mutex_);
    if (!router_.activate(window)) {
        return {};
    }
    return windows_.focused_node(window);
}

HitTestResult Sdl3WindowHost::hit_test_and_hover(
    WindowId window,
    LayoutPoint logical_point,
    std::span<const LayoutRect> bounds_by_node_index,
    NodeId& target
) noexcept {
    std::lock_guard lock(state_mutex_);
    return hit_test_hover_locked(window, logical_point, bounds_by_node_index, target);
}

HitTestResult Sdl3WindowHost::press_pointer(
    WindowId window,
    std::uint32_t pointer,
    LayoutPoint logical_point,
    std::span<const LayoutRect> bounds_by_node_index,
    NodeId& target
) noexcept {
    std::lock_guard lock(state_mutex_);
    const auto result = hit_test_hover_locked(window, logical_point, bounds_by_node_index, target);
    if (result == HitTestResult::hit) {
        router_.capture_pointer(window, pointer, target);
    }
    return result;
}

HitTestResult Sdl3WindowHost::hit_test_hover_locked(
    WindowId window,
    LayoutPoint logical_point,
    std::span<const LayoutRect> bounds_by_node_index,
    NodeId& target
) noexcept {
    const auto result =
        windows_.hit_test_logical(window, logical_point, bounds_by_node_index, target);
    if (result == HitTestResult::hit) {
        router_.set_hover(window, target);
    } else {
        router_.clear_hover(window);
    }
    return result;
}

bool Sdl3WindowHost::capture_pointer(
    WindowId window,
    std::uint32_t pointer,
    NodeId target
) noexcept {
    std::lock_guard lock(state_mutex_);
    return router_.capture_pointer(window, pointer, target);
}

bool Sdl3WindowHost::release_pointer(WindowId window, std::uint32_t pointer) noexcept {
    std::lock_guard lock(state_mutex_);
    return router_.release_pointer(window, pointer);
}

bool Sdl3WindowHost::set_hover(WindowId window, NodeId node) noexcept {
    std::lock_guard lock(state_mutex_);
    return router_.set_hover(window, node);
}

bool Sdl3WindowHost::clear_hover(WindowId window) noexcept {
    std::lock_guard lock(state_mutex_);
    return router_.clear_hover(window);
}

}
