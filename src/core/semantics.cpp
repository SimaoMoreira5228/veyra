#include "veyra/core/semantics.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace veyra {
namespace {

constexpr auto focused_state = static_cast<std::uint16_t>(SemanticState::focused);
constexpr auto focusable_state = static_cast<std::uint16_t>(SemanticState::focusable);

constexpr std::uint16_t known_states = static_cast<std::uint16_t>(SemanticState::disabled) |
    static_cast<std::uint16_t>(SemanticState::selected) |
    static_cast<std::uint16_t>(SemanticState::checked) |
    static_cast<std::uint16_t>(SemanticState::expanded) |
    static_cast<std::uint16_t>(SemanticState::busy) |
    static_cast<std::uint16_t>(SemanticState::read_only) |
    static_cast<std::uint16_t>(SemanticState::required) |
    static_cast<std::uint16_t>(SemanticState::invalid) | focused_state | focusable_state |
    static_cast<std::uint16_t>(SemanticState::mixed) |
    static_cast<std::uint16_t>(SemanticState::modal) |
    static_cast<std::uint16_t>(SemanticState::sorted_ascending) |
    static_cast<std::uint16_t>(SemanticState::sorted_descending);

constexpr std::uint16_t known_actions = static_cast<std::uint16_t>(SemanticAction::activate) |
    static_cast<std::uint16_t>(SemanticAction::increment) |
    static_cast<std::uint16_t>(SemanticAction::decrement) |
    static_cast<std::uint16_t>(SemanticAction::set_value) |
    static_cast<std::uint16_t>(SemanticAction::expand) |
    static_cast<std::uint16_t>(SemanticAction::collapse) |
    static_cast<std::uint16_t>(SemanticAction::select) |
    static_cast<std::uint16_t>(SemanticAction::dismiss) |
    static_cast<std::uint16_t>(SemanticAction::scroll_forward) |
    static_cast<std::uint16_t>(SemanticAction::scroll_backward);

bool valid_role(SemanticRole role) noexcept {
    switch (role) {
    case SemanticRole::none:
    case SemanticRole::generic:
    case SemanticRole::button:
    case SemanticRole::checkbox:
    case SemanticRole::text:
    case SemanticRole::text_input:
    case SemanticRole::heading:
    case SemanticRole::link:
    case SemanticRole::image:
    case SemanticRole::list:
    case SemanticRole::list_item:
    case SemanticRole::slider:
    case SemanticRole::progress_indicator:
    case SemanticRole::tab:
    case SemanticRole::tab_list:
    case SemanticRole::menu:
    case SemanticRole::menu_item:
    case SemanticRole::dialog:
    case SemanticRole::window:
    case SemanticRole::switch_:
    case SemanticRole::radio:
    case SemanticRole::column_header:
    case SemanticRole::scrollbar:
    case SemanticRole::spinbutton:
        return true;
    }
    return false;
}

bool valid_bounds(const LayoutRect& bounds) noexcept {
    return std::isfinite(bounds.x) && std::isfinite(bounds.y) && std::isfinite(bounds.width) &&
        std::isfinite(bounds.height) && bounds.width >= 0.0F && bounds.height >= 0.0F;
}

SemanticSnapshotResult validate_data(const SemanticNodeData& data, bool tree_focus_state) noexcept {
    if (!valid_role(data.role)) {
        return SemanticSnapshotResult::invalid_role;
    }
    if ((data.states & ~known_states) != 0 ||
        (tree_focus_state &&
         (has_state(data.states, SemanticState::focused) ||
          has_state(data.states, SemanticState::focusable)))) {
        return SemanticSnapshotResult::invalid_state;
    }
    if ((data.actions & ~known_actions) != 0) {
        return SemanticSnapshotResult::invalid_action;
    }
    return SemanticSnapshotResult::complete;
}

LayoutRect intersect(LayoutRect bounds, const LayoutRect& clip) noexcept {
    const auto left = std::max(static_cast<double>(bounds.x), static_cast<double>(clip.x));
    const auto top = std::max(static_cast<double>(bounds.y), static_cast<double>(clip.y));
    const auto right = std::min(
        static_cast<double>(bounds.x) + bounds.width,
        static_cast<double>(clip.x) + clip.width
    );
    const auto bottom = std::min(
        static_cast<double>(bounds.y) + bounds.height,
        static_cast<double>(clip.y) + clip.height
    );

    return {
        .x = static_cast<float>(left),
        .y = static_cast<float>(top),
        .width = static_cast<float>(std::max(0.0, right - left)),
        .height = static_cast<float>(std::max(0.0, bottom - top)),
    };
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

bool belongs_to_root(const NodeTree& tree, NodeId root, NodeId node) noexcept {
    for (auto current = node; !current.is_null(); current = tree.parent_of(current)) {
        if (current == root) {
            return true;
        }
    }
    return false;
}

NodeId semantic_parent(
    const NodeTree& tree,
    NodeId root,
    NodeId node,
    std::span<const SemanticNodeData> data_by_node_index
) noexcept {
    if (node == root) {
        return {};
    }

    auto parent = tree.parent_of(node);
    while (!parent.is_null()) {
        if (data_by_node_index[parent.index].role != SemanticRole::none) {
            return parent;
        }
        if (parent == root) {
            return {};
        }
        parent = tree.parent_of(parent);
    }
    return {};
}

SemanticSnapshotResult collect_snapshot(
    const NodeTree& tree,
    NodeId root,
    NodeId focused,
    std::span<const SemanticNodeData> data_by_node_index,
    std::span<const LayoutRect> bounds_by_node_index,
    std::span<SemanticNode> output,
    bool write_output,
    std::size_t& node_count
) noexcept {
    node_count = 0;
    auto current = root;
    while (!current.is_null()) {
        if (current.index >= data_by_node_index.size()) {
            return SemanticSnapshotResult::missing_node_data;
        }
        if (current.index >= bounds_by_node_index.size()) {
            return SemanticSnapshotResult::missing_bounds;
        }

        const auto& data = data_by_node_index[current.index];
        const auto data_result = validate_data(data, true);
        if (data_result != SemanticSnapshotResult::complete) {
            return data_result;
        }
        if (!valid_bounds(bounds_by_node_index[current.index])) {
            return SemanticSnapshotResult::invalid_bounds;
        }

        auto clipped_bounds = bounds_by_node_index[current.index];
        auto ancestor = current == root ? NodeId{} : tree.parent_of(current);
        while (!ancestor.is_null()) {
            if (ancestor.index >= bounds_by_node_index.size()) {
                return SemanticSnapshotResult::missing_bounds;
            }
            if (!valid_bounds(bounds_by_node_index[ancestor.index])) {
                return SemanticSnapshotResult::invalid_bounds;
            }

            clipped_bounds = intersect(clipped_bounds, bounds_by_node_index[ancestor.index]);
            if (ancestor == root) {
                break;
            }
            ancestor = tree.parent_of(ancestor);
        }

        if (data.role != SemanticRole::none) {
            if (write_output) {
                auto states = data.states;
                if (current == focused) {
                    states = static_cast<std::uint16_t>(states | focused_state);
                }
                if (tree.is_focusable(current)) {
                    states = static_cast<std::uint16_t>(states | focusable_state);
                }

                output[node_count] = {
                    .id = current,
                    .parent = semantic_parent(tree, root, current, data_by_node_index),
                    .role = data.role,
                    .label = data.label,
                    .value = data.value,
                    .bounds = clipped_bounds,
                    .states = states,
                    .actions = data.actions,
                };
            }
            ++node_count;
        }

        current = next_preorder_node(tree, root, current);
    }
    return SemanticSnapshotResult::complete;
}

}

SemanticSnapshotResult build_semantic_snapshot(
    const NodeTree& tree,
    NodeId root,
    NodeId focused,
    std::span<const SemanticNodeData> data_by_node_index,
    std::span<const LayoutRect> bounds_by_node_index,
    std::span<SemanticNode> output,
    std::size_t& node_count
) noexcept {
    node_count = 0;
    if (!tree.contains(root)) {
        return SemanticSnapshotResult::invalid_root;
    }
    if (!focused.is_null() && (!tree.contains(focused) || !belongs_to_root(tree, root, focused))) {
        return SemanticSnapshotResult::invalid_focus;
    }

    std::size_t required_count = 0;
    const auto validation = collect_snapshot(
        tree,
        root,
        focused,
        data_by_node_index,
        bounds_by_node_index,
        {},
        false,
        required_count
    );
    if (validation != SemanticSnapshotResult::complete) {
        return validation;
    }
    if (output.size() < required_count) {
        node_count = required_count;
        return SemanticSnapshotResult::insufficient_output;
    }

    return collect_snapshot(
        tree,
        root,
        focused,
        data_by_node_index,
        bounds_by_node_index,
        output,
        true,
        node_count
    );
}

VirtualSemanticResult virtual_semantic_item(
    const NodeTree& tree,
    NodeId list,
    const VirtualListIndex& index,
    std::uint64_t item_key,
    std::size_t item_index,
    double scroll_offset,
    LayoutRect viewport,
    SemanticNodeData data,
    VirtualSemanticNode& output
) noexcept {
    if (!tree.contains(list)) {
        return VirtualSemanticResult::invalid_list;
    }
    if (item_index >= index.size()) {
        return VirtualSemanticResult::invalid_item_index;
    }
    if (!std::isfinite(scroll_offset) || scroll_offset < 0.0) {
        return VirtualSemanticResult::invalid_scroll_offset;
    }
    if (!valid_bounds(viewport)) {
        return VirtualSemanticResult::invalid_viewport;
    }

    const auto data_result = validate_data(data, false);
    if (data_result != SemanticSnapshotResult::complete || data.role != SemanticRole::list_item) {
        return VirtualSemanticResult::invalid_data;
    }

    double item_top = 0.0;
    double item_bottom = 0.0;
    if (!index.offset_of(item_index, item_top) || !index.offset_of(item_index + 1, item_bottom)) {
        return VirtualSemanticResult::invalid_item_index;
    }

    const auto y = static_cast<double>(viewport.y) + item_top - scroll_offset;
    const auto height = item_bottom - item_top;
    constexpr auto maximum_coordinate = static_cast<double>(std::numeric_limits<float>::max());
    if (!std::isfinite(y) || std::abs(y) > maximum_coordinate || height > maximum_coordinate) {
        return VirtualSemanticResult::invalid_geometry;
    }

    const auto bottom = y + height;
    const auto viewport_bottom = static_cast<double>(viewport.y) + viewport.height;
    const bool visible = viewport.width > 0.0F && viewport.height > 0.0F && height > 0.0 &&
        bottom > viewport.y && y < viewport_bottom;

    output = {
        .list = list,
        .key = item_key,
        .index = item_index,
        .position_in_set = item_index + 1,
        .set_size = index.size(),
        .role = SemanticRole::list_item,
        .label = data.label,
        .value = data.value,
        .bounds =
            {
                .x = viewport.x,
                .y = static_cast<float>(y),
                .width = viewport.width,
                .height = static_cast<float>(height),
            },
        .states = data.states,
        .actions = data.actions,
        .visible = visible,
    };
    return VirtualSemanticResult::complete;
}

}
