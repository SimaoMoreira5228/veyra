#include "veyra/app/window_semantics.hpp"

#include <array>
#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

void check_snapshots_stay_inside_their_window() {
    auto tree = veyra::NodeTree{};
    tree.reserve(6);

    const auto root_a = tree.create();
    const auto button_a = tree.create();
    const auto text_a = tree.create();
    const auto root_b = tree.create();
    const auto button_b = tree.create();
    require(tree.append_child(root_a, button_a), "first button attaches");
    require(tree.append_child(button_a, text_a), "first label attaches");
    require(tree.append_child(root_b, button_b), "second button attaches");
    require(tree.set_focusable(button_a, true), "first button is focusable");
    require(tree.set_focusable(button_b, true), "second button is focusable");

    const std::array data{
        veyra::SemanticNodeData{.role = veyra::SemanticRole::window, .label = "First"},
        veyra::SemanticNodeData{
            .role = veyra::SemanticRole::button,
            .label = "Save",
            .actions = veyra::action_flag(veyra::SemanticAction::activate),
        },
        veyra::SemanticNodeData{.role = veyra::SemanticRole::text, .value = "Ready"},
        veyra::SemanticNodeData{.role = veyra::SemanticRole::window, .label = "Second"},
        veyra::SemanticNodeData{.role = veyra::SemanticRole::button, .label = "Cancel"},
    };
    const std::array bounds{
        veyra::LayoutRect{.width = 100.0F, .height = 100.0F},
        veyra::LayoutRect{.x = 10.0F, .y = 10.0F, .width = 30.0F, .height = 20.0F},
        veyra::LayoutRect{.x = 12.0F, .y = 12.0F, .width = 8.0F, .height = 8.0F},
        veyra::LayoutRect{.width = 80.0F, .height = 80.0F},
        veyra::LayoutRect{.x = 4.0F, .y = 4.0F, .width = 24.0F, .height = 18.0F},
    };

    auto slots = std::array<veyra::WindowSlot, 2>{};
    auto windows = veyra::WindowSet(slots);
    auto first = veyra::WindowId{};
    auto second = veyra::WindowId{};
    require(windows.create({}, first) == veyra::WindowSetResult::ok, "first window creates");
    require(windows.create({}, second) == veyra::WindowSetResult::ok, "second window creates");
    require(windows.set_accessibility_root(first, root_a), "first root is stored");
    require(windows.set_accessibility_root(second, root_b), "second root is stored");
    require(windows.set_focused_node(first, button_a), "first focus is stored");
    require(windows.set_focused_node(second, button_b), "second focus is stored");

    auto output = std::array<veyra::SemanticNode, 4>{};
    auto count = std::size_t{};
    require(
        veyra::app::build_window_snapshot(tree, windows, first, data, bounds, output, count) ==
            veyra::SemanticSnapshotResult::complete,
        "first window snapshot builds"
    );
    require(count == 3, "first snapshot contains only its subtree");
    require(output[0].id == root_a, "first snapshot starts at the first root");
    require(
        veyra::has_state(output[1].states, veyra::SemanticState::focused),
        "first focus comes from the first window"
    );
    require(output[2].id == text_a, "first snapshot keeps descendant order");
    require(output[2].parent == button_a, "descendant parent links stay inside the window");

    require(
        veyra::app::build_window_snapshot(tree, windows, second, data, bounds, output, count) ==
            veyra::SemanticSnapshotResult::complete,
        "second window snapshot builds"
    );
    require(count == 2, "second snapshot contains only its subtree");
    require(output[0].id == root_b, "second snapshot starts at the second root");
    require(
        veyra::has_state(output[1].states, veyra::SemanticState::focused),
        "second focus comes from the second window"
    );

    require(windows.set_focused_node(first, button_b), "cross-window focus is stored as an id");
    require(
        veyra::app::build_window_snapshot(tree, windows, first, data, bounds, output, count) ==
            veyra::SemanticSnapshotResult::invalid_focus,
        "cross-window focus is rejected by the snapshot"
    );
    require(count == 0, "a rejected snapshot clears the count");
}

void check_snapshot_capacity_and_unknown_window() {
    auto tree = veyra::NodeTree{};
    tree.reserve(2);
    const auto root = tree.create();
    const auto child = tree.create();
    require(tree.append_child(root, child), "child attaches");

    const std::array data{
        veyra::SemanticNodeData{.role = veyra::SemanticRole::window},
        veyra::SemanticNodeData{.role = veyra::SemanticRole::button},
    };
    const std::array bounds{
        veyra::LayoutRect{.width = 40.0F, .height = 40.0F},
        veyra::LayoutRect{.width = 10.0F, .height = 10.0F},
    };

    auto slots = std::array<veyra::WindowSlot, 1>{};
    auto windows = veyra::WindowSet(slots);
    auto window = veyra::WindowId{};
    require(windows.create({}, window) == veyra::WindowSetResult::ok, "window creates");
    require(windows.set_accessibility_root(window, root), "root is stored");

    std::array<veyra::SemanticNode, 1> output{};
    auto count = std::size_t{};
    require(
        veyra::app::build_window_snapshot(tree, windows, window, data, bounds, output, count) ==
            veyra::SemanticSnapshotResult::insufficient_output,
        "a short output reports the required count"
    );
    require(count == 2, "required count includes every semantic node");

    require(
        veyra::app::build_window_snapshot(
            tree,
            windows,
            veyra::kInvalidWindowId,
            data,
            bounds,
            output,
            count
        ) == veyra::SemanticSnapshotResult::invalid_root,
        "unknown windows have no semantic root"
    );
    require(count == 0, "unknown windows report no nodes");
}

}

int main() {
    try {
        check_snapshots_stay_inside_their_window();
        check_snapshot_capacity_and_unknown_window();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }

    return 0;
}
