#include "veyra/behaviors/overlay/overlay.hpp"

#include <array>
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

struct Fixture {
    veyra::NodeTree tree;
    veyra::NodeId dialog_a;
    veyra::NodeId dialog_b;
    veyra::NodeId restore_a;
    veyra::NodeId restore_b;
};

Fixture make_fixture() {
    Fixture fixture;
    fixture.tree.reserve(5);

    const auto root = fixture.tree.create();
    fixture.dialog_a = fixture.tree.create();
    fixture.dialog_b = fixture.tree.create();
    fixture.restore_a = fixture.tree.create();
    fixture.restore_b = fixture.tree.create();
    require(fixture.tree.append_child(root, fixture.dialog_a), "dialog attaches");
    require(fixture.tree.append_child(root, fixture.dialog_b), "nested dialog attaches");
    require(fixture.tree.append_child(root, fixture.restore_a), "restore node attaches");
    require(fixture.tree.append_child(root, fixture.restore_b), "second restore node attaches");
    require(fixture.tree.set_focusable(fixture.restore_a, true), "restore node is focusable");
    require(
        fixture.tree.set_focusable(fixture.restore_b, true),
        "second restore node is focusable"
    );

    return fixture;
}

void check_open_close_reports_restore_target() {
    auto fixture = make_fixture();
    veyra::OverlayController overlays;

    require(overlays.empty(), "stack starts empty");
    require(overlays.trap_root().is_null(), "empty stack sets no trap");

    const auto opened = overlays.open({
        .dialog = fixture.dialog_a,
        .restore_focus = fixture.restore_a,
    });
    require(opened == veyra::OverlayResult::opened, "open pushes the dialog");
    require(!overlays.empty(), "stack holds the open dialog");
    require(overlays.depth() == 1, "depth tracks the open dialog");
    require(overlays.trap_root() == fixture.dialog_a, "trap root is the open dialog");
    require(overlays.contains(fixture.dialog_a), "open dialog is on the stack");

    const auto closed = overlays.close_top();
    require(closed.result == veyra::OverlayResult::closed, "close pops the dialog");
    require(closed.dialog == fixture.dialog_a, "close reports the dialog");
    require(closed.restore_focus == fixture.restore_a, "close reports the restore target");
    require(closed.kind == veyra::OverlayKind::modal, "dialogs open modal by default");
    require(overlays.empty(), "stack is empty after close");
    require(overlays.trap_root().is_null(), "trap clears with the stack");

    const auto empty_close = overlays.close_top();
    require(empty_close.result == veyra::OverlayResult::ignored, "close on empty stack is ignored");
    require(empty_close.dialog.is_null(), "empty close reports no dialog");
    require(empty_close.restore_focus.is_null(), "empty close reports no restore target");

    const auto empty_reset = overlays.close_all();
    require(empty_reset.closed == 0, "close all on empty stack closes nothing");
    require(empty_reset.restore_focus.is_null(), "empty reset reports no restore target");

    const auto null_open = overlays.open({});
    require(null_open == veyra::OverlayResult::ignored, "null dialog is rejected");
    require(overlays.empty(), "rejected open leaves the stack alone");
}

void check_trap_root_tracks_nesting() {
    auto fixture = make_fixture();
    veyra::OverlayController overlays;

    require(
        overlays.open({.dialog = fixture.dialog_a, .restore_focus = fixture.restore_a}) ==
            veyra::OverlayResult::opened,
        "first dialog opens"
    );
    require(
        overlays.open({.dialog = fixture.dialog_b, .restore_focus = fixture.restore_b}) ==
            veyra::OverlayResult::opened,
        "nested dialog opens"
    );
    require(overlays.depth() == 2, "nested dialogs stack");
    require(overlays.trap_root() == fixture.dialog_b, "trap root is always the top layer");

    const auto inner = overlays.close_top();
    require(inner.restore_focus == fixture.restore_b, "inner close restores inner focus");
    require(overlays.trap_root() == fixture.dialog_a, "trap falls back to the outer dialog");

    require(
        overlays.open({.dialog = fixture.dialog_b, .restore_focus = fixture.restore_b}) ==
            veyra::OverlayResult::opened,
        "nested dialog reopens"
    );
    const auto reset = overlays.close_all();
    require(reset.closed == 2, "close all pops every layer");
    require(
        reset.restore_focus == fixture.restore_a,
        "close all restores the focus predating every dialog"
    );
    require(overlays.empty(), "close all empties the stack");
}

void check_escape_policy_per_layer() {
    auto fixture = make_fixture();
    veyra::OverlayController overlays;

    const auto empty = overlays.escape();
    require(empty.result == veyra::OverlayResult::ignored, "escape on empty stack is ignored");

    require(
        overlays.open({.dialog = fixture.dialog_a, .restore_focus = fixture.restore_a}) ==
            veyra::OverlayResult::opened,
        "cancelable dialog opens"
    );
    const auto cancel = overlays.escape();
    require(cancel.result == veyra::OverlayResult::cancel, "escape reports cancel intent");
    require(cancel.dialog == fixture.dialog_a, "cancel names the top dialog");
    require(overlays.depth() == 1, "escape reports intent without popping");

    require(
        overlays.open({
            .dialog = fixture.dialog_b,
            .restore_focus = fixture.restore_b,
            .escape_cancels = false,
        }) == veyra::OverlayResult::opened,
        "non-cancelable dialog opens"
    );
    const auto swallowed = overlays.escape();
    require(
        swallowed.result == veyra::OverlayResult::handled,
        "escape on a non-cancelable layer is swallowed"
    );
    require(swallowed.dialog == fixture.dialog_b, "swallowed escape names the top dialog");
    require(overlays.depth() == 2, "swallowed escape leaves the stack alone");

    require(overlays.close_top().dialog == fixture.dialog_b, "top layer closes");
    const auto outer = overlays.escape();
    require(outer.result == veyra::OverlayResult::cancel, "outer policy applies after close");
}

void check_default_and_outside_intents() {
    auto fixture = make_fixture();
    veyra::OverlayController overlays;

    require(
        overlays.request_default().result == veyra::OverlayResult::ignored,
        "default on empty stack is ignored"
    );
    require(
        overlays.outside_press().result == veyra::OverlayResult::ignored,
        "outside press on empty stack is ignored"
    );

    require(
        overlays.open({.dialog = fixture.dialog_a, .restore_focus = fixture.restore_a}) ==
            veyra::OverlayResult::opened,
        "modal dialog opens"
    );
    const auto modal_default = overlays.request_default();
    require(
        modal_default.result == veyra::OverlayResult::activate_default,
        "default request reports default-action intent"
    );
    require(modal_default.dialog == fixture.dialog_a, "default intent names the top dialog");
    require(overlays.depth() == 1, "default intent leaves the stack alone");

    const auto modal_outside = overlays.outside_press();
    require(
        modal_outside.result == veyra::OverlayResult::handled,
        "outside press on a modal layer is swallowed"
    );
    require(overlays.depth() == 1, "swallowed outside press leaves the stack alone");

    require(
        overlays.open({
            .dialog = fixture.dialog_b,
            .restore_focus = fixture.restore_b,
            .escape_cancels = true,
            .kind = veyra::OverlayKind::popover,
        }) == veyra::OverlayResult::opened,
        "popover opens"
    );
    const auto popover_outside = overlays.outside_press();
    require(
        popover_outside.result == veyra::OverlayResult::dismiss,
        "outside press on a popover reports dismiss intent"
    );
    require(popover_outside.dialog == fixture.dialog_b, "dismiss names the popover");
    require(overlays.depth() == 2, "dismiss intent leaves the stack alone");
}

void check_overflow_at_capacity() {
    auto fixture = make_fixture();
    veyra::OverlayController overlays;

    for (std::size_t i = 0; i < veyra::OverlayController::kCapacity; ++i) {
        const auto result = overlays.open({
            .dialog = fixture.dialog_a,
            .restore_focus = fixture.restore_a,
        });
        require(result == veyra::OverlayResult::opened, "layers open up to capacity");
    }
    require(overlays.depth() == veyra::OverlayController::kCapacity, "stack fills to capacity");

    const auto full = overlays.open({
        .dialog = fixture.dialog_b,
        .restore_focus = fixture.restore_b,
    });
    require(full == veyra::OverlayResult::overflow, "open past capacity overflows");
    require(overlays.depth() == veyra::OverlayController::kCapacity, "overflow keeps the stack");
    require(overlays.trap_root() == fixture.dialog_a, "overflow keeps the trap root");
    require(!overlays.contains(fixture.dialog_b), "overflowed dialog never lands on the stack");
}

void check_dialog_semantics() {
    auto fixture = make_fixture();
    veyra::OverlayController overlays;
    veyra::SemanticNodeData output{};

    require(!overlays.write_dialog_semantics("Save", output), "empty stack writes no semantics");
    require(
        overlays.open({.dialog = fixture.dialog_a, .restore_focus = fixture.restore_a}) ==
            veyra::OverlayResult::opened,
        "modal dialog opens"
    );
    require(!overlays.write_dialog_semantics("", output), "empty name writes no semantics");

    require(overlays.write_dialog_semantics("Save changes", output), "dialog semantics write");
    require(output.role == veyra::SemanticRole::dialog, "dialog record uses the dialog role");
    require(
        veyra::has_state(output.states, veyra::SemanticState::modal),
        "modal layer sets the modal state"
    );
    require(
        veyra::has_action(output.actions, veyra::SemanticAction::dismiss),
        "cancelable dialog offers dismiss"
    );

    require(
        overlays.open({
            .dialog = fixture.dialog_b,
            .restore_focus = fixture.restore_b,
            .escape_cancels = false,
            .kind = veyra::OverlayKind::popover,
        }) == veyra::OverlayResult::opened,
        "non-cancelable popover opens"
    );
    require(overlays.write_dialog_semantics("Hints", output), "popover semantics write");
    require(
        !veyra::has_state(output.states, veyra::SemanticState::modal),
        "popover layer sets no modal state"
    );
    require(
        veyra::has_action(output.actions, veyra::SemanticAction::dismiss),
        "popover offers dismiss through outside press"
    );
}

void check_modal_state_passes_snapshot_validation() {
    veyra::NodeTree tree;
    tree.reserve(2);

    const auto root = tree.create();
    const auto dialog = tree.create();
    require(tree.append_child(root, dialog), "dialog attaches");

    std::array<veyra::SemanticNodeData, 2> data{};
    data[0] = {.role = veyra::SemanticRole::window};
    data[1] = {
        .role = veyra::SemanticRole::dialog,
        .label = "Save changes",
        .states = veyra::state_flag(veyra::SemanticState::modal),
        .actions = veyra::action_flag(veyra::SemanticAction::dismiss),
    };
    const std::array<veyra::LayoutRect, 2> bounds{
        veyra::LayoutRect{.width = 200.0F, .height = 200.0F},
        veyra::LayoutRect{.x = 10.0F, .y = 10.0F, .width = 80.0F, .height = 60.0F},
    };
    std::array<veyra::SemanticNode, 2> output{};
    std::size_t node_count = 0;

    const auto result =
        veyra::build_semantic_snapshot(tree, root, {}, data, bounds, output, node_count);
    require(result == veyra::SemanticSnapshotResult::complete, "modal state survives validation");
    require(node_count == 2, "dialog snapshot keeps both records");
    require(output[1].role == veyra::SemanticRole::dialog, "snapshot keeps the dialog role");
    require(
        veyra::has_state(output[1].states, veyra::SemanticState::modal),
        "snapshot keeps the modal state"
    );
}

}

int main() {
    try {
        check_open_close_reports_restore_target();
        check_trap_root_tracks_nesting();
        check_escape_policy_per_layer();
        check_default_and_outside_intents();
        check_overflow_at_capacity();
        check_dialog_semantics();
        check_modal_state_passes_snapshot_validation();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
