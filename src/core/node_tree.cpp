#include "veyra/core/node_tree.hpp"

#include <utility>

namespace veyra {
namespace {

constexpr std::uint8_t dirty_mask(DirtyCategory category) noexcept {
    return static_cast<std::uint8_t>(category);
}

constexpr std::uint8_t all_dirty_categories = dirty_mask(DirtyCategory::structure) |
    dirty_mask(DirtyCategory::layout) | dirty_mask(DirtyCategory::paint) |
    dirty_mask(DirtyCategory::semantics);

bool is_single_dirty_category(std::uint8_t mask) noexcept {
    return mask != 0 && (mask & all_dirty_categories) == mask && (mask & (mask - 1U)) == 0;
}

}

NodeTree::NodeTree(NodeTree&& other) noexcept :
    ids_(std::move(other.ids_)), links_(std::move(other.links_)),
    dirty_node_count_(std::exchange(other.dirty_node_count_, 0)) {
    other.links_.clear();
}

NodeTree& NodeTree::operator=(NodeTree&& other) noexcept {
    if (this == &other) {
        return *this;
    }

    ids_ = std::move(other.ids_);
    links_ = std::move(other.links_);
    dirty_node_count_ = std::exchange(other.dirty_node_count_, 0);
    other.links_.clear();
    return *this;
}

void NodeTree::reserve(std::size_t count) {
    ids_.reserve(count);
    links_.reserve(count);
}

NodeId NodeTree::create() {
    const auto id = ids_.allocate();
    if (id.is_null()) {
        return id;
    }

    try {
        if (links_.size() <= id.index) {
            links_.resize(static_cast<std::size_t>(id.index) + 1);
        }
        links_[id.index] = {};
    } catch (...) {
        ids_.release(id);
        throw;
    }

    mark_dirty_unchecked(id, DirtyCategory::structure);
    return id;
}

bool NodeTree::append_child(NodeId parent_id, NodeId child_id) noexcept {
    if (!can_append_child(parent_id, child_id)) {
        return false;
    }

    append_child_unchecked(parent_id, child_id);
    return true;
}

bool NodeTree::append_children(NodeId parent_id, std::span<const NodeId> children) noexcept {
    if (!contains(parent_id)) {
        return false;
    }

    for (const auto child_id : children) {
        if (!can_append_child(parent_id, child_id)) {
            return false;
        }
    }

    for (const auto child_id : children) {
        append_child_unchecked(parent_id, child_id);
    }
    return true;
}

bool NodeTree::can_append_child(NodeId parent_id, NodeId child_id) const noexcept {
    if (!contains(parent_id) || !contains(child_id) || parent_id == child_id) {
        return false;
    }

    for (auto ancestor = parent_id; !ancestor.is_null(); ancestor = links_[ancestor.index].parent) {
        if (ancestor == child_id) {
            return false;
        }
    }

    return true;
}

void NodeTree::append_child_unchecked(NodeId parent_id, NodeId child_id) noexcept {
    auto& child = links_[child_id.index];
    auto& parent = links_[parent_id.index];
    if (child.parent == parent_id && parent.last_child == child_id) {
        return;
    }

    detach_unchecked(child_id);
    child.parent = parent_id;
    child.previous_sibling = parent.last_child;

    if (parent.last_child.is_null()) {
        parent.first_child = child_id;
    } else {
        links_[parent.last_child.index].next_sibling = child_id;
    }

    parent.last_child = child_id;
    mark_dirty_unchecked(parent_id, DirtyCategory::structure);
    mark_dirty_unchecked(parent_id, DirtyCategory::layout);
    mark_dirty_unchecked(child_id, DirtyCategory::structure);
    mark_dirty_unchecked(child_id, DirtyCategory::layout);
}

bool NodeTree::detach(NodeId child_id) noexcept {
    if (!contains(child_id)) {
        return false;
    }

    detach_unchecked(child_id);
    return true;
}

void NodeTree::detach_unchecked(NodeId child_id) noexcept {
    auto& child = links_[child_id.index];
    if (child.parent.is_null()) {
        return;
    }

    const auto parent_id = child.parent;
    auto& parent = links_[parent_id.index];
    if (child.previous_sibling.is_null()) {
        parent.first_child = child.next_sibling;
    } else {
        links_[child.previous_sibling.index].next_sibling = child.next_sibling;
    }

    if (child.next_sibling.is_null()) {
        parent.last_child = child.previous_sibling;
    } else {
        links_[child.next_sibling.index].previous_sibling = child.previous_sibling;
    }

    mark_dirty_unchecked(parent_id, DirtyCategory::structure);
    mark_dirty_unchecked(parent_id, DirtyCategory::layout);
    mark_dirty_unchecked(child_id, DirtyCategory::structure);
    mark_dirty_unchecked(child_id, DirtyCategory::layout);
    child.parent = {};
    child.previous_sibling = {};
    child.next_sibling = {};
}

bool NodeTree::destroy_subtree(NodeId id) noexcept {
    if (!contains(id)) {
        return false;
    }

    auto current = id;
    while (true) {
        if (!links_[current.index].first_child.is_null()) {
            current = links_[current.index].first_child;
            continue;
        }

        const auto parent = links_[current.index].parent;
        const auto destroying_root = current == id;
        detach_unchecked(current);
        clear_all_dirty_unchecked(current);
        links_[current.index] = {};
        ids_.release(current);

        if (destroying_root) {
            return true;
        }

        current = parent;
    }
}

bool NodeTree::contains(NodeId id) const noexcept {
    return ids_.contains(id);
}

NodeId NodeTree::parent_of(NodeId id) const noexcept {
    if (!contains(id)) {
        return {};
    }
    return links_[id.index].parent;
}

NodeId NodeTree::first_child(NodeId id) const noexcept {
    if (!contains(id)) {
        return {};
    }
    return links_[id.index].first_child;
}

NodeId NodeTree::next_sibling(NodeId id) const noexcept {
    if (!contains(id)) {
        return {};
    }
    return links_[id.index].next_sibling;
}

bool NodeTree::path_to_root(NodeId id, std::span<NodeId> path, std::size_t& length) const noexcept {
    length = 0;
    if (!contains(id)) {
        return false;
    }

    for (auto current = id; !current.is_null(); current = links_[current.index].parent) {
        ++length;
    }
    if (path.size() < length) {
        return false;
    }

    auto current = id;
    for (std::size_t index = 0; index < length; ++index) {
        path[index] = current;
        current = links_[current.index].parent;
    }
    return true;
}

bool NodeTree::mark_dirty(NodeId id, DirtyCategory category) noexcept {
    const auto mask = dirty_mask(category);
    if (!contains(id) || !is_single_dirty_category(mask)) {
        return false;
    }

    const auto was_dirty = (links_[id.index].dirty_flags & mask) != 0;
    mark_dirty_unchecked(id, category);
    return !was_dirty;
}

bool NodeTree::clear_dirty(NodeId id, DirtyCategory category) noexcept {
    const auto mask = dirty_mask(category);
    if (!contains(id) || !is_single_dirty_category(mask)) {
        return false;
    }

    auto& flags = links_[id.index].dirty_flags;
    if ((flags & mask) == 0) {
        return false;
    }

    flags = static_cast<std::uint8_t>(flags & ~mask);
    if (flags == 0) {
        --dirty_node_count_;
    }
    return true;
}

bool NodeTree::is_dirty(NodeId id, DirtyCategory category) const noexcept {
    const auto mask = dirty_mask(category);
    if (!contains(id) || !is_single_dirty_category(mask)) {
        return false;
    }

    return (links_[id.index].dirty_flags & mask) != 0;
}

std::size_t NodeTree::size() const noexcept {
    return ids_.size();
}

std::size_t NodeTree::dirty_node_count() const noexcept {
    return dirty_node_count_;
}

void NodeTree::mark_dirty_unchecked(NodeId id, DirtyCategory category) noexcept {
    auto& flags = links_[id.index].dirty_flags;
    const auto mask = dirty_mask(category);
    if ((flags & mask) != 0) {
        return;
    }

    if (flags == 0) {
        ++dirty_node_count_;
    }
    flags = static_cast<std::uint8_t>(flags | mask);
}

void NodeTree::clear_all_dirty_unchecked(NodeId id) noexcept {
    auto& flags = links_[id.index].dirty_flags;
    if (flags != 0) {
        --dirty_node_count_;
        flags = 0;
    }
}

}
