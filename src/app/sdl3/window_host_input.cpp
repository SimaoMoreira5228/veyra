#include "veyra/app/sdl3/window_host.hpp"

namespace veyra::app::sdl3 {
namespace {

bool seat_exists(const SeatInputSet& seats, SeatId seat) noexcept {
    return seats.find(seat) != nullptr;
}

}

SeatId Sdl3WindowHost::default_seat() const noexcept {
    std::lock_guard lock(state_mutex_);
    return default_seat_;
}

SeatId Sdl3WindowHost::active_seat() const noexcept {
    std::lock_guard lock(state_mutex_);
    return active_seat_;
}

bool Sdl3WindowHost::set_active_seat(SeatId seat) noexcept {
    std::lock_guard lock(state_mutex_);
    if (!seat_exists(seats_, seat)) {
        return false;
    }

    active_seat_ = seat;
    return true;
}

SeatId Sdl3WindowHost::create_seat() noexcept {
    std::lock_guard lock(state_mutex_);
    auto seat = SeatId{};
    if (seats_.create(seat) != SeatResult::ok) {
        return kInvalidSeatId;
    }
    if (default_seat_ == kInvalidSeatId) {
        default_seat_ = seat;
    }

    return seat;
}

bool Sdl3WindowHost::release_seat(SeatId seat) noexcept {
    std::lock_guard lock(state_mutex_);
    if (seat == kInvalidSeatId || seat == default_seat_ || !seats_.release(seat)) {
        return false;
    }
    if (active_seat_ == seat) {
        active_seat_ = default_seat_;
    }

    return true;
}

std::size_t Sdl3WindowHost::seat_count() const noexcept {
    std::lock_guard lock(state_mutex_);
    return seats_.size();
}

bool Sdl3WindowHost::seat_focus(SeatId seat, WindowId window, NodeId node) noexcept {
    std::lock_guard lock(state_mutex_);
    return seats_.focus(seat, window, node);
}

NodeId Sdl3WindowHost::seat_focused_node(SeatId seat) const noexcept {
    std::lock_guard lock(state_mutex_);
    const auto* state = seats_.find(seat);
    return state == nullptr ? NodeId{} : state->focused_node();
}

bool Sdl3WindowHost::seat_set_modifiers(SeatId seat, std::uint32_t modifiers) noexcept {
    std::lock_guard lock(state_mutex_);
    return seats_.set_modifiers(seat, modifiers);
}

std::uint32_t Sdl3WindowHost::seat_modifiers(SeatId seat) const noexcept {
    std::lock_guard lock(state_mutex_);
    const auto* state = seats_.find(seat);
    return state == nullptr ? 0 : state->modifiers();
}

bool Sdl3WindowHost::seat_pointer_captured(SeatId seat, std::uint32_t pointer) const noexcept {
    std::lock_guard lock(state_mutex_);
    const auto* state = seats_.find(seat);
    return state != nullptr && state->pointer_captured(pointer);
}

NodeId Sdl3WindowHost::seat_hovered_node(SeatId seat) const noexcept {
    std::lock_guard lock(state_mutex_);
    const auto* state = seats_.find(seat);
    return state == nullptr ? NodeId{} : state->hovered_node();
}

NodeId Sdl3WindowHost::activate(WindowId window) noexcept {
    std::lock_guard lock(state_mutex_);
    if (!router_.activate(window)) {
        return {};
    }
    const auto node = windows_.focused_node(window);
    seats_.focus(active_seat_, window, node);

    return node;
}

NodeId Sdl3WindowHost::activate(SeatId seat, WindowId window) noexcept {
    std::lock_guard lock(state_mutex_);
    if (!seat_exists(seats_, seat) || !router_.activate(window)) {
        return {};
    }
    const auto node = windows_.focused_node(window);
    seats_.focus(seat, window, node);

    return node;
}

HitTestResult Sdl3WindowHost::hit_test_and_hover(
    WindowId window,
    LayoutPoint logical_point,
    std::span<const LayoutRect> bounds_by_node_index,
    NodeId& target
) noexcept {
    std::lock_guard lock(state_mutex_);
    return hit_test_hover_locked(active_seat_, window, logical_point, bounds_by_node_index, target);
}

HitTestResult Sdl3WindowHost::hit_test_and_hover(
    SeatId seat,
    WindowId window,
    LayoutPoint logical_point,
    std::span<const LayoutRect> bounds_by_node_index,
    NodeId& target
) noexcept {
    std::lock_guard lock(state_mutex_);
    if (!seat_exists(seats_, seat)) {
        target = {};
        return HitTestResult::no_hit;
    }

    return hit_test_hover_locked(seat, window, logical_point, bounds_by_node_index, target);
}

HitTestResult Sdl3WindowHost::press_pointer(
    WindowId window,
    std::uint32_t pointer,
    LayoutPoint logical_point,
    std::span<const LayoutRect> bounds_by_node_index,
    NodeId& target
) noexcept {
    std::lock_guard lock(state_mutex_);
    const auto result =
        hit_test_hover_locked(active_seat_, window, logical_point, bounds_by_node_index, target);
    if (result == HitTestResult::hit) {
        seats_.capture_pointer(active_seat_, pointer, target);
        router_.capture_pointer(window, pointer, target);
    }

    return result;
}

HitTestResult Sdl3WindowHost::press_pointer(
    SeatId seat,
    WindowId window,
    std::uint32_t pointer,
    LayoutPoint logical_point,
    std::span<const LayoutRect> bounds_by_node_index,
    NodeId& target
) noexcept {
    std::lock_guard lock(state_mutex_);
    if (!seat_exists(seats_, seat)) {
        target = {};
        return HitTestResult::no_hit;
    }

    const auto result =
        hit_test_hover_locked(seat, window, logical_point, bounds_by_node_index, target);
    if (result == HitTestResult::hit) {
        seats_.capture_pointer(seat, pointer, target);
        if (seat == active_seat_) {
            router_.capture_pointer(window, pointer, target);
        }
    }

    return result;
}

HitTestResult Sdl3WindowHost::hit_test_hover_locked(
    SeatId seat,
    WindowId window,
    LayoutPoint logical_point,
    std::span<const LayoutRect> bounds_by_node_index,
    NodeId& target
) noexcept {
    const auto result =
        windows_.hit_test_logical(window, logical_point, bounds_by_node_index, target);
    if (result == HitTestResult::hit) {
        seats_.set_hover(seat, target);
        if (seat == active_seat_) {
            router_.set_hover(window, target);
        }
    } else {
        seats_.clear_hover(seat);
        if (seat == active_seat_) {
            router_.clear_hover(window);
        }
    }

    return result;
}

bool Sdl3WindowHost::capture_pointer(
    WindowId window,
    std::uint32_t pointer,
    NodeId target
) noexcept {
    return capture_pointer(active_seat_, window, pointer, target);
}

bool Sdl3WindowHost::capture_pointer(
    SeatId seat,
    WindowId window,
    std::uint32_t pointer,
    NodeId target
) noexcept {
    std::lock_guard lock(state_mutex_);
    if (seats_.capture_pointer(seat, pointer, target) != SeatResult::ok) {
        return false;
    }
    if (seat == active_seat_) {
        router_.capture_pointer(window, pointer, target);
    }

    return true;
}

bool Sdl3WindowHost::release_pointer(WindowId window, std::uint32_t pointer) noexcept {
    return release_pointer(active_seat_, window, pointer);
}

bool Sdl3WindowHost::release_pointer(SeatId seat, WindowId window, std::uint32_t pointer) noexcept {
    std::lock_guard lock(state_mutex_);
    if (!seats_.release_pointer(seat, pointer)) {
        return false;
    }
    if (seat == active_seat_) {
        router_.release_pointer(window, pointer);
    }

    return true;
}

bool Sdl3WindowHost::set_hover(WindowId window, NodeId node) noexcept {
    return set_hover(active_seat_, window, node);
}

bool Sdl3WindowHost::set_hover(SeatId seat, WindowId window, NodeId node) noexcept {
    std::lock_guard lock(state_mutex_);
    if (!seats_.set_hover(seat, node)) {
        return false;
    }
    if (seat == active_seat_) {
        router_.set_hover(window, node);
    }

    return true;
}

bool Sdl3WindowHost::clear_hover(WindowId window) noexcept {
    return clear_hover(active_seat_, window);
}

bool Sdl3WindowHost::clear_hover(SeatId seat, WindowId window) noexcept {
    std::lock_guard lock(state_mutex_);
    if (!seats_.clear_hover(seat)) {
        return false;
    }
    if (seat == active_seat_) {
        router_.clear_hover(window);
    }

    return true;
}

void Sdl3WindowHost::track_input_locked(const WindowEvent& event) noexcept {
    auto modifiers = std::uint32_t{};
    switch (event.kind) {
    case WindowEventKind::pointer_down:
    case WindowEventKind::pointer_up:
    case WindowEventKind::pointer_move:
        modifiers = event.data.pointer.modifiers;
        break;
    case WindowEventKind::pointer_cancel:
        seats_.clear_pointer_state(active_seat_);
        return;
    case WindowEventKind::pointer_leave:
        seats_.clear_hover(active_seat_);
        modifiers = event.data.pointer.modifiers;
        break;
    case WindowEventKind::wheel:
        modifiers = event.data.wheel.modifiers;
        break;
    case WindowEventKind::key_down:
    case WindowEventKind::key_up:
        modifiers = event.data.key.modifiers;
        break;
    case WindowEventKind::focus_gained:
        seats_.focus(active_seat_, event.window, windows_.focused_node(event.window));
        return;
    case WindowEventKind::focus_lost:
        seats_.clear_pointer_state(active_seat_);
        seats_.set_modifiers(active_seat_, 0);
        return;
    default:
        return;
    }

    seats_.set_modifiers(active_seat_, modifiers);
}

}
