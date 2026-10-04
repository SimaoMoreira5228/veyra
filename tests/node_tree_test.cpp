#include "veyra/core/node_tree.hpp"

#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

void check_hierarchy_and_lifetimes() {
    veyra::NodeTree tree;
    tree.reserve(6);

    const auto first_root = tree.create();
    const auto second_root = tree.create();
    const auto first_child = tree.create();
    const auto second_child = tree.create();
    const auto grandchild = tree.create();
    const auto second_grandchild = tree.create();

    require(tree.append_child(first_root, first_child), "first child attaches");
    require(tree.append_child(first_root, second_child), "second child attaches");
    require(tree.append_child(first_child, grandchild), "grandchild attaches");
    require(tree.append_child(first_child, second_grandchild), "sibling grandchild attaches");
    require(tree.parent_of(first_child) == first_root, "parent link is recorded");
    require(tree.first_child(first_root) == first_child, "first child preserves insertion order");
    require(
        tree.next_sibling(first_child) == second_child,
        "sibling link preserves insertion order"
    );
    std::array<veyra::NodeId, 3> path{};
    std::size_t path_length = 0;
    require(tree.path_to_root(grandchild, path, path_length), "hierarchy path fits caller storage");
    require(path_length == 3, "hierarchy path includes every ancestor");
    require(path[0] == grandchild, "hierarchy path starts at the target");
    require(path[1] == first_child, "hierarchy path walks through the parent");
    require(path[2] == first_root, "hierarchy path ends at the root");

    std::array too_small{veyra::NodeId{90, 90}, veyra::NodeId{91, 91}};
    require(
        !tree.path_to_root(grandchild, too_small, path_length),
        "short route buffer is rejected"
    );
    require(path_length == 3, "short route reports the required capacity");
    require(too_small[0].index == 90, "short route leaves caller storage unchanged");
    require(too_small[1].index == 91, "short route does not partially write results");

    require(
        !tree.append_child(grandchild, first_root),
        "an ancestor cannot be attached below its child"
    );

    require(tree.append_child(second_root, first_child), "attached child can be reparented");
    require(
        tree.first_child(first_root) == second_child,
        "reparenting detaches from the old parent"
    );
    require(
        tree.first_child(second_root) == first_child,
        "reparented child appears under its new parent"
    );
    require(tree.next_sibling(first_child).is_null(), "reparenting clears the old sibling link");

    require(tree.detach(second_child), "child can be detached");
    require(tree.first_child(first_root).is_null(), "detaching the last child clears the parent");
    require(tree.destroy_subtree(first_child), "node and descendants can be destroyed together");
    require(!tree.contains(first_child), "destroyed subtree root becomes stale");
    require(!tree.contains(grandchild), "destroyed descendant becomes stale");
    require(!tree.contains(second_grandchild), "destroyed sibling descendant becomes stale");
    require(tree.first_child(second_root).is_null(), "destroyed subtree detaches from its parent");
    require(tree.destroy_subtree(second_child), "detached leaf can be destroyed");
    require(!tree.contains(second_child), "destroyed node id becomes stale");

    const auto reused = tree.create();
    require(reused.index == second_child.index, "destroyed slot is reused");
    require(reused.generation != second_child.generation, "reused slot advances its generation");
    require(!tree.contains(second_child), "stale id cannot access a reused tree slot");
    require(tree.size() == 3, "tree counts live nodes");
}

void check_dirty_tracking_and_moves() {
    veyra::NodeTree tree;
    tree.reserve(3);

    const auto root = tree.create();
    const auto child = tree.create();
    const auto grandchild = tree.create();

    require(tree.dirty_node_count() == 3, "created nodes start dirty");
    require(tree.dirty_nodes().size() == 3, "dirty nodes can be enumerated directly");
    require(tree.append_child(root, child), "child attaches for dirty tracking");
    require(tree.append_child(child, grandchild), "grandchild attaches for dirty tracking");
    require(
        tree.is_dirty(root, veyra::DirtyCategory::structure),
        "parent tracks structure changes"
    );
    require(tree.is_dirty(root, veyra::DirtyCategory::layout), "parent tracks layout changes");
    require(tree.is_dirty(child, veyra::DirtyCategory::layout), "child tracks layout changes");

    require(tree.mark_dirty(root, veyra::DirtyCategory::paint), "new dirty category is recorded");
    require(
        !tree.mark_dirty(root, veyra::DirtyCategory::paint),
        "duplicate dirty category coalesces"
    );
    require(
        tree.clear_dirty(root, veyra::DirtyCategory::structure),
        "dirty category can be cleared"
    );
    require(tree.clear_dirty(root, veyra::DirtyCategory::layout), "layout category can be cleared");
    require(tree.dirty_node_count() == 3, "node count stays dirty while a category remains");

    require(tree.destroy_subtree(child), "dirty subtree can be destroyed");
    require(tree.dirty_node_count() == 1, "destroyed nodes leave the dirty count");
    require(tree.dirty_nodes().size() == 1, "destroyed nodes leave the dirty index");
    require(
        tree.is_dirty(root, veyra::DirtyCategory::paint),
        "parent paint dirtiness is preserved"
    );

    veyra::NodeTree moved_tree(std::move(tree));
    require(moved_tree.dirty_node_count() == 1, "move preserves dirty node count");
    require(
        moved_tree.dirty_nodes().size() == 1 && moved_tree.dirty_nodes()[0] == root,
        "move preserves dirty node identities"
    );
    require(
        moved_tree.clear_dirty(root, veyra::DirtyCategory::paint),
        "moved tree can clear dirty state"
    );
    require(moved_tree.dirty_node_count() == 1, "remaining categories keep the node dirty");
    require(
        moved_tree.clear_dirty(root, veyra::DirtyCategory::structure),
        "structure can be cleared"
    );
    require(moved_tree.clear_dirty(root, veyra::DirtyCategory::layout), "layout can be cleared");
    require(moved_tree.dirty_node_count() == 0, "clearing the last category resets dirty count");
    require(moved_tree.dirty_nodes().empty(), "clearing the last category removes the node");

    veyra::NodeTree assigned_tree;
    assigned_tree.create();
    const auto replaced = assigned_tree.create();
    assigned_tree = std::move(moved_tree);
    require(assigned_tree.contains(root), "move assignment preserves node identities");
    require(!assigned_tree.contains(replaced), "move assignment replaces existing nodes");
    require(assigned_tree.dirty_node_count() == 0, "move assignment preserves dirty count");

    const veyra::NodeId invalid{};
    require(
        !assigned_tree.mark_dirty(invalid, veyra::DirtyCategory::paint),
        "invalid id is rejected"
    );
}

void check_independent_text_and_image_invalidation() {
    veyra::NodeTree tree;
    const auto node = tree.create();
    const auto peer = tree.create();
    require(
        tree.clear_dirty(node, veyra::DirtyCategory::structure),
        "new node structure is cleared"
    );
    require(tree.clear_dirty(peer, veyra::DirtyCategory::structure), "peer structure is cleared");

    require(tree.mark_dirty(node, veyra::DirtyCategory::text), "text work can be marked dirty");
    require(tree.mark_dirty(node, veyra::DirtyCategory::image), "image work can be marked dirty");
    require(tree.is_dirty(node, veyra::DirtyCategory::text), "text dirtiness is queryable");
    require(tree.is_dirty(node, veyra::DirtyCategory::image), "image dirtiness is queryable");
    require(
        !tree.is_dirty(node, veyra::DirtyCategory::paint),
        "content dirtiness stays separate from paint"
    );
    require(tree.dirty_node_count() == 1, "multiple dirty categories count one node");
    require(
        tree.dirty_nodes().size() == 1 && tree.dirty_nodes()[0] == node,
        "dirty-node index contains one entry per node"
    );
    require(!tree.mark_dirty(node, veyra::DirtyCategory::text), "repeated text work coalesces");

    require(tree.clear_dirty(node, veyra::DirtyCategory::text), "text work can be cleared alone");
    require(!tree.is_dirty(node, veyra::DirtyCategory::text), "cleared text work is clean");
    require(tree.is_dirty(node, veyra::DirtyCategory::image), "clearing text preserves image work");
    require(
        tree.mark_dirty(peer, veyra::DirtyCategory::paint),
        "peer can be dirtied independently"
    );
    require(tree.clear_dirty(node, veyra::DirtyCategory::image), "image work can be cleared alone");
    require(tree.dirty_node_count() == 1, "clean node leaves its peer dirty");
    require(
        tree.dirty_nodes().size() == 1 && tree.dirty_nodes()[0] == peer,
        "removing one node preserves the swapped dirty-node index"
    );
    require(tree.clear_dirty(peer, veyra::DirtyCategory::paint), "peer paint clears");
    require(tree.dirty_node_count() == 0, "clearing the last category resets the node count");
    require(tree.dirty_nodes().empty(), "clean nodes leave the dirty-node index");
}

void check_atomic_child_batches() {
    veyra::NodeTree tree;
    tree.reserve(6);

    const auto old_parent = tree.create();
    const auto new_parent = tree.create();
    const auto first_child = tree.create();
    const auto second_child = tree.create();
    const auto descendant = tree.create();

    require(tree.append_child(old_parent, first_child), "first child starts under old parent");
    require(tree.append_child(old_parent, second_child), "second child starts under old parent");
    require(tree.append_child(first_child, descendant), "descendant attaches below first child");

    const auto stale_child = tree.create();
    require(tree.destroy_subtree(stale_child), "stale batch target is destroyed");

    const std::array invalid_batch{first_child, stale_child};
    require(
        !tree.append_children(new_parent, invalid_batch),
        "stale child rejects the whole batch"
    );
    require(
        tree.parent_of(first_child) == old_parent,
        "failed batch leaves earlier children in place"
    );
    require(tree.next_sibling(first_child) == second_child, "failed batch preserves sibling order");

    const std::array valid_batch{first_child, second_child};
    require(tree.append_children(new_parent, valid_batch), "valid child batch commits");
    require(tree.first_child(new_parent) == first_child, "batch preserves input order");
    require(tree.next_sibling(first_child) == second_child, "batch links siblings in input order");

    const std::array cyclic_batch{second_child, first_child};
    require(!tree.append_children(descendant, cyclic_batch), "cycle rejects the whole batch");
    require(
        tree.parent_of(second_child) == new_parent,
        "cycle rejection leaves earlier children in place"
    );
}

}

int main() {
    try {
        check_hierarchy_and_lifetimes();
        check_dirty_tracking_and_moves();
        check_independent_text_and_image_invalidation();
        check_atomic_child_batches();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
