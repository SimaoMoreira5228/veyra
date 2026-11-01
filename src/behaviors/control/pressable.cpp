#include "veyra/behaviors/control/pressable.hpp"

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

constexpr std::uint64_t kPressableHoverDurationMs = 120;
constexpr std::uint64_t kPressablePressDurationMs = 80;

PressableTintAnimator::PressableTintAnimator() noexcept {
    hover_.start(0.0F, 0.0F, 0, kPressableHoverDurationMs, AnimationEasing::ease_out_cubic);
    press_.start(0.0F, 0.0F, 0, kPressablePressDurationMs, AnimationEasing::ease_out_cubic);
}

bool PressableTintAnimator::tick(PressableState state, std::uint64_t now_ms) noexcept {
    const auto hover_target = (state.hovered && !state.disabled) ? 1.0F : 0.0F;
    const auto press_target = (state.pressed && !state.disabled) ? 1.0F : 0.0F;

    if (hover_target != hover_.target()) {
        hover_.retarget(hover_target, now_ms);
    }

    if (press_target != press_.target()) {
        press_.retarget(press_target, now_ms);
    }

    const auto before = tint();

    hover_.advance(now_ms);
    press_.advance(now_ms);

    const auto after = tint();
    return before.hover != after.hover || before.press != after.press;
}

PressableTint PressableTintAnimator::tint() const noexcept {
    return {
        .hover = hover_.value(),
        .press = press_.value(),
    };
}

bool PressableTintAnimator::running() const noexcept {
    return hover_.running() || press_.running();
}

void PressableTintAnimator::set_paused(bool paused) noexcept {
    hover_.set_paused(paused);
    press_.set_paused(paused);
}

void PressableTintAnimator::set_throttled(bool throttled) noexcept {
    hover_.set_throttled(throttled);
    press_.set_throttled(throttled);
}

void PressableTintAnimator::set_reduced_motion(bool reduced_motion) noexcept {
    hover_.set_reduced_motion(reduced_motion);
    press_.set_reduced_motion(reduced_motion);
}

void PressableTintAnimator::apply_preferences(const SystemPreferences& preferences) noexcept {
    set_reduced_motion(preferences.reduced_motion);
}

}
