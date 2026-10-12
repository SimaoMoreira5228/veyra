#include "veyra/behaviors/pressable.hpp"
#include "veyra/core/node_tree.hpp"

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
        control.pointer_cancel(99) == veyra::PressableResult::ignored && control.state().pressed,
        "cancellation for a foreign pointer is ignored and keeps the press"
    );
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
        control.set_focused(true) == veyra::PressableResult::handled,
        "focus can be reflected into the controller"
    );
    require(
        control.key_up(veyra::PressableKey::enter) == veyra::PressableResult::ignored,
        "Enter release without an armed press is ignored"
    );
    control.set_focused(false);
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
        control.pointer_enter() == veyra::PressableResult::ignored && !control.state().hovered,
        "a disabled control ignores hover"
    );
    require(
        control.pointer_leave() == veyra::PressableResult::ignored,
        "leaving without hover reports no change"
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

void check_tint_animator_follows_state() {
    auto control = veyra::PressableController{};
    auto tint_anim = veyra::PressableTintAnimator{};

    require(!tint_anim.tick(control.state(), 0), "an idle control ticks to zero tint");
    const auto idle = tint_anim.tint();
    require(idle.hover == 0.0F && idle.press == 0.0F, "a fresh tint animator rests at zero");

    control.pointer_enter();
    require(!tint_anim.tick(control.state(), 0), "retargeting alone changes nothing yet");
    require(tint_anim.tick(control.state(), 60), "the hover leg reports progress");
    tint_anim.tick(control.state(), 60);
    require(
        tint_anim.tint().hover > 0.0F && tint_anim.tint().hover < 1.0F,
        "hover tint advances partway"
    );

    control.pointer_down(1, true);
    tint_anim.tick(control.state(), 60);
    tint_anim.tick(control.state(), 1000);
    require(!tint_anim.tick(control.state(), 1001), "settled ticks report no change");
    const auto settled = tint_anim.tint();
    require(
        settled.hover == 1.0F && settled.press == 1.0F,
        "hover and press tints settle at full strength"
    );
    require(!tint_anim.running(), "settled tints stop running");

    control.pointer_up(1, true, true);
    control.pointer_leave();
    tint_anim.tick(control.state(), 1100);
    tint_anim.tick(control.state(), 1150);
    const auto released = tint_anim.tint();
    require(
        released.hover < 1.0F && released.press < 1.0F,
        "release and leave retarget both legs down"
    );
    tint_anim.tick(control.state(), 2000);
    const auto rest = tint_anim.tint();
    require(rest.hover == 0.0F && rest.press == 0.0F, "both legs return to zero");
}

void check_tint_retarget_never_jumps() {
    auto control = veyra::PressableController{};
    auto tint_anim = veyra::PressableTintAnimator{};

    control.pointer_enter();
    tint_anim.tick(control.state(), 0);
    tint_anim.tick(control.state(), 60);
    const auto mid_hover = tint_anim.tint().hover;
    require(mid_hover > 0.0F && mid_hover < 1.0F, "hover tint advances partway");

    control.pointer_leave();
    tint_anim.tick(control.state(), 60);
    require(
        tint_anim.tint().hover == mid_hover,
        "leaving mid-flight resumes from the current tint"
    );
}

void check_tint_respects_reduced_motion_and_throttle() {
    auto control = veyra::PressableController{};
    auto tint_anim = veyra::PressableTintAnimator{};
    tint_anim.set_reduced_motion(true);

    control.pointer_enter();
    tint_anim.tick(control.state(), 0);
    tint_anim.tick(control.state(), 5000);
    require(tint_anim.tint().hover == 0.0F, "reduced motion holds the tint at its start value");

    auto offscreen = veyra::PressableTintAnimator{};
    offscreen.set_throttled(true);
    offscreen.tick(control.state(), 0);
    offscreen.tick(control.state(), 5000);
    require(offscreen.tint().hover == 0.0F, "a throttled tick contributes zero progress");
    offscreen.set_throttled(false);
    offscreen.tick(control.state(), 5120);
    require(offscreen.tint().hover == 1.0F, "unthrottling resumes the hover leg");
}

void check_tint_classification_avoids_layout() {
    require(
        veyra::PressableTintAnimator::animated_property() == veyra::AnimatedProperty::color,
        "pressable tint animates color"
    );
    require(
        veyra::invalidation_for(veyra::PressableTintAnimator::animated_property()) ==
            veyra::AnimationInvalidation::paint,
        "pressable tint repaints without layout"
    );

    auto tree = veyra::NodeTree{};
    const auto root = tree.create();
    const auto child = tree.create();
    require(tree.append_child(root, child), "test nodes link");

    tree.clear_dirty(child, veyra::DirtyCategory::structure);
    tree.clear_dirty(child, veyra::DirtyCategory::layout);
    tree.clear_dirty(child, veyra::DirtyCategory::paint);

    require(
        !veyra::mark_animation_dirty(tree, child, veyra::AnimatedProperty::opacity),
        "a compositor opacity tick invalidates nothing"
    );
    require(
        !tree.is_dirty(child, veyra::DirtyCategory::layout),
        "the compositor path triggers zero layout invalidation"
    );
    require(
        !tree.is_dirty(child, veyra::DirtyCategory::paint),
        "the compositor path repaints nothing"
    );

    require(
        veyra::mark_animation_dirty(tree, child, veyra::PressableTintAnimator::animated_property()),
        "a tint tick marks paint"
    );
    require(tree.is_dirty(child, veyra::DirtyCategory::paint), "tint marks the paint flag");
    require(!tree.is_dirty(child, veyra::DirtyCategory::layout), "tint pays no layout");
}

}

int main() {
    try {
        check_pointer_activation_and_cancellation();
        check_keyboard_activation_and_focus_loss();
        check_disabled_state_and_semantics();
        check_tint_animator_follows_state();
        check_tint_retarget_never_jumps();
        check_tint_respects_reduced_motion_and_throttle();
        check_tint_classification_avoids_layout();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
