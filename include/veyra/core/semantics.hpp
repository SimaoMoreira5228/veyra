#ifndef VEYRA_CORE_SEMANTICS_HPP
#define VEYRA_CORE_SEMANTICS_HPP

#include "veyra/core/node_tree.hpp"
#include "veyra/layout/geometry.hpp"
#include "veyra/layout/virtual_list.hpp"

#include <cstdint>
#include <span>
#include <string_view>

namespace veyra {

enum class SemanticRole : std::uint8_t {
    none,
    generic,
    button,
    checkbox,
    text,
    text_input,
    heading,
    link,
    image,
    list,
    list_item,
    slider,
    progress_indicator,
    tab,
    tab_list,
    menu,
    menu_item,
    dialog,
    window,
    switch_,
    radio,
    column_header,
    scrollbar,
    spinbutton,
};

enum class SemanticState : std::uint16_t {
    disabled = 1U << 0U,
    selected = 1U << 1U,
    checked = 1U << 2U,
    expanded = 1U << 3U,
    busy = 1U << 4U,
    read_only = 1U << 5U,
    required = 1U << 6U,
    invalid = 1U << 7U,
    focused = 1U << 8U,
    focusable = 1U << 9U,
    mixed = 1U << 10U,
    modal = 1U << 11U,
    sorted_ascending = 1U << 12U,
    sorted_descending = 1U << 13U,
};

enum class SemanticAction : std::uint16_t {
    activate = 1U << 0U,
    increment = 1U << 1U,
    decrement = 1U << 2U,
    set_value = 1U << 3U,
    expand = 1U << 4U,
    collapse = 1U << 5U,
    select = 1U << 6U,
    dismiss = 1U << 7U,
    scroll_forward = 1U << 8U,
    scroll_backward = 1U << 9U,
};

constexpr std::uint16_t operator|(SemanticState left, SemanticState right) noexcept {
    return static_cast<std::uint16_t>(left) | static_cast<std::uint16_t>(right);
}

constexpr std::uint16_t operator|(std::uint16_t left, SemanticState right) noexcept {
    return left | static_cast<std::uint16_t>(right);
}

constexpr std::uint16_t state_flag(SemanticState state) noexcept {
    return static_cast<std::uint16_t>(state);
}

constexpr std::uint16_t operator|(SemanticAction left, SemanticAction right) noexcept {
    return static_cast<std::uint16_t>(left) | static_cast<std::uint16_t>(right);
}

constexpr std::uint16_t operator|(std::uint16_t left, SemanticAction right) noexcept {
    return left | static_cast<std::uint16_t>(right);
}

constexpr std::uint16_t action_flag(SemanticAction action) noexcept {
    return static_cast<std::uint16_t>(action);
}

constexpr bool has_state(std::uint16_t states, SemanticState state) noexcept {
    return (states & static_cast<std::uint16_t>(state)) != 0;
}

constexpr bool has_action(std::uint16_t actions, SemanticAction action) noexcept {
    return (actions & static_cast<std::uint16_t>(action)) != 0;
}

struct SemanticNodeData {
    SemanticRole role{SemanticRole::none};
    std::string_view label;
    std::string_view value;
    std::uint16_t states{};
    std::uint16_t actions{};
};

struct SemanticNode {
    NodeId id;
    NodeId parent;
    SemanticRole role{SemanticRole::none};
    std::string_view label;
    std::string_view value;
    LayoutRect bounds;
    std::uint16_t states{};
    std::uint16_t actions{};
};

struct VirtualSemanticNode {
    NodeId list;
    std::uint64_t key{};
    std::size_t index{};
    std::size_t position_in_set{};
    std::size_t set_size{};
    SemanticRole role{SemanticRole::list_item};
    std::string_view label;
    std::string_view value;
    LayoutRect bounds;
    std::uint16_t states{};
    std::uint16_t actions{};
    bool visible{};
};

enum class SemanticSnapshotResult : std::uint8_t {
    complete,
    insufficient_output,
    invalid_root,
    invalid_focus,
    missing_node_data,
    missing_bounds,
    invalid_role,
    invalid_state,
    invalid_action,
    invalid_bounds,
};

enum class VirtualSemanticResult : std::uint8_t {
    complete,
    invalid_list,
    invalid_item_index,
    invalid_scroll_offset,
    invalid_viewport,
    invalid_data,
    invalid_geometry,
};

SemanticSnapshotResult build_semantic_snapshot(
    const NodeTree& tree,
    NodeId root,
    NodeId focused,
    std::span<const SemanticNodeData> data_by_node_index,
    std::span<const LayoutRect> bounds_by_node_index,
    std::span<SemanticNode> output,
    std::size_t& node_count
) noexcept;

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
) noexcept;

}

#endif
