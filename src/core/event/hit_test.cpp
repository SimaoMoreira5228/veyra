#include "veyra/core/event/hit_test.hpp"

#include <cmath>

namespace veyra {
namespace {

bool contains_point(const LayoutRect& bounds, LayoutPoint point) noexcept {
    const auto right = static_cast<double>(bounds.x) + bounds.width;
    const auto bottom = static_cast<double>(bounds.y) + bounds.height;

    return point.x >= bounds.x && point.y >= bounds.y && point.x < right && point.y < bottom;
}

bool valid_bounds(const LayoutRect& bounds) noexcept {
    return std::isfinite(bounds.x) && std::isfinite(bounds.y) && std::isfinite(bounds.width) &&
        std::isfinite(bounds.height) && bounds.width >= 0.0F && bounds.height >= 0.0F;
}

}

HitTestResult hit_test(
    const NodeTree& tree,
    NodeId root,
    LayoutPoint point,
    std::span<const LayoutRect> bounds_by_node_index,
    NodeId& target
) noexcept {
    target = {};
    if (!tree.contains(root)) {
        return HitTestResult::invalid_root;
    }
    if (!std::isfinite(point.x) || !std::isfinite(point.y)) {
        return HitTestResult::invalid_point;
    }

    auto current = root;
    while (true) {
        if (current.index >= bounds_by_node_index.size()) {
            return HitTestResult::insufficient_bounds;
        }

        const auto& bounds = bounds_by_node_index[current.index];
        if (!valid_bounds(bounds)) {
            return HitTestResult::invalid_bounds;
        }
        if (!contains_point(bounds, point)) {
            return HitTestResult::no_hit;
        }

        auto child = tree.last_child(current);
        while (!child.is_null()) {
            if (child.index >= bounds_by_node_index.size()) {
                return HitTestResult::insufficient_bounds;
            }

            const auto& child_bounds = bounds_by_node_index[child.index];
            if (!valid_bounds(child_bounds)) {
                return HitTestResult::invalid_bounds;
            }
            if (contains_point(child_bounds, point)) {
                break;
            }
            child = tree.previous_sibling(child);
        }

        if (child.is_null()) {
            target = current;
            return HitTestResult::hit;
        }
        current = child;
    }
}

}
