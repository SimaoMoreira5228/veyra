#ifndef VEYRA_CORE_FOCUS_NAVIGATION_HPP
#define VEYRA_CORE_FOCUS_NAVIGATION_HPP

#include "veyra/core/node_tree.hpp"
#include "veyra/layout/core/geometry.hpp"

#include <cstdint>
#include <span>

namespace veyra {

enum class FocusDirection : std::uint8_t {
    forward,
    backward,
};

enum class FocusMoveResult : std::uint8_t {
    moved,
    unchanged,
    no_focusable_nodes,
    no_directional_candidate,
    invalid_root,
    invalid_current,
    invalid_direction,
    insufficient_bounds,
    invalid_bounds,
};

struct FocusMove {
    FocusMoveResult result{};
    NodeId target;
};

FocusMove
move_focus(NodeTree& tree, NodeId root, NodeId current, FocusDirection direction) noexcept;

enum class SpatialFocusDirection : std::uint8_t {
    left,
    right,
    up,
    down,
};

FocusMove move_focus_spatially(
    NodeTree& tree,
    NodeId root,
    NodeId current,
    SpatialFocusDirection direction,
    std::span<const LayoutRect> bounds_by_node_index
) noexcept;

}

#endif
