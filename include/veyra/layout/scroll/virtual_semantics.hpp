#ifndef VEYRA_LAYOUT_SCROLL_VIRTUAL_SEMANTICS_HPP
#define VEYRA_LAYOUT_SCROLL_VIRTUAL_SEMANTICS_HPP

#include "veyra/core/semantics/semantics.hpp"
#include "veyra/foundation/geometry.hpp"
#include "veyra/layout/scroll/virtual_list.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace veyra {

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

enum class VirtualSemanticResult : std::uint8_t {
    complete,
    invalid_list,
    invalid_item_index,
    invalid_scroll_offset,
    invalid_viewport,
    invalid_data,
    invalid_geometry,
};

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

namespace veyra::layout {
using ::veyra::VirtualSemanticNode;
using ::veyra::VirtualSemanticResult;
using ::veyra::virtual_semantic_item;
}

namespace veyra::layout::scroll {
using ::veyra::VirtualSemanticNode;
using ::veyra::VirtualSemanticResult;
using ::veyra::virtual_semantic_item;
}

#endif
