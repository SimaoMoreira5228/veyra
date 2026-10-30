#ifndef VEYRA_EXAMPLES_APP_SEMANTICS_HPP
#define VEYRA_EXAMPLES_APP_SEMANTICS_HPP

#include "veyra/app/window_semantics.hpp"
#include "veyra/core/tree/node_tree.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace veyra {

class AppSemantics {
  public:
    static constexpr std::size_t kRowCount = 20;
    static constexpr std::size_t kNodeCount = 5 + kRowCount;

    AppSemantics();

    NodeTree& tree() noexcept;
    std::span<const LayoutRect> bounds() const noexcept {
        return bounds_;
    }

    NodeId focus_node(std::uint64_t id) const noexcept;
    SemanticSnapshotResult snapshot(
        const WindowSet& windows,
        WindowId window,
        std::span<SemanticNode> output,
        std::size_t& count
    ) const noexcept;

    NodeId root{};
    NodeId inspector_root{};

  private:
    NodeTree tree_;
    std::array<NodeId, 4> focus_nodes_{};
    std::array<SemanticNodeData, kNodeCount> data_{};
    std::array<LayoutRect, kNodeCount> bounds_{};
};

inline AppSemantics::AppSemantics() {
    tree_.reserve(kNodeCount);
    root = tree_.create();
    const auto list = tree_.create();
    const auto field = tree_.create();
    const auto button = tree_.create();

    focus_nodes_[1] = list;
    focus_nodes_[2] = field;
    focus_nodes_[3] = button;
    tree_.set_focusable(list, true);
    tree_.set_focusable(field, true);
    tree_.set_focusable(button, true);
    tree_.append_child(root, list);
    tree_.append_child(root, field);
    tree_.append_child(root, button);

    auto rows = std::array<NodeId, kRowCount>{};
    for (auto index = std::size_t{}; index < rows.size(); ++index) {
        rows[index] = tree_.create();
    }
    tree_.append_children(list, rows);
    inspector_root = tree_.create();

    data_[root.index] = {.role = SemanticRole::window, .label = "Veyra demo"};
    data_[list.index] = {.role = SemanticRole::list, .label = "Messages"};
    data_[field.index] = {.role = SemanticRole::text_input, .label = "Command"};
    data_[button.index] = {
        .role = SemanticRole::button,
        .label = "Send",
        .actions = action_flag(SemanticAction::activate),
    };
    for (const auto row : rows) {
        data_[row.index] = {.role = SemanticRole::list_item, .label = "Message"};
    }
    data_[inspector_root.index] = {.role = SemanticRole::window, .label = "Inspector"};

    bounds_[root.index] = {.width = 480.0F, .height = 320.0F};
    bounds_[list.index] = {.x = 8.0F, .y = 48.0F, .width = 280.0F, .height = 264.0F};
    bounds_[field.index] = {.x = 296.0F, .y = 8.0F, .width = 176.0F, .height = 24.0F};
    bounds_[button.index] = {.x = 296.0F, .y = 40.0F, .width = 72.0F, .height = 24.0F};
    for (auto index = std::size_t{}; index < rows.size(); ++index) {
        bounds_[rows[index].index] = {
            .x = 12.0F,
            .y = 52.0F + static_cast<float>(index) * 12.0F,
            .width = 272.0F,
            .height = 10.0F,
        };
    }
    bounds_[inspector_root.index] = {.width = 240.0F, .height = 160.0F};
}

inline NodeTree& AppSemantics::tree() noexcept {
    return tree_;
}

inline NodeId AppSemantics::focus_node(std::uint64_t id) const noexcept {
    if (id >= focus_nodes_.size()) {
        return {};
    }
    return focus_nodes_[id];
}

inline SemanticSnapshotResult AppSemantics::snapshot(
    const WindowSet& windows,
    WindowId window,
    std::span<SemanticNode> output,
    std::size_t& count
) const noexcept {
    return app::build_window_snapshot(tree_, windows, window, data_, bounds_, output, count);
}

}

#endif
