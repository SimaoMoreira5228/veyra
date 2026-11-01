#include "veyra/app/accessibility.hpp"

namespace veyra::app {
namespace {

AccessibilityNode map_node(WindowId window, const SemanticNode& source) noexcept {
    const auto redact = accessibility_value_protected(source.states);
    const auto value = redact ? std::string_view{} : source.value;

    return {
        .window = window,
        .id = source.id,
        .parent = source.parent,
        .role = source.role,
        .name = source.label.empty() ? value : source.label,
        .value = value,
        .bounds = source.bounds,
        .states = source.states,
        .actions = source.actions,
    };
}

AccessibilityNode
map_virtual_item(WindowId window, NodeId list, const VirtualSemanticNode& source) noexcept {
    const auto redact = accessibility_value_protected(source.states);
    const auto value = redact ? std::string_view{} : source.value;

    return {
        .window = window,
        .id = {},
        .parent = list,
        .role = source.role,
        .name = source.label.empty() ? value : source.label,
        .value = value,
        .bounds = source.bounds,
        .states = source.states,
        .actions = source.actions,
        .item_key = source.key,
        .position_in_set = source.position_in_set,
        .set_size = source.set_size,
        .virtualized = true,
        .visible = source.visible,
    };
}

}

AccessibilityBridge::AccessibilityBridge(AccessibilitySink& sink) noexcept : sink_(&sink) {
}

AccessibilityPublishResult
AccessibilityBridge::publish_window(WindowId window, std::span<const SemanticNode> nodes) noexcept {
    if (window == kInvalidWindowId) {
        return AccessibilityPublishResult::invalid_window;
    }

    for (const auto& source : nodes) {
        if (source.id.is_null()) {
            return AccessibilityPublishResult::invalid_node;
        }
    }

    sink_->begin_window(window);
    for (const auto& source : nodes) {
        sink_->node(map_node(window, source));
    }
    sink_->end_window(window);

    return AccessibilityPublishResult::complete;
}

AccessibilityPublishResult AccessibilityBridge::publish_virtual_list(
    WindowId window,
    NodeId list,
    std::span<const VirtualSemanticNode> items
) noexcept {
    if (window == kInvalidWindowId) {
        return AccessibilityPublishResult::invalid_window;
    }
    if (list.is_null()) {
        return AccessibilityPublishResult::invalid_node;
    }

    sink_->begin_window(window);
    for (const auto& source : items) {
        sink_->node(map_virtual_item(window, list, source));
    }
    sink_->end_window(window);

    return AccessibilityPublishResult::complete;
}

}
