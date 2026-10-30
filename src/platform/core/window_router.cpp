#include "veyra/platform/core/window_router.hpp"

#include <cmath>

namespace veyra {

WindowRouter::WindowRouter(std::span<WindowRouteState> storage) noexcept : storage_(storage) {
}

WindowId WindowRouteState::id() const noexcept {
    return id_;
}

NodeId WindowRouteState::focused_node() const noexcept {
    return focused_node_;
}

float WindowRouteState::logical_width() const noexcept {
    return logical_width_;
}

float WindowRouteState::logical_height() const noexcept {
    return logical_height_;
}

float WindowRouteState::scale_x() const noexcept {
    return scale_x_;
}

float WindowRouteState::scale_y() const noexcept {
    return scale_y_;
}

std::uint64_t WindowRouteState::scale_generation() const noexcept {
    return scale_generation_;
}

bool WindowRouteState::focused() const noexcept {
    return focused_;
}

bool WindowRouteState::damaged() const noexcept {
    return damaged_;
}

std::size_t WindowRouteState::capture_count() const noexcept {
    return capture_count_;
}

std::uint32_t WindowRouteState::captured_pointer(std::size_t index) const noexcept {
    return index < capture_count_ ? captures_[index].pointer : 0;
}

NodeId WindowRouteState::capture_target(std::size_t index) const noexcept {
    return index < capture_count_ ? captures_[index].target : NodeId{};
}

bool WindowRouteState::pointer_captured(std::uint32_t pointer) const noexcept {
    for (auto index = std::size_t{}; index < capture_count_; ++index) {
        if (captures_[index].pointer == pointer) {
            return true;
        }
    }
    return false;
}

NodeId WindowRouteState::capture_target_for(std::uint32_t pointer) const noexcept {
    for (auto index = std::size_t{}; index < capture_count_; ++index) {
        if (captures_[index].pointer == pointer) {
            return captures_[index].target;
        }
    }
    return {};
}

NodeId WindowRouteState::hovered_node() const noexcept {
    return hovered_node_;
}

WindowRouteState* WindowRouter::attach(
    WindowId id,
    float logical_width,
    float logical_height,
    float scale_x,
    float scale_y
) noexcept {
    if (id == kInvalidWindowId ||
        !valid_geometry(logical_width, logical_height, scale_x, scale_y) || find(id) != nullptr ||
        size_ == storage_.size()) {
        return nullptr;
    }

    for (auto& state : storage_) {
        if (state.id_ != kInvalidWindowId) {
            continue;
        }

        state = {};
        state.id_ = id;
        state.logical_width_ = logical_width;
        state.logical_height_ = logical_height;
        state.scale_x_ = scale_x;
        state.scale_y_ = scale_y;
        state.scale_generation_ = 1;
        state.damaged_ = true;
        ++size_;
        return &state;
    }

    return nullptr;
}

bool WindowRouter::detach(WindowId id) noexcept {
    auto* state = find_mutable(id);
    if (state == nullptr) {
        return false;
    }

    *state = {};
    --size_;
    return true;
}

const WindowRouteState* WindowRouter::find(WindowId id) noexcept {
    return find_mutable(id);
}

WindowRouteState* WindowRouter::find_mutable(WindowId id) noexcept {
    if (id == kInvalidWindowId) {
        return nullptr;
    }

    for (auto& state : storage_) {
        if (state.id_ == id) {
            return &state;
        }
    }

    return nullptr;
}

const WindowRouteState* WindowRouter::find(WindowId id) const noexcept {
    if (id == kInvalidWindowId) {
        return nullptr;
    }

    for (const auto& state : storage_) {
        if (state.id_ == id) {
            return &state;
        }
    }

    return nullptr;
}

WindowRouteResult WindowRouter::route(const WindowEvent& event) noexcept {
    const auto result = route_event(event);
    if (observer_ != nullptr) {
        observer_(observer_user_, event, result);
    }
    return result;
}

bool WindowRouter::activate(WindowId id) noexcept {
    if (find_mutable(id) == nullptr) {
        return false;
    }

    for (auto& state : storage_) {
        if (state.id_ == kInvalidWindowId) {
            continue;
        }
        const auto active = state.id_ == id;
        if (state.focused_ != active) {
            state.focused_ = active;
            state.damaged_ = true;
        }
    }
    return true;
}

void WindowRouter::set_observer(RouteObserver observer, void* user) noexcept {
    observer_ = observer;
    observer_user_ = user;
}

WindowRouteResult WindowRouter::route_event(const WindowEvent& event) noexcept {
    if (event.window == kInvalidWindowId) {
        return WindowRouteResult::invalid_window;
    }

    auto* state = find_mutable(event.window);
    if (state == nullptr) {
        return WindowRouteResult::unknown_window;
    }

    if (event.kind == WindowEventKind::resize) {
        const auto width = event.data.resize.width;
        const auto height = event.data.resize.height;
        if (!valid_geometry(width, height, state->scale_x_, state->scale_y_)) {
            return WindowRouteResult::invalid_geometry;
        }
        if (state->logical_width_ != width || state->logical_height_ != height) {
            state->logical_width_ = width;
            state->logical_height_ = height;
            state->damaged_ = true;
        }
    } else if (event.kind == WindowEventKind::scale_change) {
        const auto x = event.data.scale.x;
        const auto y = event.data.scale.y;
        if (!valid_geometry(state->logical_width_, state->logical_height_, x, y)) {
            return WindowRouteResult::invalid_geometry;
        }
        if (state->scale_x_ != x || state->scale_y_ != y) {
            state->scale_x_ = x;
            state->scale_y_ = y;
            ++state->scale_generation_;
            state->damaged_ = true;
        }
    } else if (event.kind == WindowEventKind::focus_gained) {
        activate(event.window);
    } else if (
        event.kind == WindowEventKind::focus_lost || event.kind == WindowEventKind::pointer_cancel
    ) {
        if (event.kind == WindowEventKind::focus_lost && state->focused_) {
            state->focused_ = false;
            state->damaged_ = true;
        }
        if (state->capture_count_ != 0 || !state->hovered_node_.is_null()) {
            state->capture_count_ = 0;
            state->hovered_node_ = {};
            state->damaged_ = true;
        }
    } else if (event.kind == WindowEventKind::pointer_leave) {
        if (!state->hovered_node_.is_null()) {
            state->hovered_node_ = {};
            state->damaged_ = true;
        }
    } else if (event.kind == WindowEventKind::pointer_up) {
        const auto pointer = event.data.pointer.pointer;
        for (auto index = std::size_t{}; index < state->capture_count_; ++index) {
            if (state->captures_[index].pointer != pointer) {
                continue;
            }
            state->captures_[index] = state->captures_[state->capture_count_ - 1];
            --state->capture_count_;
            state->damaged_ = true;
            break;
        }
    }

    return WindowRouteResult::routed;
}

bool WindowRouter::set_focused_node(WindowId id, NodeId node) noexcept {
    auto* state = find_mutable(id);
    if (state == nullptr) {
        return false;
    }

    if (state->focused_node_ != node) {
        state->focused_node_ = node;
        state->damaged_ = true;
    }
    return true;
}

bool WindowRouter::capture_pointer(WindowId id, std::uint32_t pointer, NodeId target) noexcept {
    auto* state = find_mutable(id);
    if (state == nullptr) {
        return false;
    }

    for (auto index = std::size_t{}; index < state->capture_count_; ++index) {
        if (state->captures_[index].pointer == pointer) {
            state->captures_[index].target = target;
            state->damaged_ = true;
            return true;
        }
    }
    if (state->capture_count_ == WindowRouteState::kPointerCaptureCapacity) {
        return false;
    }

    state->captures_[state->capture_count_] = {.pointer = pointer, .target = target};
    ++state->capture_count_;
    state->damaged_ = true;
    return true;
}

bool WindowRouter::release_pointer(WindowId id, std::uint32_t pointer) noexcept {
    auto* state = find_mutable(id);
    if (state == nullptr) {
        return false;
    }

    for (auto index = std::size_t{}; index < state->capture_count_; ++index) {
        if (state->captures_[index].pointer != pointer) {
            continue;
        }
        state->captures_[index] = state->captures_[state->capture_count_ - 1];
        --state->capture_count_;
        state->damaged_ = true;
        return true;
    }
    return false;
}

bool WindowRouter::set_hover(WindowId id, NodeId node) noexcept {
    auto* state = find_mutable(id);
    if (state == nullptr) {
        return false;
    }
    if (state->hovered_node_ != node) {
        state->hovered_node_ = node;
        state->damaged_ = true;
    }
    return true;
}

bool WindowRouter::clear_hover(WindowId id) noexcept {
    return set_hover(id, {});
}

bool WindowRouter::mark_damaged(WindowId id) noexcept {
    auto* state = find_mutable(id);
    if (state == nullptr) {
        return false;
    }

    state->damaged_ = true;
    return true;
}

bool WindowRouter::mark_presented(WindowId id) noexcept {
    auto* state = find_mutable(id);
    if (state == nullptr) {
        return false;
    }

    state->damaged_ = false;
    return true;
}

std::size_t WindowRouter::size() const noexcept {
    return size_;
}

bool WindowRouter::valid_geometry(
    float width,
    float height,
    float scale_x,
    float scale_y
) noexcept {
    return std::isfinite(width) && std::isfinite(height) && std::isfinite(scale_x) &&
        std::isfinite(scale_y) && width > 0.0F && height > 0.0F && scale_x > 0.0F && scale_y > 0.0F;
}

}
