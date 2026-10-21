#include "veyra/behaviors/overlay.hpp"
#include "veyra/behaviors/pressable.hpp"
#include "veyra/core/focus_navigation.hpp"
#include "veyra/widgets/overlay/popover.hpp"

#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

struct Fixture {
    veyra::NodeTree tree;
    veyra::NodeId page;
    veyra::NodeId background;
    veyra::NodeId restore;
    veyra::NodeId dialog;
    veyra::NodeId default_button;
    veyra::NodeId cancel_button;
};

Fixture make_fixture() {
    Fixture fixture;
    fixture.tree.reserve(8);

    fixture.page = fixture.tree.create();
    fixture.background = fixture.tree.create();
    fixture.restore = fixture.tree.create();
    fixture.dialog = fixture.tree.create();
    fixture.default_button = fixture.tree.create();
    fixture.cancel_button = fixture.tree.create();

    require(fixture.tree.append_child(fixture.page, fixture.background), "background attaches");
    require(fixture.tree.append_child(fixture.page, fixture.restore), "restore node attaches");
    require(fixture.tree.append_child(fixture.page, fixture.dialog), "dialog attaches");
    require(fixture.tree.append_child(fixture.dialog, fixture.default_button), "default attaches");
    require(fixture.tree.append_child(fixture.dialog, fixture.cancel_button), "cancel attaches");

    require(fixture.tree.set_focusable(fixture.background, true), "background is focusable");
    require(fixture.tree.set_focusable(fixture.restore, true), "restore node is focusable");
    require(fixture.tree.set_focusable(fixture.default_button, true), "default is focusable");
    require(fixture.tree.set_focusable(fixture.cancel_button, true), "cancel is focusable");

    return fixture;
}

veyra::OverlayController open_modal(Fixture& fixture) {
    veyra::OverlayController overlays;

    const auto opened = overlays.open({
        .dialog = fixture.dialog,
        .restore_focus = fixture.restore,
    });

    require(opened == veyra::OverlayResult::opened, "modal dialog opens");
    require(overlays.trap_root() == fixture.dialog, "trap root is the open dialog");

    return overlays;
}

void check_modal_chrome_layers_scrim() {
    const auto theme = veyra::Theme{};
    const veyra::PopoverGeometry geometry{
        .panel = {.x = 40.0F, .y = 60.0F, .width = 120.0F, .height = 80.0F},
        .viewport = {.x = 0.0F, .y = 0.0F, .width = 200.0F, .height = 200.0F},
        .modal = true,
    };
    std::array<veyra::DisplayCommand, 4> commands{};

    const auto output = veyra::emit_popover(theme, geometry, commands);

    require(output.result == veyra::PopoverEmitResult::complete, "modal chrome emits");
    require(output.placed == 3 && output.required == 3, "modal chrome is scrim plus panel");

    const auto* scrim = std::get_if<veyra::FillRect>(&commands[0]);
    require(scrim != nullptr, "scrim leads under the panel");
    require(scrim->color.alpha == 128, "scrim is translucent");

    const auto* panel = std::get_if<veyra::FillRect>(&commands[1]);
    require(panel != nullptr, "panel sits above the scrim");
    require(
        panel->bounds.x == 40.0F && panel->bounds.width == 120.0F,
        "panel pins the dialog rect"
    );
}

void check_modal_lifecycle_traps_focus_and_restores() {
    auto fixture = make_fixture();
    auto overlays = open_modal(fixture);

    veyra::SemanticNodeData dialog_data{};
    require(overlays.write_dialog_semantics("Save changes", dialog_data), "semantics fills");
    require(
        dialog_data.role == veyra::SemanticRole::dialog &&
            veyra::has_state(dialog_data.states, veyra::SemanticState::modal),
        "open modal reads modal"
    );

    check_modal_chrome_layers_scrim();

    const auto trap = overlays.trap_root();

    auto move = veyra::move_focus(fixture.tree, trap, {}, veyra::FocusDirection::forward);
    require(move.result == veyra::FocusMoveResult::moved, "tab enters the trap");
    require(move.target == fixture.default_button, "first tab lands on default");

    move = veyra::move_focus(fixture.tree, trap, move.target, veyra::FocusDirection::forward);
    require(move.result == veyra::FocusMoveResult::moved, "tab advances inside the trap");
    require(move.target == fixture.cancel_button, "second tab lands on cancel");

    move = veyra::move_focus(fixture.tree, trap, move.target, veyra::FocusDirection::forward);
    require(move.result == veyra::FocusMoveResult::moved, "tab wraps inside the trap");
    require(move.target == fixture.default_button, "wrap returns to default, never background");

    move = veyra::move_focus(
        fixture.tree,
        trap,
        fixture.default_button,
        veyra::FocusDirection::backward
    );
    require(move.target == fixture.cancel_button, "shift-tab cycles back to cancel");

    const auto background_move =
        veyra::move_focus(fixture.tree, trap, fixture.background, veyra::FocusDirection::forward);
    require(
        background_move.result == veyra::FocusMoveResult::invalid_current,
        "trap refuses focus parked outside the dialog"
    );

    const auto restore_move =
        veyra::move_focus(fixture.tree, trap, fixture.restore, veyra::FocusDirection::forward);
    require(
        restore_move.result == veyra::FocusMoveResult::invalid_current,
        "trap refuses the restore node while open"
    );

    const auto modal_outside = overlays.outside_press();
    require(
        modal_outside.result == veyra::OverlayResult::handled,
        "outside press on a modal is swallowed"
    );
    require(overlays.depth() == 1, "swallowed outside press leaves the stack alone");

    const auto escape = overlays.escape();
    require(escape.result == veyra::OverlayResult::cancel, "escape reports cancel");
    require(escape.dialog == fixture.dialog, "cancel names the open dialog");
    require(overlays.depth() == 1, "escape reports intent without popping");

    const auto closed = overlays.close_top();
    require(closed.result == veyra::OverlayResult::closed, "close pops the dialog");
    require(closed.dialog == fixture.dialog, "close reports the dialog");
    require(closed.restore_focus == fixture.restore, "close returns the restore id");
    require(overlays.empty(), "stack is empty after close");
    require(overlays.trap_root().is_null(), "trap clears with the stack");

    require(fixture.tree.contains(closed.restore_focus), "restore id still resolves");
    require(fixture.tree.is_focusable(closed.restore_focus), "restore node stays focusable");

    const auto theme = veyra::Theme{};
    const veyra::PopoverGeometry popup{
        .panel = {.x = 40.0F, .y = 60.0F, .width = 120.0F, .height = 80.0F},
        .modal = false,
    };
    std::array<veyra::DisplayCommand, 4> commands{};
    const auto repaint = veyra::emit_popover(theme, popup, commands);
    require(repaint.placed == 2, "closed dialog repaints popover chrome, scrim gone");

    const auto* panel = std::get_if<veyra::FillRect>(&commands[0]);
    require(panel != nullptr, "panel leads once the scrim is gone");
    require(
        panel->color == theme.color(veyra::tokens::color_surface),
        "panel keeps the surface color"
    );
}

void check_default_activation_wires_intent_to_pressable() {
    auto fixture = make_fixture();
    auto overlays = open_modal(fixture);

    const auto intent = overlays.request_default();
    require(
        intent.result == veyra::OverlayResult::activate_default,
        "enter reports default-action intent"
    );
    require(intent.dialog == fixture.dialog, "default intent names the open dialog");
    require(overlays.depth() == 1, "default intent leaves the stack alone");

    veyra::PressableController default_button;
    veyra::PressableController cancel_button;

    require(
        default_button.set_focused(true) == veyra::PressableResult::handled,
        "default button holds focus"
    );
    require(
        default_button.key_down(veyra::PressableKey::enter) == veyra::PressableResult::activated,
        "enter on the focused default button activates"
    );
    require(!cancel_button.state().pressed, "cancel stays idle while default fires");

    const auto closed = overlays.close_top();
    require(closed.restore_focus == fixture.restore, "default path restores the opener");
}

void check_outside_press_modal_ignored_popover_dismisses() {
    auto fixture = make_fixture();
    veyra::OverlayController overlays;

    require(
        overlays.open({.dialog = fixture.dialog, .restore_focus = fixture.restore}) ==
            veyra::OverlayResult::opened,
        "modal dialog opens"
    );

    const auto modal_outside = overlays.outside_press();
    require(
        modal_outside.result == veyra::OverlayResult::handled,
        "outside press on a modal never closes"
    );
    require(modal_outside.dialog == fixture.dialog, "swallowed press names the dialog");
    require(overlays.depth() == 1, "modal survives the outside press");

    const auto modal_closed = overlays.close_top();
    require(modal_closed.restore_focus == fixture.restore, "modal close restores the opener");

    require(
        overlays.open({
            .dialog = fixture.dialog,
            .restore_focus = fixture.restore,
            .kind = veyra::OverlayKind::popover,
        }) == veyra::OverlayResult::opened,
        "popover opens"
    );

    const auto popover_outside = overlays.outside_press();
    require(
        popover_outside.result == veyra::OverlayResult::dismiss,
        "outside press on a popover dismisses"
    );
    require(popover_outside.dialog == fixture.dialog, "dismiss names the popover");
    require(overlays.depth() == 1, "dismiss reports intent without popping");

    const auto popover_closed = overlays.close_top();
    require(popover_closed.result == veyra::OverlayResult::closed, "popover close pops");
    require(popover_closed.kind == veyra::OverlayKind::popover, "close keeps the popover kind");
    require(popover_closed.restore_focus == fixture.restore, "popover close restores the opener");
    require(overlays.empty(), "stack is empty after popover close");
}

}

int main() {
    try {
        check_modal_lifecycle_traps_focus_and_restores();
        check_default_activation_wires_intent_to_pressable();
        check_outside_press_modal_ignored_popover_dismisses();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
