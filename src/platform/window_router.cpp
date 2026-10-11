#include "veyra/platform/window_router.hpp"

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
        if (!state->focused_) {
            state->focused_ = true;
            state->damaged_ = true;
        }
    } else if (event.kind == WindowEventKind::focus_lost) {
        if (state->focused_) {
            state->focused_ = false;
            state->damaged_ = true;
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
