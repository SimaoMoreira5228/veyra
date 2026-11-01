#include "veyra/app/accessibility.hpp"
#include "veyra/core/semantics/semantics.hpp"
#include "veyra/platform/headless/recording_accessibility_sink.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
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

struct Fixture {
    veyra::NodeTree tree;
    veyra::NodeId root;
    veyra::NodeId field;
    std::array<veyra::SemanticNodeData, 2> data{};
    std::array<veyra::LayoutRect, 2> bounds{};
    std::array<veyra::SemanticNode, 4> nodes{};
    std::size_t node_count{};
    veyra::RecordingAccessibilitySink sink;
    veyra::app::AccessibilityBridge bridge{sink};

    Fixture() {
        tree.reserve(2);
        root = tree.create();
        field = tree.create();
        tree.append_child(root, field);
        tree.set_focusable(field, true);

        data[0] = {
            .role = veyra::SemanticRole::window,
            .label = "Main",
        };
        data[1] = {
            .role = veyra::SemanticRole::text_input,
            .label = "Password",
            .value = "hunter2",
            .states = veyra::state_flag(veyra::SemanticState::protected_value),
            .actions = veyra::action_flag(veyra::SemanticAction::set_value),
        };
        bounds[0] = {.width = 320.0F, .height = 200.0F};
        bounds[1] = {.x = 8.0F, .y = 8.0F, .width = 200.0F, .height = 24.0F};
    }

    bool snapshot(veyra::NodeId focused) {
        return veyra::build_semantic_snapshot(
                   tree,
                   root,
                   focused,
                   data,
                   bounds,
                   nodes,
                   node_count
               ) == veyra::SemanticSnapshotResult::complete;
    }
};

void check_nodes_carry_role_name_state_and_actions() {
    auto fixture = Fixture{};
    require(fixture.snapshot(fixture.field), "the snapshot builds with a protected field");
    require(fixture.node_count == 2, "both semantic nodes are present");

    require(
        fixture.bridge.publish_window(
            veyra::WindowId{7},
            std::span<const veyra::SemanticNode>(fixture.nodes.data(), fixture.node_count)
        ) == veyra::app::AccessibilityPublishResult::complete,
        "the window publishes"
    );

    require(fixture.sink.begin_count() == 1 && fixture.sink.end_count() == 1, "one window bracket");
    require(fixture.sink.window() == veyra::WindowId{7}, "the sink sees the window id");
    require(fixture.sink.node_count() == 2, "both nodes reach the sink");

    const auto* window = fixture.sink.at(0);
    const auto* field = fixture.sink.at(1);
    require(window != nullptr && field != nullptr, "both records resolve");
    require(window->role == veyra::SemanticRole::window, "the window role maps");
    require(window->name == "Main", "the window name maps");
    require(field->role == veyra::SemanticRole::text_input, "the field role maps");
    require(field->name == "Password", "the field name maps");
    require(
        veyra::has_action(field->actions, veyra::SemanticAction::set_value),
        "the field action maps"
    );
    require(
        veyra::has_state(field->states, veyra::SemanticState::focused) &&
            veyra::has_state(field->states, veyra::SemanticState::focusable),
        "the focused node carries focus state"
    );
    require(field->parent == fixture.root, "the semantic parent maps");
}

void check_focus_and_state_changes_reach_the_sink() {
    auto fixture = Fixture{};
    require(fixture.snapshot(fixture.field), "the field-focused snapshot builds");
    fixture.bridge.publish_window(
        veyra::WindowId{8},
        std::span<const veyra::SemanticNode>(fixture.nodes.data(), fixture.node_count)
    );
    require(
        veyra::has_state(fixture.sink.at(1)->states, veyra::SemanticState::focused),
        "the field starts focused"
    );

    fixture.sink.reset();
    require(fixture.snapshot(fixture.root), "the root-focused snapshot builds");
    fixture.bridge.publish_window(
        veyra::WindowId{8},
        std::span<const veyra::SemanticNode>(fixture.nodes.data(), fixture.node_count)
    );
    require(
        veyra::has_state(fixture.sink.at(0)->states, veyra::SemanticState::focused),
        "focus moves to the root"
    );
    require(
        !veyra::has_state(fixture.sink.at(1)->states, veyra::SemanticState::focused),
        "the field loses its focused state"
    );

    fixture.sink.reset();
    fixture.data[1].states =
        fixture.data[1].states | veyra::state_flag(veyra::SemanticState::disabled);
    require(fixture.snapshot(fixture.field), "the disabled snapshot builds");
    fixture.bridge.publish_window(
        veyra::WindowId{8},
        std::span<const veyra::SemanticNode>(fixture.nodes.data(), fixture.node_count)
    );
    require(
        veyra::has_state(fixture.sink.at(1)->states, veyra::SemanticState::disabled),
        "a state change reaches the sink"
    );
}

void check_protected_values_are_redacted() {
    auto fixture = Fixture{};
    require(fixture.snapshot(fixture.field), "the protected snapshot builds");
    fixture.bridge.publish_window(
        veyra::WindowId{9},
        std::span<const veyra::SemanticNode>(fixture.nodes.data(), fixture.node_count)
    );

    const auto* field = fixture.sink.at(1);
    require(field->value.empty(), "a protected value is redacted");
    require(field->name == "Password", "the name survives redaction");
    require(
        veyra::has_state(field->states, veyra::SemanticState::protected_value),
        "the protected state is preserved for adapters"
    );
    require(
        field->value.find("hunter2") == std::string_view::npos,
        "the secret never reaches the sink"
    );

    fixture.sink.reset();
    fixture.data[1].label = "";
    require(fixture.snapshot(fixture.field), "the label-free protected snapshot builds");
    fixture.bridge.publish_window(
        veyra::WindowId{9},
        std::span<const veyra::SemanticNode>(fixture.nodes.data(), fixture.node_count)
    );
    require(
        fixture.sink.at(1)->name.empty(),
        "an empty label cannot leak a protected value as name"
    );
}

void check_virtualized_children_keep_set_metadata() {
    const auto list = veyra::NodeId{.index = 3, .generation = 1};
    const auto items = std::array<veyra::VirtualSemanticNode, 2>{
        veyra::VirtualSemanticNode{
            .list = list,
            .key = 41,
            .index = 2,
            .position_in_set = 3,
            .set_size = 1000,
            .label = "Report.pdf",
            .bounds = {.y = 40.0F, .width = 200.0F, .height = 20.0F},
            .states = veyra::state_flag(veyra::SemanticState::selected),
            .visible = true,
        },
        veyra::VirtualSemanticNode{
            .list = list,
            .key = 42,
            .index = 3,
            .position_in_set = 4,
            .set_size = 1000,
            .label = "Notes.txt",
            .bounds = {.y = 60.0F, .width = 200.0F, .height = 20.0F},
            .visible = false,
        },
    };

    auto sink = veyra::RecordingAccessibilitySink{};
    auto bridge = veyra::app::AccessibilityBridge(sink);
    require(
        bridge.publish_virtual_list(veyra::WindowId{10}, list, items) ==
            veyra::app::AccessibilityPublishResult::complete,
        "virtual children publish"
    );
    require(sink.node_count() == 2, "every virtual child reaches the sink");

    const auto* first = sink.at(0);
    require(first->virtualized, "the record is marked virtualized");
    require(first->parent == list, "the record points at its list");
    require(first->item_key == 41, "the stable item key maps");
    require(first->position_in_set == 3 && first->set_size == 1000, "set metadata maps");
    require(first->name == "Report.pdf", "the item name maps");
    require(veyra::has_state(first->states, veyra::SemanticState::selected), "the item state maps");
    require(first->visible && !sink.at(1)->visible, "visibility maps without filtering");
}

void check_invalid_inputs_do_not_touch_the_sink() {
    auto fixture = Fixture{};
    require(fixture.snapshot(fixture.field), "the snapshot builds");

    require(
        fixture.bridge.publish_window(
            veyra::kInvalidWindowId,
            std::span<const veyra::SemanticNode>(fixture.nodes.data(), fixture.node_count)
        ) == veyra::app::AccessibilityPublishResult::invalid_window,
        "a null window is rejected"
    );
    require(fixture.sink.begin_count() == 0, "a rejected window never opens");

    const auto null_node = std::array<veyra::SemanticNode, 1>{veyra::SemanticNode{}};
    require(
        fixture.bridge.publish_window(veyra::WindowId{11}, null_node) ==
            veyra::app::AccessibilityPublishResult::invalid_node,
        "a null node is rejected"
    );
    require(fixture.sink.begin_count() == 0, "a rejected node never opens the window");

    const auto items = std::array<veyra::VirtualSemanticNode, 1>{};
    require(
        fixture.bridge.publish_virtual_list(veyra::WindowId{11}, {}, items) ==
            veyra::app::AccessibilityPublishResult::invalid_node,
        "a null virtual list is rejected"
    );

    require(
        fixture.bridge.publish_window(veyra::WindowId{11}, {}) ==
            veyra::app::AccessibilityPublishResult::complete,
        "an empty window still publishes"
    );
    require(
        fixture.sink.begin_count() == 1 && fixture.sink.end_count() == 1,
        "an empty window keeps its bracket"
    );
}

}

int main() {
    try {
        check_nodes_carry_role_name_state_and_actions();
        check_focus_and_state_changes_reach_the_sink();
        check_protected_values_are_redacted();
        check_virtualized_children_keep_set_metadata();
        check_invalid_inputs_do_not_touch_the_sink();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';

        return 1;
    }

    std::cout << "accessibility bridge checks passed\n";

    return 0;
}
