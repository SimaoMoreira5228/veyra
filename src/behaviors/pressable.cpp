#include "veyra/behaviors/pressable.hpp"

namespace veyra {

PressableState PressableController::state() const noexcept {
    return {
        .hovered = hovered_,
        .pressed = pointer_pressed_ || space_armed_ || enter_armed_,
        .focused = focused_,
        .disabled = disabled_,
    };
}

bool PressableController::set_disabled(bool disabled) noexcept {
    if (disabled_ == disabled) {
        return false;
    }

    disabled_ = disabled;
    if (disabled_) {
        hovered_ = false;
        pointer_pressed_ = false;
        active_pointer_id_ = 0;
        focused_ = false;
        space_armed_ = false;
        enter_armed_ = false;
    }
    return true;
}

PressableResult PressableController::set_focused(bool focused) noexcept {
    if (focused_ == focused || (focused && disabled_)) {
        return PressableResult::ignored;
    }

    focused_ = focused;
    if (!focused_) {
        space_armed_ = false;
        enter_armed_ = false;
    }
    return PressableResult::handled;
}

PressableResult PressableController::pointer_enter() noexcept {
    if (disabled_) {
        return PressableResult::ignored;
    }

    hovered_ = true;
    return PressableResult::handled;
}

PressableResult PressableController::pointer_leave() noexcept {
    if (!hovered_) {
        return PressableResult::ignored;
    }

    hovered_ = false;
    return PressableResult::handled;
}

PressableResult PressableController::pointer_down(std::uint64_t pointer_id, bool primary) noexcept {
    if (!primary || disabled_ || pointer_pressed_) {
        return PressableResult::ignored;
    }

    pointer_pressed_ = true;
    active_pointer_id_ = pointer_id;
    return PressableResult::handled;
}

PressableResult
PressableController::pointer_up(std::uint64_t pointer_id, bool primary, bool inside) noexcept {
    if (!primary || !pointer_pressed_ || pointer_id != active_pointer_id_) {
        return PressableResult::ignored;
    }

    pointer_pressed_ = false;
    active_pointer_id_ = 0;
    if (inside && !disabled_) {
        return PressableResult::activated;
    }
    return PressableResult::handled;
}

PressableResult PressableController::pointer_cancel(std::uint64_t pointer_id) noexcept {
    if (!pointer_pressed_ || pointer_id != active_pointer_id_) {
        return PressableResult::ignored;
    }

    pointer_pressed_ = false;
    active_pointer_id_ = 0;
    return PressableResult::handled;
}

PressableResult PressableController::key_down(PressableKey key) noexcept {
    if (disabled_ || !focused_) {
        return PressableResult::ignored;
    }

    switch (key) {
    case PressableKey::space:
        if (space_armed_) {
            return PressableResult::handled;
        }
        space_armed_ = true;
        return PressableResult::handled;
    case PressableKey::enter:
        if (enter_armed_) {
            return PressableResult::handled;
        }
        enter_armed_ = true;
        return PressableResult::activated;
    }
    return PressableResult::ignored;
}

PressableResult PressableController::key_up(PressableKey key) noexcept {
    switch (key) {
    case PressableKey::space:
        if (!space_armed_) {
            return PressableResult::ignored;
        }
        space_armed_ = false;
        return disabled_ ? PressableResult::handled : PressableResult::activated;
    case PressableKey::enter:
        if (!enter_armed_) {
            return PressableResult::ignored;
        }
        enter_armed_ = false;
        return PressableResult::handled;
    default:
        return PressableResult::ignored;
    }
}

PressableResult PressableController::invoke() const noexcept {
    return disabled_ ? PressableResult::ignored : PressableResult::activated;
}

bool PressableController::write_semantics(
    std::string_view accessible_name,
    SemanticNodeData& output
) const noexcept {
    if (accessible_name.empty()) {
        return false;
    }

    output = {
        .role = SemanticRole::button,
        .label = accessible_name,
        .states = disabled_ ? state_flag(SemanticState::disabled) : std::uint16_t{},
        .actions = disabled_ ? std::uint16_t{} : action_flag(SemanticAction::activate),
    };
    return true;
}

}
