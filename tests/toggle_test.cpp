#include "veyra/behaviors/toggle.hpp"

#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

void check_uncontrolled_pointer_toggle() {
    auto control = veyra::ToggleController{};
    require(control.value() == veyra::ToggleValue::unchecked, "a new toggle starts unchecked");
    require(
        control.pointer_enter() == veyra::ToggleResult::handled,
        "pointer entry marks the control hovered"
    );
    require(
        control.pointer_down(1, true) == veyra::ToggleResult::handled,
        "primary pointer press arms activation"
    );
    require(control.state().pressed, "pointer press is visible in controller state");

    const auto release = control.pointer_up(1, true, true);
    require(
        release.result == veyra::ToggleResult::changed &&
            release.value == veyra::ToggleValue::checked &&
            control.value() == veyra::ToggleValue::checked,
        "release inside commits the toggle in uncontrolled mode"
    );

    control.pointer_down(1, true);
    const auto cancel = control.pointer_up(1, true, false);
    require(
        cancel.result == veyra::ToggleResult::handled &&
            control.value() == veyra::ToggleValue::checked,
        "release outside cancels without changing the value"
    );

    const auto second = control.invoke();
    require(
        second.result == veyra::ToggleResult::changed &&
            second.value == veyra::ToggleValue::unchecked &&
            control.value() == veyra::ToggleValue::unchecked,
        "programmatic invocation toggles back off"
    );
}

void check_keyboard_and_focus_loss() {
    auto control = veyra::ToggleController{};
    require(
        control.key_down(veyra::ToggleKey::space).result == veyra::ToggleResult::ignored,
        "unfocused controls ignore keyboard input"
    );
    require(
        control.set_focused(true) == veyra::ToggleResult::handled,
        "focus can be reflected into the controller"
    );

    require(
        control.key_down(veyra::ToggleKey::space).result == veyra::ToggleResult::handled &&
            control.state().pressed,
        "Space key-down arms without toggling"
    );
    require(
        control.key_down(veyra::ToggleKey::space).result == veyra::ToggleResult::handled &&
            control.value() == veyra::ToggleValue::unchecked,
        "repeated Space key-down does not toggle"
    );
    const auto release = control.key_up(veyra::ToggleKey::space);
    require(
        release.result == veyra::ToggleResult::changed &&
            release.value == veyra::ToggleValue::checked,
        "Space toggles on release"
    );

    const auto enter = control.key_down(veyra::ToggleKey::enter);
    require(
        enter.result == veyra::ToggleResult::changed &&
            enter.value == veyra::ToggleValue::unchecked,
        "Enter toggles on key-down"
    );
    require(
        control.key_up(veyra::ToggleKey::enter).result == veyra::ToggleResult::handled,
        "Enter release only clears its armed state"
    );

    control.key_down(veyra::ToggleKey::space);
    require(
        control.set_focused(false) == veyra::ToggleResult::handled,
        "focus loss updates controller state"
    );
    require(
        control.key_up(veyra::ToggleKey::space).result == veyra::ToggleResult::ignored,
        "focus loss cancels an armed key"
    );
}

void check_controlled_mode_and_migration() {
    auto control = veyra::ToggleController{};
    require(
        control.configure(
            {.mode = veyra::ToggleMode::controlled, .value = veyra::ToggleValue::unchecked}
        ),
        "a control can be configured for controlled ownership"
    );

    const auto intent = control.invoke();
    require(
        intent.result == veyra::ToggleResult::changed &&
            intent.value == veyra::ToggleValue::checked &&
            control.value() == veyra::ToggleValue::unchecked,
        "controlled activation emits intent without committing"
    );

    const auto pushed = control.set_controlled_value(veyra::ToggleValue::checked);
    require(
        pushed.result == veyra::ToggleResult::changed &&
            control.value() == veyra::ToggleValue::checked,
        "the owner commits by pushing the authoritative value"
    );
    require(
        control.set_controlled_value(veyra::ToggleValue::checked).result ==
            veyra::ToggleResult::handled,
        "pushing the same value reports no change"
    );
    require(
        control.request_value(veyra::ToggleValue::checked).result == veyra::ToggleResult::handled,
        "requesting the current value reports no change"
    );

    auto owned = veyra::ToggleController{};
    require(
        owned.set_controlled_value(veyra::ToggleValue::checked).result ==
            veyra::ToggleResult::ignored,
        "controlled writes are rejected in uncontrolled mode"
    );
    require(
        !owned.migrate_mode(veyra::ToggleMode::uncontrolled, owned.value()),
        "migrating nowhere reports no change"
    );
    require(
        owned.migrate_mode(veyra::ToggleMode::controlled, veyra::ToggleValue::checked),
        "an explicit migration call switches ownership"
    );
    require(
        owned.set_controlled_value(veyra::ToggleValue::unchecked).result ==
            veyra::ToggleResult::changed,
        "controlled writes work after migration"
    );
}

void check_mixed_opt_in_and_collapse() {
    auto control = veyra::ToggleController{};
    control.request_value(veyra::ToggleValue::checked);
    require(
        control.configure({.value = veyra::ToggleValue::mixed}),
        "configuration normalizes an unrequested mixed value"
    );
    require(
        control.value() == veyra::ToggleValue::unchecked,
        "mixed without opt-in starts unchecked"
    );
    require(
        control.request_value(veyra::ToggleValue::mixed).result == veyra::ToggleResult::handled &&
            control.value() == veyra::ToggleValue::unchecked,
        "mixed requests collapse while opted out"
    );

    require(control.set_allow_mixed(true), "mixed support can be enabled");
    require(
        control.request_value(veyra::ToggleValue::mixed).result == veyra::ToggleResult::changed &&
            control.value() == veyra::ToggleValue::mixed,
        "mixed commits once allowed"
    );
    const auto activated = control.invoke();
    require(
        activated.result == veyra::ToggleResult::changed &&
            activated.value == veyra::ToggleValue::checked,
        "activation from mixed resolves to checked"
    );

    control.request_value(veyra::ToggleValue::mixed);
    require(control.set_allow_mixed(false), "mixed support can be revoked");
    require(
        control.value() == veyra::ToggleValue::checked,
        "revoking mixed support settles on checked"
    );
}

void check_hover_capture_and_keyboard_edges() {
    auto control = veyra::ToggleController{};
    require(
        control.pointer_leave() == veyra::ToggleResult::ignored,
        "leaving without hover reports no change"
    );
    require(
        control.pointer_enter() == veyra::ToggleResult::handled &&
            control.pointer_leave() == veyra::ToggleResult::handled && !control.state().hovered,
        "a hovered control clears hover on leave"
    );
    require(
        control.pointer_down(1, false) == veyra::ToggleResult::ignored,
        "non-primary pointer presses are ignored"
    );

    control.pointer_down(1, true);
    require(
        control.pointer_cancel(99) == veyra::ToggleResult::ignored && control.state().pressed,
        "cancellation for a foreign pointer is ignored and keeps the press"
    );
    require(
        control.pointer_cancel(1) == veyra::ToggleResult::handled && !control.state().pressed,
        "cancellation by the owner clears the press without toggling"
    );
    require(control.value() == veyra::ToggleValue::unchecked, "cancelled press keeps the value");

    control.set_focused(true);
    const auto enter = control.key_down(veyra::ToggleKey::enter);
    require(enter.result == veyra::ToggleResult::changed, "Enter toggles on the first key-down");
    require(
        control.key_down(veyra::ToggleKey::enter).result == veyra::ToggleResult::handled,
        "a repeated Enter key-down does not toggle again"
    );
    require(
        control.key_up(veyra::ToggleKey::enter).result == veyra::ToggleResult::handled,
        "Enter release clears its armed state"
    );

    auto untouched = veyra::ToggleController{};
    untouched.set_focused(true);
    require(
        untouched.key_up(veyra::ToggleKey::enter).result == veyra::ToggleResult::ignored,
        "Enter release without an armed press is ignored"
    );

    auto idle = veyra::ToggleController{};
    require(
        idle.request_value(veyra::ToggleValue::checked).result == veyra::ToggleResult::changed,
        "requests commit while enabled"
    );
    require(idle.set_disabled(true), "disabling reports a state change");
    require(
        idle.request_value(veyra::ToggleValue::unchecked).result == veyra::ToggleResult::ignored &&
            idle.value() == veyra::ToggleValue::checked,
        "value requests are ignored while disabled"
    );
    require(
        idle.pointer_enter() == veyra::ToggleResult::ignored && !idle.state().hovered,
        "a disabled toggle ignores hover"
    );

    auto kinded = veyra::ToggleController{};
    require(kinded.set_kind(veyra::ToggleKind::toggle_switch), "the widget kind can switch");
    require(kinded.kind() == veyra::ToggleKind::toggle_switch, "the configured kind reads back");
    require(
        !kinded.set_kind(veyra::ToggleKind::toggle_switch),
        "repeating the widget kind reports no change"
    );

    auto opted = veyra::ToggleController{};
    require(
        opted.configure({.allow_mixed = true}),
        "configuration alone can opt into mixed values"
    );
    require(opted.mixed_allowed(), "the mixed opt-in reads back");
    require(
        opted.request_value(veyra::ToggleValue::mixed).result == veyra::ToggleResult::changed,
        "mixed commits once configured in"
    );
}

void check_disabled_during_armed_and_semantics() {
    auto control = veyra::ToggleController{};
    control.set_focused(true);
    control.pointer_down(1, true);
    control.key_down(veyra::ToggleKey::space);
    require(control.set_disabled(true), "disabling reports a state change");
    require(
        control.state().disabled && !control.state().pressed && !control.state().focused,
        "disabling clears transient armed state"
    );
    require(
        control.pointer_up(1, true, true).result == veyra::ToggleResult::ignored &&
            control.key_up(veyra::ToggleKey::space).result == veyra::ToggleResult::ignored &&
            control.invoke().result == veyra::ToggleResult::ignored,
        "armed inputs cannot activate while disabled"
    );

    auto semantics = veyra::SemanticNodeData{
        .role = veyra::SemanticRole::text,
        .label = "unchanged",
    };
    require(!control.write_semantics({}, semantics), "an empty accessible name is rejected");
    require(
        semantics.role == veyra::SemanticRole::text && semantics.label == "unchanged",
        "invalid semantic output stays unchanged"
    );
    require(control.write_semantics("Alerts", semantics), "a named toggle writes semantics");
    require(
        semantics.role == veyra::SemanticRole::checkbox && semantics.label == "Alerts" &&
            veyra::has_state(semantics.states, veyra::SemanticState::disabled) &&
            !veyra::has_action(semantics.actions, veyra::SemanticAction::activate),
        "disabled toggle semantics expose the name and remove activation"
    );

    require(control.set_disabled(false), "enabling reports a state change");
    control.configure(
        {.kind = veyra::ToggleKind::toggle_switch, .value = veyra::ToggleValue::mixed}
    );
    require(!control.mixed_allowed(), "configure resets the mixed opt-in");
    control.set_allow_mixed(true);
    control.request_value(veyra::ToggleValue::mixed);
    require(
        control.write_semantics("Alerts", semantics) &&
            semantics.role == veyra::SemanticRole::switch_ &&
            veyra::has_state(semantics.states, veyra::SemanticState::checked) &&
            veyra::has_state(semantics.states, veyra::SemanticState::mixed) &&
            veyra::has_action(semantics.actions, veyra::SemanticAction::activate),
        "a mixed switch exposes checked, mixed, and activation"
    );
}

}

int main() {
    try {
        check_uncontrolled_pointer_toggle();
        check_keyboard_and_focus_loss();
        check_controlled_mode_and_migration();
        check_mixed_opt_in_and_collapse();
        check_hover_capture_and_keyboard_edges();
        check_disabled_during_armed_and_semantics();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
