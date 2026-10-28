#include "veyra/app/window_set.hpp"

#include <array>
#include <cstdint>
#include <iostream>
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

    const auto first_node = veyra::NodeId{.index = 7, .generation = 2};
    const auto second_node = veyra::NodeId{.index = 9, .generation = 4};
    require(windows.set_focused_node(first, first_node), "first focus is accepted");
    require(windows.set_focused_node(second, second_node), "second focus is accepted");
    require(windows.focused_node(first) == first_node, "first window keeps its focus");
    require(windows.focused_node(second) == second_node, "second window keeps its focus");
    require(windows.focused_node(veyra::kInvalidWindowId).is_null(), "unknown focus is null");
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
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }

    return 0;
}
