#include "veyra/behaviors/pressable.hpp"

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

void check_pointer_activation_and_cancellation() {
    auto control = veyra::PressableController{};
    require(
        control.pointer_down(1, false) == veyra::PressableResult::ignored,
        "non-primary pointer presses are ignored"
    );
    require(
        control.pointer_enter() == veyra::PressableResult::handled,
        "pointer entry marks the control hovered"
    );
    require(
        control.pointer_down(1, true) == veyra::PressableResult::handled,
        "primary pointer press arms activation"
    );
    require(control.state().pressed, "pointer press is visible in controller state");
    require(
        control.pointer_leave() == veyra::PressableResult::handled && control.state().pressed,
        "pointer capture preserves press state outside the hit region"
    );
    require(
        control.pointer_up(1, true, true) == veyra::PressableResult::activated,
        "release inside the hit region activates"
    );
    require(!control.state().pressed, "activation clears pointer press state");

    control.pointer_down(1, true);
    require(
        control.pointer_up(1, true, false) == veyra::PressableResult::handled,
        "release outside cancels activation"
    );
    control.pointer_down(1, true);
    require(
        control.pointer_cancel(1) == veyra::PressableResult::handled && !control.state().pressed,
        "pointer cancellation clears press state"
    );

    control.pointer_down(1, true);
    require(
        control.pointer_down(2, true) == veyra::PressableResult::ignored &&
            control.pointer_up(2, true, true) == veyra::PressableResult::ignored,
        "another pointer cannot take over an active press"
    );
    require(
        control.pointer_up(1, true, true) == veyra::PressableResult::activated,
        "the captured pointer can complete its activation"
    );
}

void check_keyboard_activation_and_focus_loss() {
    auto control = veyra::PressableController{};
    require(
        control.key_down(veyra::PressableKey::space) == veyra::PressableResult::ignored,
        "unfocused controls ignore keyboard activation"
    );
    require(
        control.set_focused(true) == veyra::PressableResult::handled,
        "focus can be reflected into the controller"
    );
    require(
        control.key_down(veyra::PressableKey::space) == veyra::PressableResult::handled,
        "Space key-down arms activation"
    );
    require(control.state().pressed, "armed key is shown as pressed");
    require(
        control.key_down(veyra::PressableKey::space) == veyra::PressableResult::handled,
        "repeated key-down does not activate"
    );
    require(
        control.key_up(veyra::PressableKey::space) == veyra::PressableResult::activated,
        "Space activates on release"
    );

    require(
        control.key_down(veyra::PressableKey::enter) == veyra::PressableResult::activated,
        "Enter activates on key-down"
    );
    require(
        control.key_down(veyra::PressableKey::enter) == veyra::PressableResult::handled,
        "repeated Enter key-down does not activate again"
    );
    require(
        control.key_up(veyra::PressableKey::enter) == veyra::PressableResult::handled,
        "Enter release clears its pressed state"
    );

    control.key_down(veyra::PressableKey::space);
    require(
        control.set_focused(false) == veyra::PressableResult::handled,
        "focus loss updates controller state"
    );
    require(
        control.key_up(veyra::PressableKey::space) == veyra::PressableResult::ignored,
        "focus loss cancels an armed activation"
    );
}

void check_disabled_state_and_semantics() {
    auto control = veyra::PressableController{};
    control.pointer_enter();
    control.pointer_down(1, true);
    control.set_focused(true);
    require(control.set_disabled(true), "disabling reports a state change");
    require(
        control.state().disabled && !control.state().hovered && !control.state().pressed &&
            !control.state().focused,
        "disabling clears transient state"
    );
    require(
        control.pointer_up(1, true, true) == veyra::PressableResult::ignored,
        "a disabled control cannot activate from a pointer release"
    );
    require(
        control.invoke() == veyra::PressableResult::ignored,
        "a disabled control rejects accessible invocation"
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
    require(control.write_semantics("Save", semantics), "a named control writes button semantics");
    require(
        semantics.role == veyra::SemanticRole::button && semantics.label == "Save" &&
            veyra::has_state(semantics.states, veyra::SemanticState::disabled) &&
            !veyra::has_action(semantics.actions, veyra::SemanticAction::activate),
        "disabled button semantics expose the name and remove activation"
    );

    require(control.set_disabled(false), "enabling reports a state change");
    require(
        control.write_semantics("Save", semantics) &&
            veyra::has_action(semantics.actions, veyra::SemanticAction::activate) &&
            !veyra::has_state(semantics.states, veyra::SemanticState::disabled),
        "enabled button semantics expose activation"
    );
    require(
        control.invoke() == veyra::PressableResult::activated,
        "accessible invocation requests activation"
    );
}

}

int main() {
    try {
        check_pointer_activation_and_cancellation();
        check_keyboard_activation_and_focus_loss();
        check_disabled_state_and_semantics();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
