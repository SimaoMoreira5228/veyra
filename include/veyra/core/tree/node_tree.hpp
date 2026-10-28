#ifndef VEYRA_CORE_NODE_TREE_HPP
#define VEYRA_CORE_NODE_TREE_HPP

#include "veyra/core/tree/node_id_pool.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace veyra {

enum class DirtyCategory : std::uint8_t {
    structure = 1U << 0U,
    layout = 1U << 1U,
    paint = 1U << 2U,
    semantics = 1U << 3U,
    text = 1U << 4U,
    image = 1U << 5U,
};

class NodeTree {
    struct Links {
        NodeId parent;
        NodeId first_child;
        NodeId last_child;
        NodeId previous_sibling;
        NodeId next_sibling;
        std::uint8_t dirty_flags{};
        std::uint32_t dirty_index{NodeId::invalid_index};
        bool focusable{};
    };

    NodeIdPool ids_;
    std::vector<Links> links_;
    std::vector<NodeId> dirty_nodes_;
    std::size_t dirty_node_count_{};

    bool can_append_child(NodeId parent, NodeId child) const noexcept;
    void append_child_unchecked(NodeId parent, NodeId child) noexcept;
    void detach_unchecked(NodeId id) noexcept;
    void mark_dirty_unchecked(NodeId id, DirtyCategory category) noexcept;
    void remove_dirty_node_unchecked(NodeId id) noexcept;
    void clear_all_dirty_unchecked(NodeId id) noexcept;

  public:
    NodeTree() = default;
    NodeTree(const NodeTree&) = delete;
    NodeTree& operator=(const NodeTree&) = delete;
    NodeTree(NodeTree&& other) noexcept;
    NodeTree& operator=(NodeTree&& other) noexcept;

    void reserve(std::size_t count);
    NodeId create();
    bool append_child(NodeId parent, NodeId child) noexcept;
    bool append_children(NodeId parent, std::span<const NodeId> children) noexcept;
    bool detach(NodeId child) noexcept;
    bool destroy_subtree(NodeId id) noexcept;
    bool contains(NodeId id) const noexcept;
    NodeId parent_of(NodeId id) const noexcept;
    NodeId first_child(NodeId id) const noexcept;
    NodeId last_child(NodeId id) const noexcept;
    NodeId previous_sibling(NodeId id) const noexcept;
    NodeId next_sibling(NodeId id) const noexcept;
    bool set_focusable(NodeId id, bool focusable) noexcept;
    bool is_focusable(NodeId id) const noexcept;
    bool path_to_root(NodeId id, std::span<NodeId> path, std::size_t& length) const noexcept;
    bool mark_dirty(NodeId id, DirtyCategory category) noexcept;
    bool clear_dirty(NodeId id, DirtyCategory category) noexcept;
    bool is_dirty(NodeId id, DirtyCategory category) const noexcept;
    std::size_t size() const noexcept;
    std::size_t dirty_node_count() const noexcept;
    std::span<const NodeId> dirty_nodes() const noexcept;
};

}

namespace veyra::core {
using ::veyra::DirtyCategory;
using ::veyra::NodeTree;
}

namespace veyra::core::tree {
using ::veyra::DirtyCategory;
using ::veyra::NodeTree;
}

#endif
