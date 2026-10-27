#include "veyra/core/tree/node_tree.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

enum class EventType : std::uint8_t {
    create,
    append_child,
    detach,
    destroy_subtree,
    text_changed,
    image_changed,
};

struct Event {
    std::uint64_t tick;
    EventType type;
    veyra::NodeId node;
    veyra::NodeId parent;
};

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

bool apply(veyra::NodeTree& tree, const Event& event) {
    switch (event.type) {
    case EventType::create:
        return tree.create() == event.node;
    case EventType::append_child:
        return tree.append_child(event.parent, event.node);
    case EventType::detach:
        return tree.detach(event.node);
    case EventType::destroy_subtree:
        return tree.destroy_subtree(event.node);
    case EventType::text_changed:
        return tree.mark_dirty(event.node, veyra::DirtyCategory::text);
    case EventType::image_changed:
        return tree.mark_dirty(event.node, veyra::DirtyCategory::image);
    }

    return false;
}

std::uint64_t replay() {
    constexpr veyra::NodeId root{0, 1};
    constexpr veyra::NodeId branch{1, 1};
    constexpr veyra::NodeId child{2, 1};
    constexpr veyra::NodeId removed{3, 1};
    constexpr veyra::NodeId reused{3, 2};
    constexpr std::array events{
        Event{1, EventType::create, root, {}},
        Event{2, EventType::create, branch, {}},
        Event{3, EventType::create, child, {}},
        Event{4, EventType::create, removed, {}},
        Event{5, EventType::append_child, branch, root},
        Event{6, EventType::append_child, child, branch},
        Event{7, EventType::detach, child, {}},
        Event{8, EventType::destroy_subtree, removed, {}},
        Event{9, EventType::create, reused, {}},
        Event{10, EventType::append_child, reused, root},
        Event{11, EventType::detach, branch, {}},
        Event{12, EventType::text_changed, child, {}},
        Event{13, EventType::image_changed, reused, {}},
    };
    constexpr std::array nodes{root, branch, child, removed, reused};

    veyra::NodeTree tree;
    tree.reserve(nodes.size());

    std::uint64_t previous_tick = 0;
    for (const auto& event : events) {
        require(event.tick > previous_tick, "event ticks increase strictly");
        require(apply(tree, event), "event replays successfully");
        previous_tick = event.tick;
    }

    require(tree.size() == 4, "replay produces the expected live node count");
    require(tree.dirty_node_count() == 4, "replay preserves invalidation state");
    require(tree.contains(root), "root remains live");
    require(tree.contains(branch), "detached branch remains live");
    require(tree.contains(child), "detached child remains live");
    require(!tree.contains(removed), "destroyed id stays stale after replay");
    require(tree.contains(reused), "reused slot has the recorded generation");
    require(tree.first_child(root) == reused, "replay preserves the final child order");
    require(tree.parent_of(branch).is_null(), "detached branch is a root");
    require(tree.parent_of(child).is_null(), "detached child is a root");

    std::uint64_t checksum = tree.size() + tree.dirty_node_count();
    for (const auto node : nodes) {
        const auto live = tree.contains(node);
        const auto parent = tree.parent_of(node);
        const auto first_child = tree.first_child(node);
        const auto next_sibling = tree.next_sibling(node);
        checksum = checksum * 1099511628211ULL + static_cast<std::uint64_t>(live);
        checksum = checksum * 1099511628211ULL + parent.index;
        checksum = checksum * 1099511628211ULL + parent.generation;
        checksum = checksum * 1099511628211ULL + first_child.index;
        checksum = checksum * 1099511628211ULL + first_child.generation;
        checksum = checksum * 1099511628211ULL + next_sibling.index;
        checksum = checksum * 1099511628211ULL + next_sibling.generation;
        checksum = checksum * 1099511628211ULL +
            static_cast<std::uint64_t>(tree.is_dirty(node, veyra::DirtyCategory::structure));
        checksum = checksum * 1099511628211ULL +
            static_cast<std::uint64_t>(tree.is_dirty(node, veyra::DirtyCategory::layout));
        checksum = checksum * 1099511628211ULL +
            static_cast<std::uint64_t>(tree.is_dirty(node, veyra::DirtyCategory::paint));
        checksum = checksum * 1099511628211ULL +
            static_cast<std::uint64_t>(tree.is_dirty(node, veyra::DirtyCategory::semantics));
        checksum = checksum * 1099511628211ULL +
            static_cast<std::uint64_t>(tree.is_dirty(node, veyra::DirtyCategory::text));
        checksum = checksum * 1099511628211ULL +
            static_cast<std::uint64_t>(tree.is_dirty(node, veyra::DirtyCategory::image));
    }
    return checksum;
}

}

int main() {
    try {
        const auto first_replay = replay();
        const auto second_replay = replay();
        require(first_replay == second_replay, "identical event streams produce identical state");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
