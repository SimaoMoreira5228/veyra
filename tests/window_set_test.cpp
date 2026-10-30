#include "veyra/app/window_set.hpp"
#include "veyra/core/tree/node_tree.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

struct CloseState {
    veyra::WindowId cancel{veyra::kInvalidWindowId};
    std::uint32_t calls{};
};

veyra::CloseDecision handle_close(void* user, const veyra::CloseRequest& request) {
    auto& state = *static_cast<CloseState*>(user);
    ++state.calls;

    if (request.window == state.cancel) {
        return veyra::CloseDecision::cancel;
    }

    return veyra::CloseDecision::allow;
}

void check_owner_child_visibility() {
    auto slots = std::array<veyra::WindowSlot, 8>{};
    auto windows = veyra::WindowSet(slots);

    auto owner = veyra::WindowId{};
    auto child = veyra::WindowId{};
    require(windows.create({}, owner) == veyra::WindowSetResult::ok, "owner creates");
    require(windows.create({.owner = owner}, child) == veyra::WindowSetResult::ok, "child creates");
    require(
        windows.show(child) == veyra::WindowSetResult::owner_not_visible,
        "child cannot show before its owner"
    );
    require(windows.show(owner) == veyra::WindowSetResult::ok, "owner shows");
    require(windows.show(child) == veyra::WindowSetResult::ok, "child shows after its owner");
    require(windows.show(child) == veyra::WindowSetResult::already_shown, "a top level shows once");

    auto child_info = veyra::WindowInfo{};
    require(windows.info(owner, child_info), "owner info is available");
    require(child_info.child_count == 1, "owner reports its child");
    require(!windows.info(veyra::kInvalidWindowId, child_info), "invalid info is rejected");
}

void check_cancelled_owner_close() {
    auto slots = std::array<veyra::WindowSlot, 8>{};
    auto state = CloseState{};
    auto windows =
        veyra::WindowSet(slots, veyra::ShutdownMode::on_last_window_close, handle_close, &state);

    auto owner = veyra::WindowId{};
    auto child = veyra::WindowId{};
    require(windows.create({}, owner) == veyra::WindowSetResult::ok, "owner creates");
    require(windows.create({.owner = owner}, child) == veyra::WindowSetResult::ok, "child creates");
    require(windows.show(owner) == veyra::WindowSetResult::ok, "owner shows");
    require(windows.show(child) == veyra::WindowSetResult::ok, "child shows");

    state.cancel = child;
    require(
        windows.request_close(owner, veyra::CloseReason::user) == veyra::WindowSetResult::cancelled,
        "a child cancels the owner close"
    );
    require(
        windows.is_open(owner) && windows.is_open(child),
        "cancelled close keeps the tree open"
    );

    state.cancel = veyra::kInvalidWindowId;
    require(
        windows.request_close(owner, veyra::CloseReason::user) == veyra::WindowSetResult::ok,
        "owner close succeeds when no child cancels"
    );
    require(windows.open_count() == 0, "owner close closes the child");
    require(windows.should_shutdown(), "last close requests shutdown");
}

void check_modal_dialog_result() {
    auto slots = std::array<veyra::WindowSlot, 8>{};
    auto windows = veyra::WindowSet(slots);

    auto owner = veyra::WindowId{};
    auto other = veyra::WindowId{};
    auto dialog = veyra::WindowId{};
    require(windows.create({}, owner) == veyra::WindowSetResult::ok, "owner creates");
    require(windows.create({}, other) == veyra::WindowSetResult::ok, "other creates");
    require(
        windows.create({.owner = owner, .dialog = true, .modal = true}, dialog) ==
            veyra::WindowSetResult::ok,
        "modal dialog creates"
    );
    require(
        windows.create({.dialog = true, .modal = true}, dialog) ==
            veyra::WindowSetResult::invalid_owner,
        "modal dialog requires an owner"
    );
    require(windows.show(owner) == veyra::WindowSetResult::ok, "owner shows");
    require(windows.show(dialog) == veyra::WindowSetResult::ok, "dialog shows");

    require(windows.blocks_interaction(owner), "modal dialog blocks its owner");
    require(!windows.blocks_interaction(other), "modal dialog does not block another window");
    require(
        windows.close_with_result(dialog, true, 42) == veyra::WindowSetResult::ok,
        "dialog result closes the dialog"
    );
    require(!windows.is_open(dialog), "closed dialog is not open");
    require(!windows.blocks_interaction(owner), "closed dialog releases its owner");

    auto outcome = veyra::DialogOutcome{};
    require(
        windows.take_dialog_outcome(dialog, outcome) == veyra::WindowSetResult::ok,
        "outcome is available"
    );
    require(outcome.accepted && outcome.value == 42, "outcome carries the result");
    require(
        windows.take_dialog_outcome(dialog, outcome) == veyra::WindowSetResult::invalid_state,
        "outcome is consumed once"
    );
}

void check_owner_close_cancels_dialog() {
    auto slots = std::array<veyra::WindowSlot, 8>{};
    auto windows = veyra::WindowSet(slots);

    auto owner = veyra::WindowId{};
    auto dialog = veyra::WindowId{};
    require(windows.create({}, owner) == veyra::WindowSetResult::ok, "owner creates");
    require(
        windows.create({.owner = owner, .dialog = true, .modal = true}, dialog) ==
            veyra::WindowSetResult::ok,
        "dialog creates"
    );
    require(windows.show(owner) == veyra::WindowSetResult::ok, "owner shows");
    require(windows.show(dialog) == veyra::WindowSetResult::ok, "dialog shows");
    require(
        windows.request_close(owner, veyra::CloseReason::application) == veyra::WindowSetResult::ok,
        "owner closes with its dialog"
    );

    auto outcome = veyra::DialogOutcome{};
    require(
        windows.take_dialog_outcome(dialog, outcome) == veyra::WindowSetResult::ok,
        "cancelled dialog has an outcome"
    );
    require(!outcome.accepted, "owner close cancels the dialog");
}

void check_shutdown_modes() {
    auto slots = std::array<veyra::WindowSlot, 8>{};
    auto windows = veyra::WindowSet(slots, veyra::ShutdownMode::on_main_window_close);
    auto main = veyra::WindowId{};
    auto other = veyra::WindowId{};
    require(windows.create({}, main) == veyra::WindowSetResult::ok, "main creates");
    require(windows.create({}, other) == veyra::WindowSetResult::ok, "other creates");
    require(windows.show(main) == veyra::WindowSetResult::ok, "main shows");
    require(windows.show(other) == veyra::WindowSetResult::ok, "other shows");
    require(windows.set_main_window(main) == veyra::WindowSetResult::ok, "main window is recorded");
    require(!windows.should_shutdown(), "main window keeps the app alive");
    require(
        windows.request_close(other, veyra::CloseReason::user) == veyra::WindowSetResult::ok,
        "other closes"
    );
    require(!windows.should_shutdown(), "closing other does not shut down");
    require(
        windows.request_close(main, veyra::CloseReason::user) == veyra::WindowSetResult::ok,
        "main closes"
    );
    require(windows.should_shutdown(), "closing main requests shutdown");

    auto explicit_slots = std::array<veyra::WindowSlot, 2>{};
    auto explicit_windows =
        veyra::WindowSet(explicit_slots, veyra::ShutdownMode::on_explicit_shutdown);
    auto explicit_window = veyra::WindowId{};
    require(
        explicit_windows.create({}, explicit_window) == veyra::WindowSetResult::ok,
        "explicit window creates"
    );
    require(
        explicit_windows.show(explicit_window) == veyra::WindowSetResult::ok,
        "explicit window shows"
    );
    require(
        explicit_windows.request_close(explicit_window, veyra::CloseReason::user) ==
            veyra::WindowSetResult::ok,
        "explicit window closes"
    );
    require(!explicit_windows.should_shutdown(), "explicit shutdown ignores the last close");
}

void check_capacity_and_stale_ids() {
    auto slots = std::array<veyra::WindowSlot, 1>{};
    auto windows = veyra::WindowSet(slots);
    auto first = veyra::WindowId{};
    auto second = veyra::WindowId{};
    require(windows.create({}, first) == veyra::WindowSetResult::ok, "first window creates");
    require(windows.create({}, second) == veyra::WindowSetResult::capacity, "capacity is enforced");
    require(windows.destroy(first) == veyra::WindowSetResult::ok, "window destroys");
    require(windows.create({}, second) == veyra::WindowSetResult::ok, "slot is reusable");
    require(windows.is_open(second), "reused slot is open");
    require(!windows.is_open(first), "old id is not open");
}

void check_window_contexts() {
    auto slots = std::array<veyra::WindowSlot, 4>{};
    auto windows = veyra::WindowSet(slots);

    auto first = veyra::WindowId{};
    auto second = veyra::WindowId{};
    require(windows.create({}, first) == veyra::WindowSetResult::ok, "first context creates");
    require(windows.create({}, second) == veyra::WindowSetResult::ok, "second context creates");
    require(windows.show(first) == veyra::WindowSetResult::ok, "first context shows");
    require(windows.show(second) == veyra::WindowSetResult::ok, "second context shows");

    auto* first_theme = windows.theme(first);
    auto* second_theme = windows.theme(second);
    require(first_theme != nullptr && second_theme != nullptr, "each window owns a theme");
    require(first_theme != second_theme, "window themes are distinct");
    require(
        first_theme->set(
            veyra::tokens::color_surface,
            veyra::Srgba8{.red = 1, .green = 2, .blue = 3}
        ) == veyra::ThemeUpdateResult::changed,
        "a window theme accepts an override"
    );
    require(
        first_theme->color(veyra::tokens::color_surface) !=
            second_theme->color(veyra::tokens::color_surface),
        "a theme override does not leak to another window"
    );

    require(
        !windows.set_focused_node(first, veyra::NodeId{.index = 0, .generation = 1}),
        "focus is rejected before a semantic scope is bound"
    );

    auto tree = veyra::NodeTree{};
    tree.reserve(4);
    const auto first_root = tree.create();
    const auto first_node = tree.create();
    const auto second_root = tree.create();
    const auto second_node = tree.create();
    require(tree.append_child(first_root, first_node), "first focus node attaches");
    require(tree.append_child(second_root, second_node), "second focus node attaches");
    require(tree.set_focusable(first_node, true), "first focus node is focusable");
    require(tree.set_focusable(second_node, true), "second focus node is focusable");

    require(
        windows.bind_semantics(first, tree, first_root) == veyra::WindowSetResult::ok,
        "first window binds its root"
    );
    require(
        windows.bind_semantics(second, tree, second_root) == veyra::WindowSetResult::ok,
        "second window binds its root"
    );
    require(
        windows.bind_semantics(first, tree, veyra::NodeId{.index = 99, .generation = 1}) ==
            veyra::WindowSetResult::invalid_root,
        "a root outside the tree is rejected"
    );

    require(windows.set_focused_node(first, first_node), "first focus is accepted");
    require(windows.set_focused_node(second, second_node), "second focus is accepted");
    require(windows.focused_node(first) == first_node, "first window keeps its focus");
    require(windows.focused_node(second) == second_node, "second window keeps its focus");
    require(
        !windows.set_focused_node(first, second_node),
        "focus cannot cross into another window scope"
    );
    require(windows.focused_node(first) == first_node, "a rejected focus leaves the old focus");
    require(windows.focused_node(veyra::kInvalidWindowId).is_null(), "unknown focus is null");
    require(windows.accessibility_root(first) == first_root, "accessibility root is stored");
    require(windows.accessibility_root(second) == second_root, "other windows keep their own root");
    require(
        windows.accessibility_root(veyra::kInvalidWindowId).is_null(),
        "unknown accessibility root is null"
    );

    require(
        windows.bind_semantics(first, tree, second_root) == veyra::WindowSetResult::ok,
        "a window scope can move to another root"
    );
    require(
        windows.focused_node(first).is_null(),
        "moving the scope drops focus that no longer belongs to it"
    );
    require(
        windows.bind_semantics(veyra::kInvalidWindowId, tree, first_root) ==
            veyra::WindowSetResult::invalid_window,
        "an unknown window cannot bind a scope"
    );
}

void check_scaled_hit_testing() {
    auto slots = std::array<veyra::WindowSlot, 2>{};
    auto windows = veyra::WindowSet(slots);
    auto first = veyra::WindowId{};
    auto second = veyra::WindowId{};
    require(windows.create({}, first) == veyra::WindowSetResult::ok, "first hit window creates");
    require(windows.create({}, second) == veyra::WindowSetResult::ok, "second hit window creates");

    auto tree = veyra::NodeTree{};
    tree.reserve(4);
    const auto first_root = tree.create();
    const auto first_child = tree.create();
    const auto second_root = tree.create();
    const auto second_child = tree.create();
    require(tree.append_child(first_root, first_child), "first hit child attaches");
    require(tree.append_child(second_root, second_child), "second hit child attaches");

    const std::array bounds{
        veyra::LayoutRect{.width = 100.0F, .height = 100.0F},
        veyra::LayoutRect{.x = 10.0F, .y = 10.0F, .width = 40.0F, .height = 40.0F},
        veyra::LayoutRect{.width = 100.0F, .height = 100.0F},
        veyra::LayoutRect{.x = 30.0F, .y = 30.0F, .width = 40.0F, .height = 40.0F},
    };

    require(
        windows.bind_semantics(first, tree, first_root) == veyra::WindowSetResult::ok,
        "first hit root binds"
    );
    require(
        windows.bind_semantics(second, tree, second_root) == veyra::WindowSetResult::ok,
        "second hit root binds"
    );
    require(
        windows.set_geometry(first, 100.0F, 100.0F, 1.0F, 1.0F) == veyra::WindowSetResult::ok,
        "first hit scale is set"
    );
    require(
        windows.set_geometry(second, 100.0F, 100.0F, 2.0F, 2.0F) == veyra::WindowSetResult::ok,
        "second hit scale is set"
    );

    auto target = veyra::NodeId{};
    require(
        windows.hit_test_logical(first, {.x = 20.0F, .y = 20.0F}, bounds, target) ==
                veyra::HitTestResult::hit &&
            target == first_child,
        "logical hit picks the first child"
    );
    require(
        windows.hit_test_logical(second, {.x = 40.0F, .y = 40.0F}, bounds, target) ==
                veyra::HitTestResult::hit &&
            target == second_child,
        "logical hit picks the second child"
    );

    require(
        windows.hit_test_physical(first, {.x = 40.0F, .y = 40.0F}, bounds, target) ==
                veyra::HitTestResult::hit &&
            target == first_child,
        "scale one maps a physical point to the first child"
    );
    require(
        windows.hit_test_physical(second, {.x = 40.0F, .y = 40.0F}, bounds, target) ==
                veyra::HitTestResult::hit &&
            target == second_root,
        "scale two maps the same physical point onto the second root"
    );
    require(
        windows.hit_test_physical(first, {.x = 80.0F, .y = 80.0F}, bounds, target) ==
                veyra::HitTestResult::hit &&
            target == first_root,
        "scale one misses the first child outside its bounds"
    );
    require(
        windows.hit_test_physical(second, {.x = 80.0F, .y = 80.0F}, bounds, target) ==
                veyra::HitTestResult::hit &&
            target == second_child,
        "scale two reaches the second child at the same physical point"
    );

    require(
        windows.hit_test_physical(
            veyra::kInvalidWindowId,
            {.x = 1.0F, .y = 1.0F},
            bounds,
            target
        ) == veyra::HitTestResult::invalid_root,
        "unknown windows have no hit-test root"
    );
    require(
        windows.hit_test_physical(
            first,
            {.x = std::numeric_limits<float>::infinity(), .y = 1.0F},
            bounds,
            target
        ) == veyra::HitTestResult::invalid_point,
        "non-finite points are rejected"
    );
    require(
        windows.hit_test_logical(first, {.x = 1.0F, .y = 1.0F}, {}, target) ==
            veyra::HitTestResult::insufficient_bounds,
        "missing bounds are reported"
    );
}

void check_geometry_and_state() {
    auto slots = std::array<veyra::WindowSlot, 2>{};
    auto windows = veyra::WindowSet(slots);
    auto first = veyra::WindowId{};
    auto second = veyra::WindowId{};
    require(
        windows.create({}, first) == veyra::WindowSetResult::ok,
        "first geometry window creates"
    );
    require(
        windows.create({}, second) == veyra::WindowSetResult::ok,
        "second geometry window creates"
    );

    auto info = veyra::WindowInfo{};
    require(windows.info(first, info), "new window info is available");
    require(info.state == veyra::WindowState::normal, "new windows start normal");
    require(info.width == 0.0F && info.height == 0.0F, "new windows start without geometry");
    require(info.scale_generation == 0, "new windows have no scale generation");

    require(
        windows.set_geometry(first, 320.0F, 200.0F, 1.0F, 1.0F) == veyra::WindowSetResult::ok,
        "first geometry is accepted"
    );
    require(
        windows.set_geometry(second, 640.0F, 480.0F, 2.0F, 2.0F) == veyra::WindowSetResult::ok,
        "second geometry is accepted"
    );
    require(windows.info(first, info), "first info stays available");
    require(info.width == 320.0F && info.height == 200.0F, "first client size is stored");
    require(
        info.scale_x == 1.0F && info.scale_generation == 1,
        "first scale starts generation one"
    );
    require(windows.info(second, info), "second info stays available");
    require(info.width == 640.0F && info.height == 480.0F, "second client size is stored");
    require(
        info.scale_x == 2.0F && info.scale_generation == 1,
        "second scale starts generation one"
    );

    require(
        windows.set_geometry(first, 320.0F, 200.0F, 1.0F, 1.0F) == veyra::WindowSetResult::ok,
        "repeating the same geometry is accepted"
    );
    require(windows.info(first, info), "first info stays available");
    require(info.scale_generation == 1, "unchanged scales keep the generation");
    require(
        windows.set_geometry(first, 320.0F, 200.0F, 1.5F, 1.5F) == veyra::WindowSetResult::ok,
        "a scale change is accepted"
    );
    require(windows.info(first, info), "first info stays available");
    require(info.scale_x == 1.5F && info.scale_generation == 2, "scale changes bump generation");

    require(
        windows.set_geometry(first, 0.0F, 200.0F, 1.0F, 1.0F) ==
            veyra::WindowSetResult::invalid_geometry,
        "zero width is rejected"
    );
    require(
        windows.set_geometry(first, 320.0F, std::numeric_limits<float>::infinity(), 1.0F, 1.0F) ==
            veyra::WindowSetResult::invalid_geometry,
        "non-finite height is rejected"
    );
    require(
        windows.set_geometry(first, 320.0F, 200.0F, -1.0F, 1.0F) ==
            veyra::WindowSetResult::invalid_geometry,
        "negative scale is rejected"
    );
    require(
        windows.set_geometry(veyra::kInvalidWindowId, 320.0F, 200.0F, 1.0F, 1.0F) ==
            veyra::WindowSetResult::invalid_window,
        "unknown windows reject geometry"
    );

    require(
        windows.set_state(first, veyra::WindowState::maximized) == veyra::WindowSetResult::ok,
        "window state is accepted"
    );
    require(windows.info(first, info), "first info stays available");
    require(info.state == veyra::WindowState::maximized, "window state is stored");
    require(
        windows.set_state(veyra::kInvalidWindowId, veyra::WindowState::minimized) ==
            veyra::WindowSetResult::invalid_window,
        "unknown windows reject state"
    );
    require(
        windows.set_state(first, static_cast<veyra::WindowState>(99)) ==
            veyra::WindowSetResult::invalid_state,
        "unknown states are rejected"
    );
}

}

int main() {
    try {
        check_owner_child_visibility();
        check_cancelled_owner_close();
        check_modal_dialog_result();
        check_owner_close_cancels_dialog();
        check_shutdown_modes();
        check_capacity_and_stale_ids();
        check_window_contexts();
        check_scaled_hit_testing();
        check_geometry_and_state();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }

    return 0;
}
