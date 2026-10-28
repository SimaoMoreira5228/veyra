#ifndef VEYRA_APP_WINDOW_SEMANTICS_HPP
#define VEYRA_APP_WINDOW_SEMANTICS_HPP

#include "veyra/app/window_set.hpp"
#include "veyra/core/semantics/semantics.hpp"

#include <cstddef>
#include <span>

namespace veyra::app {

SemanticSnapshotResult build_window_snapshot(
    const NodeTree& tree,
    const WindowSet& windows,
    WindowId window,
    std::span<const SemanticNodeData> data_by_node_index,
    std::span<const LayoutRect> bounds_by_node_index,
    std::span<SemanticNode> output,
    std::size_t& node_count
) noexcept;

}

namespace veyra::app {
using ::veyra::app::build_window_snapshot;
}

#endif
