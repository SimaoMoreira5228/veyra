#include "veyra/behaviors/toggle.hpp"

namespace veyra {

ToggleState ToggleController::state() const noexcept {
    return {
        .kind = kind_,
        .mode = mode_,
        .value = value_,
        .hovered = hovered_,
        .pressed = pointer_pressed_ || space_armed_ || enter_armed_,
        .focused = focused_,
        .disabled = disabled_,
    };
}

ToggleValue ToggleController::value() const noexcept {
    return value_;
}

ToggleKind ToggleController::kind() const noexcept {
    return kind_;
}

ToggleMode ToggleController::mode() const noexcept {
    return mode_;
}

bool ToggleController::mixed_allowed() const noexcept {
    return allow_mixed_;
}

bool ToggleController::configure(const ToggleConfig& config) noexcept {
    const ToggleValue normalized = config.allow_mixed || config.value != ToggleValue::mixed
        ? config.value
        : ToggleValue::unchecked;

    const bool changed = kind_ != config.kind || mode_ != config.mode || value_ != normalized ||
        allow_mixed_ != config.allow_mixed;

    kind_ = config.kind;
    mode_ = config.mode;
    value_ = normalized;
    allow_mixed_ = config.allow_mixed;
    hovered_ = false;
    focused_ = false;

    clear_armed();

    return changed;
}

bool ToggleController::migrate_mode(ToggleMode mode, ToggleValue value) noexcept {
    const ToggleValue normalized = normalize(value);
    const bool changed = mode_ != mode || value_ != normalized;

    mode_ = mode;
    value_ = normalized;

    clear_armed();

    return changed;
}

bool ToggleController::set_kind(ToggleKind kind) noexcept {
    if (kind_ == kind) {
        return false;
    }

    kind_ = kind;

    return true;
}

bool ToggleController::set_allow_mixed(bool allow) noexcept {
    if (allow_mixed_ == allow) {
        return false;
    }

    allow_mixed_ = allow;
    if (!allow_mixed_ && value_ == ToggleValue::mixed) {
        value_ = ToggleValue::checked;
    }

    return true;
}

bool ToggleController::set_disabled(bool disabled) noexcept {
    if (disabled_ == disabled) {
        return false;
    }

    disabled_ = disabled;
    if (disabled_) {
        hovered_ = false;
        focused_ = false;

        clear_armed();
    }

    return true;
}

ToggleResult ToggleController::set_focused(bool focused) noexcept {
    if (focused_ == focused || (focused && disabled_)) {
        return ToggleResult::ignored;
    }

    focused_ = focused;
    if (!focused_) {
        space_armed_ = false;
        enter_armed_ = false;
    }

    return ToggleResult::handled;
}

ToggleTransition ToggleController::set_controlled_value(ToggleValue value) noexcept {
    if (mode_ != ToggleMode::controlled) {
        return {.result = ToggleResult::ignored, .value = value_};
    }

    const ToggleValue normalized = normalize(value);
    if (value_ == normalized) {
        return {.result = ToggleResult::handled, .value = value_};
    }

    value_ = normalized;

    return {.result = ToggleResult::changed, .value = value_};
}

ToggleTransition ToggleController::request_value(ToggleValue value) noexcept {
    if (disabled_) {
        return {.result = ToggleResult::ignored, .value = value_};
    }

    const ToggleValue normalized = normalize(value);
    if (value_ == normalized) {
        return {.result = ToggleResult::handled, .value = value_};
    }

    return commit_or_emit(normalized);
}

ToggleResult ToggleController::pointer_enter() noexcept {
    if (disabled_) {
        return ToggleResult::ignored;
    }

    hovered_ = true;

    return ToggleResult::handled;
}

ToggleResult ToggleController::pointer_leave() noexcept {
    if (!hovered_) {
        return ToggleResult::ignored;
    }

    hovered_ = false;

    return ToggleResult::handled;
}

ToggleResult ToggleController::pointer_down(std::uint64_t pointer_id, bool primary) noexcept {
    if (!primary || disabled_ || pointer_pressed_) {
        return ToggleResult::ignored;
    }

    pointer_pressed_ = true;
    active_pointer_id_ = pointer_id;

    return ToggleResult::handled;
}

ToggleTransition
ToggleController::pointer_up(std::uint64_t pointer_id, bool primary, bool inside) noexcept {
    if (!primary || !pointer_pressed_ || pointer_id != active_pointer_id_) {
        return {.result = ToggleResult::ignored, .value = value_};
    }

    pointer_pressed_ = false;
    active_pointer_id_ = 0;
    if (!inside || disabled_) {
        return {.result = ToggleResult::handled, .value = value_};
    }

    return commit_or_emit(next_value());
}

ToggleResult ToggleController::pointer_cancel(std::uint64_t pointer_id) noexcept {
    if (!pointer_pressed_ || pointer_id != active_pointer_id_) {
        return ToggleResult::ignored;
    }

    pointer_pressed_ = false;
    active_pointer_id_ = 0;

    return ToggleResult::handled;
}

ToggleTransition ToggleController::key_down(ToggleKey key) noexcept {
    if (disabled_ || !focused_) {
        return {.result = ToggleResult::ignored, .value = value_};
    }

    if (key == ToggleKey::space) {
        if (space_armed_) {
            return {.result = ToggleResult::handled, .value = value_};
        }

        space_armed_ = true;

        return {.result = ToggleResult::handled, .value = value_};
    }

    if (enter_armed_) {
        return {.result = ToggleResult::handled, .value = value_};
    }

    enter_armed_ = true;

    return commit_or_emit(next_value());
}

ToggleTransition ToggleController::key_up(ToggleKey key) noexcept {
    if (key == ToggleKey::space) {
        if (!space_armed_) {
            return {.result = ToggleResult::ignored, .value = value_};
        }

        space_armed_ = false;
        if (disabled_) {
            return {.result = ToggleResult::handled, .value = value_};
        }

        return commit_or_emit(next_value());
    }

    if (!enter_armed_) {
        return {.result = ToggleResult::ignored, .value = value_};
    }

    enter_armed_ = false;

    return {.result = ToggleResult::handled, .value = value_};
}

ToggleTransition ToggleController::invoke() noexcept {
    if (disabled_) {
        return {.result = ToggleResult::ignored, .value = value_};
    }

    return commit_or_emit(next_value());
}

bool ToggleController::write_semantics(
    std::string_view accessible_name,
    SemanticNodeData& output
) const noexcept {
    if (accessible_name.empty()) {
        return false;
    }

    std::uint16_t states{};
    if (disabled_) {
        states = states | SemanticState::disabled;
    }
    if (value_ == ToggleValue::checked || value_ == ToggleValue::mixed) {
        states = states | SemanticState::checked;
    }
    if (value_ == ToggleValue::mixed) {
        states = states | SemanticState::mixed;
    }

    output = {
        .role = kind_ == ToggleKind::checkbox ? SemanticRole::checkbox : SemanticRole::switch_,
        .label = accessible_name,
        .states = states,
        .actions = disabled_ ? std::uint16_t{} : action_flag(SemanticAction::activate),
    };

    return true;
}

ToggleValue ToggleController::normalize(ToggleValue value) const noexcept {
    if (value == ToggleValue::mixed && !allow_mixed_) {
        return ToggleValue::unchecked;
    }

    return value;
}

ToggleValue ToggleController::next_value() const noexcept {
    if (value_ == ToggleValue::checked) {
        return ToggleValue::unchecked;
    }

    return ToggleValue::checked;
}

ToggleTransition ToggleController::commit_or_emit(ToggleValue value) noexcept {
    if (mode_ == ToggleMode::uncontrolled) {
        value_ = value;
    }

    return {.result = ToggleResult::changed, .value = value};
}

void ToggleController::clear_armed() noexcept {
    pointer_pressed_ = false;
    active_pointer_id_ = 0;
    space_armed_ = false;
    enter_armed_ = false;
}

}
