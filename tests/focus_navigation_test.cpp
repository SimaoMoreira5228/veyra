#include "veyra/core/event/focus_navigation.hpp"

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

void check_tree_order_and_wrap() {
    veyra::NodeTree tree;
    tree.reserve(6);

    const auto root = tree.create();
    const auto group = tree.create();
    const auto first = tree.create();
    const auto second = tree.create();
    const auto last = tree.create();
    require(tree.append_child(root, group), "group attaches");
    require(tree.append_child(group, first), "first node attaches");
    require(tree.append_child(group, second), "second node attaches");
    require(tree.append_child(root, last), "last node attaches");
    require(tree.set_focusable(first, true), "first node becomes focusable");
    require(tree.set_focusable(second, true), "second node becomes focusable");
    require(tree.set_focusable(last, true), "last node becomes focusable");

    tree.clear_dirty(first, veyra::DirtyCategory::semantics);
    tree.clear_dirty(second, veyra::DirtyCategory::semantics);
    tree.clear_dirty(last, veyra::DirtyCategory::semantics);
    require(
        !tree.is_dirty(first, veyra::DirtyCategory::semantics),
        "focus starts clean before navigation"
    );

    auto move = veyra::move_focus(tree, root, {}, veyra::FocusDirection::forward);
    require(move.result == veyra::FocusMoveResult::moved, "empty focus moves to the first node");
    require(move.target == first, "forward traversal starts in tree order");
    require(tree.is_dirty(first, veyra::DirtyCategory::semantics), "new focus dirties semantics");

    tree.clear_dirty(first, veyra::DirtyCategory::semantics);
    tree.clear_dirty(second, veyra::DirtyCategory::semantics);
    move = veyra::move_focus(tree, root, first, veyra::FocusDirection::forward);
    require(move.result == veyra::FocusMoveResult::moved, "forward traversal advances");
    require(move.target == second, "forward traversal visits nested siblings in order");
    require(tree.is_dirty(first, veyra::DirtyCategory::semantics), "old focus dirties semantics");
    require(tree.is_dirty(second, veyra::DirtyCategory::semantics), "new focus dirties semantics");

    move = veyra::move_focus(tree, root, second, veyra::FocusDirection::backward);
    require(move.result == veyra::FocusMoveResult::moved, "backward traversal retreats");
    require(move.target == first, "backward traversal follows reverse tree order");

    move = veyra::move_focus(tree, root, last, veyra::FocusDirection::forward);
    require(move.result == veyra::FocusMoveResult::moved, "forward traversal wraps");
    require(move.target == first, "forward wrap returns to the first focusable node");

    move = veyra::move_focus(tree, root, first, veyra::FocusDirection::backward);
    require(move.result == veyra::FocusMoveResult::moved, "backward traversal wraps");
    require(move.target == last, "backward wrap returns to the last focusable node");
}

void check_empty_and_invalid_routes() {
    veyra::NodeTree tree;
    tree.reserve(3);

    const auto root = tree.create();
    const auto child = tree.create();
    const auto other_root = tree.create();
    require(tree.append_child(root, child), "child attaches");
    require(tree.set_focusable(other_root, true), "separate root becomes focusable");

    auto move = veyra::move_focus(tree, root, {}, veyra::FocusDirection::forward);
    require(
        move.result == veyra::FocusMoveResult::no_focusable_nodes,
        "tree without focusable nodes is reported"
    );
    require(move.target.is_null(), "empty traversal returns a null target");

    move = veyra::move_focus(tree, {}, {}, veyra::FocusDirection::forward);
    require(move.result == veyra::FocusMoveResult::invalid_root, "invalid root is rejected");

    move = veyra::move_focus(tree, root, other_root, veyra::FocusDirection::forward);
    require(
        move.result == veyra::FocusMoveResult::invalid_current,
        "current focus outside the subtree is rejected"
    );

    require(tree.set_focusable(child, true), "child becomes focusable");
    move = veyra::move_focus(tree, root, child, veyra::FocusDirection::forward);
    require(
        move.result == veyra::FocusMoveResult::unchanged,
        "single focusable node stays focused"
    );
    require(move.target == child, "unchanged traversal preserves the focused node");

    require(tree.set_focusable(child, false), "child loses focusability");
    tree.clear_dirty(child, veyra::DirtyCategory::semantics);
    move = veyra::move_focus(tree, root, child, veyra::FocusDirection::forward);
    require(
        move.result == veyra::FocusMoveResult::no_focusable_nodes,
        "non-focusable node is skipped"
    );
    require(
        tree.is_dirty(child, veyra::DirtyCategory::semantics),
        "cleared focus dirties semantics"
    );
}

void check_spatial_navigation() {
    veyra::NodeTree tree;
    tree.reserve(6);

    const auto root = tree.create();
    const auto current = tree.create();
    const auto right_near = tree.create();
    const auto right_far = tree.create();
    const auto right_diagonal = tree.create();
    const auto above = tree.create();
    require(tree.append_child(root, current), "current node attaches");
    require(tree.append_child(root, right_near), "near node attaches");
    require(tree.append_child(root, right_far), "far node attaches");
    require(tree.append_child(root, right_diagonal), "diagonal node attaches");
    require(tree.append_child(root, above), "above node attaches");
    require(tree.set_focusable(current, true), "current node becomes focusable");
    require(tree.set_focusable(right_near, true), "near node becomes focusable");
    require(tree.set_focusable(right_far, true), "far node becomes focusable");
    require(tree.set_focusable(right_diagonal, true), "diagonal node becomes focusable");
    require(tree.set_focusable(above, true), "above node becomes focusable");
    tree.clear_dirty(current, veyra::DirtyCategory::semantics);
    tree.clear_dirty(right_near, veyra::DirtyCategory::semantics);
    tree.clear_dirty(right_far, veyra::DirtyCategory::semantics);
    tree.clear_dirty(right_diagonal, veyra::DirtyCategory::semantics);
    tree.clear_dirty(above, veyra::DirtyCategory::semantics);

    const std::array bounds{
        veyra::LayoutRect{.width = 200.0F, .height = 200.0F},
        veyra::LayoutRect{.x = 10.0F, .y = 10.0F, .width = 20.0F, .height = 20.0F},
        veyra::LayoutRect{.x = 40.0F, .y = 10.0F, .width = 20.0F, .height = 20.0F},
        veyra::LayoutRect{.x = 140.0F, .y = 10.0F, .width = 20.0F, .height = 20.0F},
        veyra::LayoutRect{.x = 25.0F, .y = 80.0F, .width = 20.0F, .height = 20.0F},
        veyra::LayoutRect{.x = 10.0F, .y = -30.0F, .width = 20.0F, .height = 20.0F},
    };

    auto move = veyra::move_focus_spatially(
        tree,
        root,
        current,
        veyra::SpatialFocusDirection::right,
        bounds
    );
    require(move.result == veyra::FocusMoveResult::moved, "right direction finds a target");
    require(move.target == right_near, "closest right-side center wins");
    require(
        tree.is_dirty(current, veyra::DirtyCategory::semantics),
        "spatial move dirties old focus"
    );
    require(
        tree.is_dirty(right_near, veyra::DirtyCategory::semantics),
        "spatial move dirties new focus"
    );

    require(tree.set_focusable(right_near, false), "near node leaves the focus order");
    move = veyra::move_focus_spatially(
        tree,
        root,
        current,
        veyra::SpatialFocusDirection::right,
        bounds
    );
    require(move.result == veyra::FocusMoveResult::moved, "right direction skips the diagonal");
    require(move.target == right_far, "same-row candidate wins over a closer diagonal");

    move =
        veyra::move_focus_spatially(tree, root, current, veyra::SpatialFocusDirection::up, bounds);
    require(move.result == veyra::FocusMoveResult::moved, "up direction finds a target");
    require(move.target == above, "candidate above current is selected");

    move = veyra::move_focus_spatially(
        tree,
        root,
        current,
        veyra::SpatialFocusDirection::left,
        bounds
    );
    require(
        move.result == veyra::FocusMoveResult::no_directional_candidate,
        "direction without a candidate leaves focus alone"
    );

    move = veyra::move_focus_spatially(
        tree,
        root,
        current,
        veyra::SpatialFocusDirection::right,
        std::span<const veyra::LayoutRect>{bounds.data(), current.index}
    );
    require(
        move.result == veyra::FocusMoveResult::insufficient_bounds,
        "short bounds are reported"
    );

    auto invalid_bounds = bounds;
    invalid_bounds[current.index].width = -1.0F;
    move = veyra::move_focus_spatially(
        tree,
        root,
        current,
        veyra::SpatialFocusDirection::right,
        invalid_bounds
    );
    require(
        move.result == veyra::FocusMoveResult::invalid_bounds,
        "invalid focus bounds are rejected"
    );
}

}

int main() {
    try {
        check_tree_order_and_wrap();
        check_empty_and_invalid_routes();
        check_spatial_navigation();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
