#include "veyra/platform/core/seat.hpp"

namespace veyra {

SeatId SeatInputState::id() const noexcept {
    return id_;
}

WindowId SeatInputState::focused_window() const noexcept {
    return focused_window_;
}

NodeId SeatInputState::focused_node() const noexcept {
    return focused_node_;
}

std::uint32_t SeatInputState::modifiers() const noexcept {
    return modifiers_;
}

std::size_t SeatInputState::capture_count() const noexcept {
    return capture_count_;
}

std::uint32_t SeatInputState::captured_pointer(std::size_t index) const noexcept {
    if (index >= capture_count_) {
        return 0;
    }

    return captures_[index].pointer;
}

NodeId SeatInputState::capture_target(std::size_t index) const noexcept {
    if (index >= capture_count_) {
        return {};
    }

    return captures_[index].target;
}

bool SeatInputState::pointer_captured(std::uint32_t pointer) const noexcept {
    for (std::size_t index = 0; index < capture_count_; ++index) {
        if (captures_[index].pointer == pointer) {
            return true;
        }
    }

    return false;
}

NodeId SeatInputState::capture_target_for(std::uint32_t pointer) const noexcept {
    for (std::size_t index = 0; index < capture_count_; ++index) {
        if (captures_[index].pointer == pointer) {
            return captures_[index].target;
        }
    }

    return {};
}

NodeId SeatInputState::hovered_node() const noexcept {
    return hovered_node_;
}

SeatInputSet::SeatInputSet(std::span<SeatInputState> storage) noexcept : storage_(storage) {
}

SeatResult SeatInputSet::create(SeatId& out_seat) noexcept {
    for (auto& state : storage_) {
        if (state.id_ != kInvalidSeatId) {
            continue;
        }

        if (next_id_ == kInvalidSeatId) {
            next_id_ = 1;
        }

        state = {};
        state.id_ = next_id_++;
        out_seat = state.id_;
        ++size_;

        return SeatResult::ok;
    }

    return SeatResult::capacity;
}

bool SeatInputSet::release(SeatId seat) noexcept {
    auto* state = find_mutable(seat);
    if (state == nullptr) {
        return false;
    }

    *state = {};
    --size_;

    return true;
}

SeatInputState* SeatInputSet::find(SeatId seat) noexcept {
    return find_mutable(seat);
}

const SeatInputState* SeatInputSet::find(SeatId seat) const noexcept {
    if (seat == kInvalidSeatId) {
        return nullptr;
    }

    for (const auto& state : storage_) {
        if (state.id_ == seat) {
            return &state;
        }
    }

    return nullptr;
}

std::size_t SeatInputSet::size() const noexcept {
    return size_;
}

std::size_t SeatInputSet::capacity() const noexcept {
    return storage_.size();
}

bool SeatInputSet::focus(SeatId seat, WindowId window, NodeId node) noexcept {
    auto* state = find_mutable(seat);
    if (state == nullptr) {
        return false;
    }

    if (state->focused_window_ != window) {
        state->captures_ = {};
        state->capture_count_ = 0;
        state->hovered_node_ = {};
    }

    state->focused_window_ = window;
    state->focused_node_ = node;

    return true;
}

bool SeatInputSet::set_modifiers(SeatId seat, std::uint32_t modifiers) noexcept {
    auto* state = find_mutable(seat);
    if (state == nullptr) {
        return false;
    }

    state->modifiers_ = modifiers;

    return true;
}

SeatResult
SeatInputSet::capture_pointer(SeatId seat, std::uint32_t pointer, NodeId target) noexcept {
    auto* state = find_mutable(seat);
    if (state == nullptr) {
        return SeatResult::invalid_seat;
    }
    if (target.is_null()) {
        return SeatResult::invalid_target;
    }

    const auto existing = capture_index(*state, pointer);
    if (existing != kSeatPointerCapacity) {
        state->captures_[existing].target = target;
        return SeatResult::ok;
    }

    if (state->capture_count_ == state->captures_.size()) {
        return SeatResult::capacity;
    }

    state->captures_[state->capture_count_] = {
        .pointer = pointer,
        .target = target,
    };
    ++state->capture_count_;

    return SeatResult::ok;
}

bool SeatInputSet::release_pointer(SeatId seat, std::uint32_t pointer) noexcept {
    auto* state = find_mutable(seat);
    if (state == nullptr) {
        return false;
    }

    const auto index = capture_index(*state, pointer);
    if (index == kSeatPointerCapacity) {
        return false;
    }

    for (std::size_t move = index + 1; move < state->capture_count_; ++move) {
        state->captures_[move - 1] = state->captures_[move];
    }
    --state->capture_count_;
    state->captures_[state->capture_count_] = {};

    return true;
}

bool SeatInputSet::set_hover(SeatId seat, NodeId node) noexcept {
    auto* state = find_mutable(seat);
    if (state == nullptr) {
        return false;
    }

    state->hovered_node_ = node;

    return true;
}

bool SeatInputSet::clear_hover(SeatId seat) noexcept {
    auto* state = find_mutable(seat);
    if (state == nullptr) {
        return false;
    }

    state->hovered_node_ = {};

    return true;
}

bool SeatInputSet::clear_pointer_state(SeatId seat) noexcept {
    auto* state = find_mutable(seat);
    if (state == nullptr) {
        return false;
    }

    state->captures_ = {};
    state->capture_count_ = 0;
    state->hovered_node_ = {};

    return true;
}

SeatInputState* SeatInputSet::find_mutable(SeatId seat) noexcept {
    if (seat == kInvalidSeatId) {
        return nullptr;
    }

    for (auto& state : storage_) {
        if (state.id_ == seat) {
            return &state;
        }
    }

    return nullptr;
}

std::size_t
SeatInputSet::capture_index(const SeatInputState& state, std::uint32_t pointer) const noexcept {
    for (std::size_t index = 0; index < state.capture_count_; ++index) {
        if (state.captures_[index].pointer == pointer) {
            return index;
        }
    }

    return kSeatPointerCapacity;
}

}
