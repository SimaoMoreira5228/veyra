#include "veyra/layout/scroll/virtual_semantics.hpp"

#include <cmath>
#include <limits>

namespace veyra {
namespace {

constexpr std::uint16_t known_states = static_cast<std::uint16_t>(SemanticState::disabled) |
    static_cast<std::uint16_t>(SemanticState::selected) |
    static_cast<std::uint16_t>(SemanticState::checked) |
    static_cast<std::uint16_t>(SemanticState::expanded) |
    static_cast<std::uint16_t>(SemanticState::busy) |
    static_cast<std::uint16_t>(SemanticState::read_only) |
    static_cast<std::uint16_t>(SemanticState::required) |
    static_cast<std::uint16_t>(SemanticState::invalid) |
    static_cast<std::uint16_t>(SemanticState::focused) |
    static_cast<std::uint16_t>(SemanticState::focusable) |
    static_cast<std::uint16_t>(SemanticState::mixed) |
    static_cast<std::uint16_t>(SemanticState::modal) |
    static_cast<std::uint16_t>(SemanticState::sorted_ascending) |
    static_cast<std::uint16_t>(SemanticState::sorted_descending) |
    static_cast<std::uint16_t>(SemanticState::protected_value);

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

bool valid_bounds(const LayoutRect& bounds) noexcept {
    return std::isfinite(bounds.x) && std::isfinite(bounds.y) && std::isfinite(bounds.width) &&
        std::isfinite(bounds.height) && bounds.width >= 0.0F && bounds.height >= 0.0F;
}

bool valid_role(SemanticRole role) noexcept {
    return static_cast<std::uint8_t>(role) <= static_cast<std::uint8_t>(SemanticRole::grid_cell);
}

bool validate_data(const SemanticNodeData& data) noexcept {
    return valid_role(data.role) && (data.states & ~known_states) == 0 &&
        (data.actions & ~known_actions) == 0;
}

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

    if (!validate_data(data) || data.role != SemanticRole::list_item) {
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
