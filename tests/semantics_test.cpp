#include "veyra/core/semantics/semantics.hpp"
#include "veyra/layout/scroll/virtual_semantics.hpp"

#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

void check_snapshot_relationships_and_clipping() {
    veyra::NodeTree tree;
    tree.reserve(5);

    const auto outside = tree.create();
    const auto root = tree.create();
    const auto layout = tree.create();
    const auto checkbox = tree.create();
    const auto text = tree.create();
    require(tree.append_child(outside, root), "root attaches below an outside node");
    require(tree.append_child(root, layout), "layout node attaches");
    require(tree.append_child(layout, checkbox), "checkbox attaches");
    require(tree.append_child(checkbox, text), "text attaches");
    require(tree.set_focusable(checkbox, true), "checkbox becomes focusable");

    const std::array data{
        veyra::SemanticNodeData{
            .role = veyra::SemanticRole::dialog,
            .label = "Outside",
        },
        veyra::SemanticNodeData{
            .role = veyra::SemanticRole::window,
            .label = "Settings",
        },
        veyra::SemanticNodeData{},
        veyra::SemanticNodeData{
            .role = veyra::SemanticRole::checkbox,
            .label = "Notifications",
            .value = "Enabled",
            .states = veyra::state_flag(veyra::SemanticState::checked),
            .actions = veyra::action_flag(veyra::SemanticAction::activate),
        },
        veyra::SemanticNodeData{
            .role = veyra::SemanticRole::text,
            .value = "On",
        },
    };
    const std::array bounds{
        veyra::LayoutRect{.width = 5.0F, .height = 5.0F},
        veyra::LayoutRect{.width = 100.0F, .height = 100.0F},
        veyra::LayoutRect{.x = 10.0F, .y = 10.0F, .width = 50.0F, .height = 50.0F},
        veyra::LayoutRect{.x = 50.0F, .y = 40.0F, .width = 30.0F, .height = 30.0F},
        veyra::LayoutRect{.x = 52.0F, .y = 42.0F, .width = 4.0F, .height = 4.0F},
    };
    std::array<veyra::SemanticNode, 3> output{};
    std::size_t node_count = 0;

    const auto result =
        veyra::build_semantic_snapshot(tree, root, checkbox, data, bounds, output, node_count);
    require(result == veyra::SemanticSnapshotResult::complete, "snapshot builds");
    require(node_count == output.size(), "snapshot includes semantic nodes only");
    require(output[0].id == root, "snapshot follows preorder");
    require(output[0].parent.is_null(), "snapshot root has no outside semantic parent");
    require(output[0].bounds.width == 100.0F, "snapshot root bounds ignore outside ancestors");
    require(output[1].id == checkbox, "layout-only nodes are omitted");
    require(output[1].parent == root, "parent links skip omitted nodes");
    require(output[2].id == text, "semantic descendants follow their parent");
    require(output[2].parent == checkbox, "nearest semantic parent is retained");
    require(output[1].bounds.x == 50.0F, "child bounds clip to the layout ancestor");
    require(output[1].bounds.y == 40.0F, "vertical clipping uses every ancestor");
    require(output[1].bounds.width == 10.0F, "clipped width is reported");
    require(output[1].bounds.height == 20.0F, "clipped height is reported");
    require(output[1].value == "Enabled", "value is borrowed into the snapshot");
    require(
        veyra::has_state(output[1].states, veyra::SemanticState::checked),
        "checked state is preserved"
    );
    require(
        veyra::has_state(output[1].states, veyra::SemanticState::focused),
        "focused state comes from the current focus node"
    );
    require(
        veyra::has_state(output[1].states, veyra::SemanticState::focusable),
        "focusable state comes from the node tree"
    );
    require(
        veyra::has_action(output[1].actions, veyra::SemanticAction::activate),
        "supported actions are preserved"
    );
}

void check_capacity_and_invalid_data() {
    veyra::NodeTree tree;
    tree.reserve(4);

    const auto root = tree.create();
    const auto first = tree.create();
    const auto second = tree.create();
    const auto outside = tree.create();
    require(tree.append_child(root, first), "first child attaches");
    require(tree.append_child(root, second), "second child attaches");

    std::array data{
        veyra::SemanticNodeData{.role = veyra::SemanticRole::window},
        veyra::SemanticNodeData{.role = veyra::SemanticRole::button, .label = "First"},
        veyra::SemanticNodeData{.role = veyra::SemanticRole::button, .label = "Second"},
    };
    const std::array bounds{
        veyra::LayoutRect{.width = 80.0F, .height = 80.0F},
        veyra::LayoutRect{.width = 20.0F, .height = 20.0F},
        veyra::LayoutRect{.x = 20.0F, .width = 20.0F, .height = 20.0F},
    };
    std::array<veyra::SemanticNode, 1> short_output{
        veyra::SemanticNode{.id = second},
    };
    std::size_t node_count = 0;

    auto result =
        veyra::build_semantic_snapshot(tree, root, {}, data, bounds, short_output, node_count);
    require(
        result == veyra::SemanticSnapshotResult::insufficient_output,
        "short output buffer is reported"
    );
    require(node_count == 3, "short output reports required capacity");
    require(short_output[0].id == second, "short output is not partially written");

    result = veyra::build_semantic_snapshot(
        tree,
        root,
        {},
        data,
        std::span<const veyra::LayoutRect>{},
        short_output,
        node_count
    );
    require(result == veyra::SemanticSnapshotResult::missing_bounds, "missing bounds are reported");

    result = veyra::build_semantic_snapshot(
        tree,
        root,
        second,
        std::span<const veyra::SemanticNodeData>{},
        bounds,
        short_output,
        node_count
    );
    require(result == veyra::SemanticSnapshotResult::missing_node_data, "missing data is reported");

    auto invalid_bounds = bounds;
    invalid_bounds[1].height = -1.0F;
    std::array<veyra::SemanticNode, 3> output{};
    result =
        veyra::build_semantic_snapshot(tree, root, {}, data, invalid_bounds, output, node_count);
    require(result == veyra::SemanticSnapshotResult::invalid_bounds, "invalid bounds are rejected");

    data[1].states = veyra::state_flag(veyra::SemanticState::focused);
    result = veyra::build_semantic_snapshot(tree, root, {}, data, bounds, output, node_count);
    require(result == veyra::SemanticSnapshotResult::invalid_state, "focused state is derived");

    data[1].states = 0x8000U;
    result = veyra::build_semantic_snapshot(tree, root, {}, data, bounds, output, node_count);
    require(result == veyra::SemanticSnapshotResult::invalid_state, "unknown states are rejected");

    data[1].states = 0;
    data[1].actions = 0x8000U;
    result = veyra::build_semantic_snapshot(tree, root, {}, data, bounds, output, node_count);
    require(
        result == veyra::SemanticSnapshotResult::invalid_action,
        "unknown actions are rejected"
    );

    data[1].actions = 0;
    result = veyra::build_semantic_snapshot(tree, root, outside, data, bounds, output, node_count);
    require(result == veyra::SemanticSnapshotResult::invalid_focus, "outside focus is rejected");
}

void check_virtual_list_items_without_tree_nodes() {
    veyra::NodeTree tree;
    const auto list = tree.create();
    std::array extents{10.0F, 20.0F, 30.0F};
    std::array<double, 4> fenwick{};
    veyra::VirtualListIndex index;
    require(index.reset(extents, fenwick), "virtual list measurements initialize");

    veyra::VirtualSemanticNode item;
    auto result = veyra::virtual_semantic_item(
        tree,
        list,
        index,
        0xA57U,
        1,
        12.0,
        {.x = 5.0F, .y = 100.0F, .width = 200.0F, .height = 15.0F},
        {
            .role = veyra::SemanticRole::list_item,
            .label = "Second item",
            .value = "Ready",
            .actions = veyra::action_flag(veyra::SemanticAction::activate),
        },
        item
    );
    require(result == veyra::VirtualSemanticResult::complete, "visible item semantics are queried");
    require(
        item.list == list && item.key == 0xA57U && item.index == 1,
        "virtual identity is stable as the item moves"
    );
    require(item.role == veyra::SemanticRole::list_item, "virtual rows expose the list item role");
    require(item.position_in_set == 2 && item.set_size == 3, "set position does not need siblings");
    require(item.label == "Second item" && item.value == "Ready", "item text is preserved");
    require(item.bounds.y == 98.0F && item.bounds.height == 20.0F, "item bounds use scroll offset");
    require(item.visible, "partly visible item is reported visible");
    require(tree.size() == 1, "query does not create a node for the item");

    result = veyra::virtual_semantic_item(
        tree,
        list,
        index,
        0xB61U,
        2,
        0.0,
        {.x = 5.0F, .y = 100.0F, .width = 200.0F, .height = 15.0F},
        {.role = veyra::SemanticRole::list_item, .label = "Third item"},
        item
    );
    require(
        result == veyra::VirtualSemanticResult::complete,
        "offscreen item semantics are queried"
    );
    require(item.bounds.y == 130.0F && !item.visible, "offscreen item keeps its content bounds");

    result = veyra::virtual_semantic_item(
        tree,
        list,
        index,
        0U,
        3,
        0.0,
        {.x = 5.0F, .y = 100.0F, .width = 200.0F, .height = 15.0F},
        {.role = veyra::SemanticRole::list_item},
        item
    );
    require(
        result == veyra::VirtualSemanticResult::invalid_item_index,
        "past-end virtual item is rejected"
    );

    result = veyra::virtual_semantic_item(
        tree,
        list,
        index,
        0U,
        0,
        -1.0,
        {.width = 100.0F, .height = 20.0F},
        {.role = veyra::SemanticRole::list_item},
        item
    );
    require(
        result == veyra::VirtualSemanticResult::invalid_scroll_offset,
        "negative virtual scroll offset is rejected"
    );

    result = veyra::virtual_semantic_item(
        tree,
        list,
        index,
        0U,
        0,
        0.0,
        {.width = -1.0F, .height = 20.0F},
        {.role = veyra::SemanticRole::list_item},
        item
    );
    require(
        result == veyra::VirtualSemanticResult::invalid_viewport,
        "invalid viewport is rejected"
    );
}

}

int main() {
    try {
        check_snapshot_relationships_and_clipping();
        check_capacity_and_invalid_data();
        check_virtual_list_items_without_tree_nodes();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
