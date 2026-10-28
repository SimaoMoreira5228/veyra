#include "veyra/app/window_semantics.hpp"

namespace veyra::app {

SemanticSnapshotResult build_window_snapshot(
    const NodeTree& tree,
    const WindowSet& windows,
    WindowId window,
    std::span<const SemanticNodeData> data_by_node_index,
    std::span<const LayoutRect> bounds_by_node_index,
    std::span<SemanticNode> output,
    std::size_t& node_count
) noexcept {
    node_count = 0;
    if (!windows.is_open(window)) {
        return SemanticSnapshotResult::invalid_root;
    }

    return build_semantic_snapshot(
        tree,
        windows.accessibility_root(window),
        windows.focused_node(window),
        data_by_node_index,
        bounds_by_node_index,
        output,
        node_count
    );
}

}
