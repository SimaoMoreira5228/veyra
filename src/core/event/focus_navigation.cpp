#include "veyra/core/event/focus_navigation.hpp"

#include <cmath>
#include <limits>

namespace veyra {
namespace {

NodeId last_preorder_node(const NodeTree& tree, NodeId root) noexcept {
    auto current = root;
    for (auto child = tree.last_child(current); !child.is_null();
         child = tree.last_child(current)) {
        current = child;
    }
    return current;
}

NodeId next_preorder_node(const NodeTree& tree, NodeId root, NodeId current) noexcept {
    auto next = tree.first_child(current);
    if (!next.is_null()) {
        return next;
    }

    while (current != root) {
        next = tree.next_sibling(current);
        if (!next.is_null()) {
            return next;
        }
        current = tree.parent_of(current);
    }
    return {};
}

NodeId previous_preorder_node(const NodeTree& tree, NodeId root, NodeId current) noexcept {
    if (current == root) {
        return last_preorder_node(tree, root);
    }

    auto previous = tree.previous_sibling(current);
    if (previous.is_null()) {
        return tree.parent_of(current);
    }

    while (true) {
        const auto child = tree.last_child(previous);
        if (child.is_null()) {
            return previous;
        }
        previous = child;
    }
}

bool belongs_to_root(const NodeTree& tree, NodeId root, NodeId node) noexcept {
    for (auto current = node; !current.is_null(); current = tree.parent_of(current)) {
        if (current == root) {
            return true;
        }
    }
    return false;
}

void mark_focus_change(NodeTree& tree, NodeId previous, NodeId next) noexcept {
    if (previous == next) {
        return;
    }
    if (!previous.is_null()) {
        tree.mark_dirty(previous, DirtyCategory::semantics);
    }
    if (!next.is_null()) {
        tree.mark_dirty(next, DirtyCategory::semantics);
    }
}

bool valid_bounds(const LayoutRect& bounds) noexcept {
    return std::isfinite(bounds.x) && std::isfinite(bounds.y) && std::isfinite(bounds.width) &&
        std::isfinite(bounds.height) && bounds.width >= 0.0F && bounds.height >= 0.0F;
}

struct Point {
    double x{};
    double y{};
};

Point center(const LayoutRect& bounds) noexcept {
    return {
        .x = static_cast<double>(bounds.x) + bounds.width * 0.5,
        .y = static_cast<double>(bounds.y) + bounds.height * 0.5,
    };
}

bool is_in_direction(Point origin, Point candidate, SpatialFocusDirection direction) noexcept {
    switch (direction) {
    case SpatialFocusDirection::left:
        return candidate.x < origin.x;
    case SpatialFocusDirection::right:
        return candidate.x > origin.x;
    case SpatialFocusDirection::up:
        return candidate.y < origin.y;
    case SpatialFocusDirection::down:
        return candidate.y > origin.y;
    }
    return false;
}

bool shares_beam(
    const LayoutRect& origin,
    const LayoutRect& candidate,
    SpatialFocusDirection direction
) noexcept {
    switch (direction) {
    case SpatialFocusDirection::left:
    case SpatialFocusDirection::right:
        return static_cast<double>(
                   candidate.y
        )<static_cast<double>(origin.y) + origin.height&& static_cast<double>(candidate.y) +
          candidate.height>
            origin.y;
    case SpatialFocusDirection::up:
    case SpatialFocusDirection::down:
        return static_cast<double>(
                   candidate.x
        )<static_cast<double>(origin.x) + origin.width&& static_cast<double>(candidate.x) +
          candidate.width>
            origin.x;
    }
    return false;
}

}

FocusMove
move_focus(NodeTree& tree, NodeId root, NodeId current, FocusDirection direction) noexcept {
    if (direction != FocusDirection::forward && direction != FocusDirection::backward) {
        return {.result = FocusMoveResult::invalid_direction};
    }
    if (!tree.contains(root)) {
        return {.result = FocusMoveResult::invalid_root};
    }
    if (!current.is_null() && (!tree.contains(current) || !belongs_to_root(tree, root, current))) {
        return {.result = FocusMoveResult::invalid_current};
    }

    auto candidate = current;
    if (candidate.is_null()) {
        candidate = direction == FocusDirection::forward ? root : last_preorder_node(tree, root);
    } else if (direction == FocusDirection::forward) {
        candidate = next_preorder_node(tree, root, candidate);
        if (candidate.is_null()) {
            candidate = root;
        }
    } else {
        candidate = previous_preorder_node(tree, root, candidate);
    }

    const auto first_candidate = candidate;
    while (true) {
        if (tree.is_focusable(candidate)) {
            mark_focus_change(tree, current, candidate);
            return {
                .result =
                    candidate == current ? FocusMoveResult::unchanged : FocusMoveResult::moved,
                .target = candidate,
            };
        }

        if (direction == FocusDirection::forward) {
            candidate = next_preorder_node(tree, root, candidate);
            if (candidate.is_null()) {
                candidate = root;
            }
        } else {
            candidate = previous_preorder_node(tree, root, candidate);
        }

        if (candidate == first_candidate) {
            break;
        }
    }

    mark_focus_change(tree, current, {});
    return {.result = FocusMoveResult::no_focusable_nodes};
}

FocusMove move_focus_spatially(
    NodeTree& tree,
    NodeId root,
    NodeId current,
    SpatialFocusDirection direction,
    std::span<const LayoutRect> bounds_by_node_index
) noexcept {
    if (!tree.contains(root)) {
        return {.result = FocusMoveResult::invalid_root};
    }
    if (!tree.contains(current) || !belongs_to_root(tree, root, current)) {
        return {.result = FocusMoveResult::invalid_current};
    }
    if (direction != SpatialFocusDirection::left && direction != SpatialFocusDirection::right &&
        direction != SpatialFocusDirection::up && direction != SpatialFocusDirection::down) {
        return {.result = FocusMoveResult::invalid_direction};
    }
    if (current.index >= bounds_by_node_index.size()) {
        return {.result = FocusMoveResult::insufficient_bounds};
    }
    if (!valid_bounds(bounds_by_node_index[current.index])) {
        return {.result = FocusMoveResult::invalid_bounds};
    }

    const auto origin = center(bounds_by_node_index[current.index]);
    const auto& origin_bounds = bounds_by_node_index[current.index];
    auto closest = NodeId{};
    auto closest_shares_beam = false;
    auto closest_distance = std::numeric_limits<double>::infinity();

    for (auto candidate = root; !candidate.is_null();
         candidate = next_preorder_node(tree, root, candidate)) {
        if (candidate == current || !tree.is_focusable(candidate)) {
            continue;
        }
        if (candidate.index >= bounds_by_node_index.size()) {
            return {.result = FocusMoveResult::insufficient_bounds};
        }

        const auto& candidate_bounds = bounds_by_node_index[candidate.index];
        if (!valid_bounds(candidate_bounds)) {
            return {.result = FocusMoveResult::invalid_bounds};
        }

        const auto candidate_center = center(candidate_bounds);
        if (!is_in_direction(origin, candidate_center, direction)) {
            continue;
        }

        const auto dx = candidate_center.x - origin.x;
        const auto dy = candidate_center.y - origin.y;
        const auto distance = dx * dx + dy * dy;
        const auto candidate_shares_beam = shares_beam(origin_bounds, candidate_bounds, direction);
        if ((candidate_shares_beam && !closest_shares_beam) ||
            (candidate_shares_beam == closest_shares_beam && distance < closest_distance)) {
            closest = candidate;
            closest_shares_beam = candidate_shares_beam;
            closest_distance = distance;
        }
    }

    if (closest.is_null()) {
        return {.result = FocusMoveResult::no_directional_candidate};
    }
    mark_focus_change(tree, current, closest);
    return {
        .result = FocusMoveResult::moved,
        .target = closest,
    };
}

}
