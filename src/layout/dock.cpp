#include "veyra/layout/dock.hpp"

#include "veyra/layout/flex.hpp"
#include "veyra/layout/split_pane.hpp"

#include <cmath>

namespace veyra {

static_assert(static_cast<DockResult>(TabsResult::ignored) == DockResult::ignored);
static_assert(static_cast<DockResult>(TabsResult::handled) == DockResult::handled);
static_assert(static_cast<DockResult>(TabsResult::changed) == DockResult::changed);

DockController::DockController() noexcept {
    nodes_[0].used = true;
    nodes_[0].id = next_id_;
    root_ = next_id_;
    ++next_id_;
    used_ = 1;
}

std::uint64_t DockController::root() const noexcept {
    return root_;
}

std::size_t DockController::node_count() const noexcept {
    return used_;
}

bool DockController::contains(std::uint64_t id) const noexcept {
    return index_of(id) != kNotFound;
}

bool DockController::is_split(std::uint64_t id) const noexcept {
    const std::size_t at = index_of(id);

    if (at == kNotFound) {
        return false;
    }

    return nodes_[at].is_split;
}

std::size_t DockController::tab_count(std::uint64_t leaf) const noexcept {
    const std::size_t at = index_of(leaf);

    if (at == kNotFound || nodes_[at].is_split) {
        return 0;
    }

    return nodes_[at].tab_count;
}

std::uint64_t DockController::active_panel(std::uint64_t leaf) const noexcept {
    const std::size_t at = index_of(leaf);

    if (at == kNotFound || nodes_[at].is_split) {
        return 0;
    }

    const Node& node = nodes_[at];

    if (node.tab_count == 0) {
        return 0;
    }

    const std::uint64_t selected = strips_[at].selected();

    if (strips_[at].has_selected()) {
        return selected;
    }

    return node.tabs[0];
}

std::size_t DockController::copy_leaves(std::span<std::uint64_t> output) const noexcept {
    std::size_t next = 0;

    collect_leaves(root_, output, next);

    return next;
}

DockResult DockController::split(
    std::uint64_t leaf,
    DockSplit direction,
    float ratio,
    std::uint64_t& new_leaf
) noexcept {
    if (!std::isfinite(ratio)) {
        return DockResult::ignored;
    }

    const std::size_t at = index_of(leaf);

    if (at == kNotFound || nodes_[at].is_split) {
        return DockResult::ignored;
    }

    if (used_ + 2 > kCapacity) {
        return DockResult::overflow;
    }

    const float clamped = clamp_unit(ratio);

    std::size_t first_slot = kNotFound;
    std::size_t second_slot = kNotFound;

    for (std::size_t i = 0; i < kCapacity; ++i) {
        if (nodes_[i].used) {
            continue;
        }

        if (first_slot == kNotFound) {
            first_slot = i;
        } else {
            second_slot = i;
            break;
        }
    }

    nodes_[first_slot].used = true;
    nodes_[first_slot].id = next_id_;
    ++next_id_;

    nodes_[second_slot].used = true;
    nodes_[second_slot].id = next_id_;
    ++next_id_;

    Node& node = nodes_[at];
    Node& first = nodes_[first_slot];
    Node& second = nodes_[second_slot];

    first.parent = node.id;
    first.tabs = node.tabs;
    first.tab_count = node.tab_count;
    strips_[first_slot] = strips_[at];

    second.parent = node.id;

    node.is_split = true;
    node.axis = direction;
    node.ratio = clamped;
    node.min_first = 0.0F;
    node.min_second = 0.0F;
    node.first = first.id;
    node.second = second.id;
    node.tabs = {};
    node.tab_count = 0;
    strips_[at] = TabsController{};

    used_ += 2;
    new_leaf = second.id;

    return DockResult::changed;
}

DockResult DockController::tab_add(std::uint64_t leaf, std::uint64_t panel) noexcept {
    const std::size_t at = index_of(leaf);

    if (at == kNotFound || nodes_[at].is_split) {
        return DockResult::ignored;
    }

    if (panel == 0) {
        return DockResult::ignored;
    }

    Node& node = nodes_[at];

    for (std::size_t i = 0; i < node.tab_count; ++i) {
        if (node.tabs[i] == panel) {
            return DockResult::handled;
        }
    }

    if (node.tab_count == kMaxTabs) {
        return DockResult::overflow;
    }

    node.tabs[node.tab_count] = panel;
    ++node.tab_count;

    const std::span<const std::uint64_t> members(node.tabs.data(), node.tab_count);
    strips_[at].select(panel, members, std::span<const std::uint64_t>{});

    return DockResult::changed;
}

DockResult DockController::tab_activate(std::uint64_t leaf, std::uint64_t panel) noexcept {
    const std::size_t at = index_of(leaf);

    if (at == kNotFound || nodes_[at].is_split) {
        return DockResult::ignored;
    }

    const Node& node = nodes_[at];
    const std::span<const std::uint64_t> members(node.tabs.data(), node.tab_count);

    return static_cast<DockResult>(
        strips_[at].select(panel, members, std::span<const std::uint64_t>{})
    );
}

DockResult DockController::tab_close(std::uint64_t leaf, std::uint64_t panel) noexcept {
    const std::size_t at = index_of(leaf);

    if (at == kNotFound || nodes_[at].is_split || panel == 0) {
        return DockResult::ignored;
    }

    Node& node = nodes_[at];
    std::size_t closing = kNotFound;

    for (std::size_t i = 0; i < node.tab_count; ++i) {
        if (node.tabs[i] == panel) {
            closing = i;
            break;
        }
    }

    if (closing == kNotFound) {
        return DockResult::ignored;
    }

    std::size_t parent = kNotFound;
    std::size_t sibling_slot = kNotFound;

    if (node.tab_count == 1 && node.parent != 0) {
        parent = index_of(node.parent);

        if (parent != kNotFound && nodes_[parent].is_split) {
            const std::uint64_t sibling =
                nodes_[parent].first == leaf ? nodes_[parent].second : nodes_[parent].first;
            sibling_slot = index_of(sibling);
        }
    }

    const std::span<const std::uint64_t> members(node.tabs.data(), node.tab_count);
    strips_[at].close_request(panel, members, std::span<const std::uint64_t>{});

    for (std::size_t i = closing + 1; i < node.tab_count; ++i) {
        node.tabs[i - 1] = node.tabs[i];
    }

    node.tabs[node.tab_count - 1] = 0;
    --node.tab_count;

    if (node.tab_count > 0 || parent == kNotFound || sibling_slot == kNotFound) {
        return DockResult::changed;
    }

    const std::uint64_t sibling = nodes_[sibling_slot].id;
    const std::uint64_t grand = nodes_[parent].parent;

    nodes_[sibling_slot].parent = grand;

    if (grand == 0) {
        root_ = sibling;
    } else {
        Node& holder = nodes_[index_of(grand)];

        if (holder.first == nodes_[parent].id) {
            holder.first = sibling;
        } else {
            holder.second = sibling;
        }
    }

    nodes_[at] = Node{};
    strips_[at] = TabsController{};
    nodes_[parent] = Node{};
    strips_[parent] = TabsController{};
    used_ -= 2;

    return DockResult::changed;
}

DockResult DockController::set_ratio(std::uint64_t node, float ratio) noexcept {
    if (!std::isfinite(ratio)) {
        return DockResult::ignored;
    }

    const std::size_t at = index_of(node);

    if (at == kNotFound || !nodes_[at].is_split) {
        return DockResult::ignored;
    }

    const float clamped = clamp_unit(ratio);

    if (nodes_[at].ratio == clamped) {
        return DockResult::handled;
    }

    nodes_[at].ratio = clamped;

    return DockResult::changed;
}

DockResult
DockController::set_minimums(std::uint64_t node, float min_first, float min_second) noexcept {
    if (!std::isfinite(min_first) || !std::isfinite(min_second) || min_first < 0.0F ||
        min_second < 0.0F) {
        return DockResult::ignored;
    }

    const std::size_t at = index_of(node);

    if (at == kNotFound || !nodes_[at].is_split) {
        return DockResult::ignored;
    }

    if (nodes_[at].min_first == min_first && nodes_[at].min_second == min_second) {
        return DockResult::handled;
    }

    nodes_[at].min_first = min_first;
    nodes_[at].min_second = min_second;

    return DockResult::changed;
}

DockResult DockController::set_gap(float gap) noexcept {
    if (!std::isfinite(gap) || gap < 0.0F) {
        return DockResult::ignored;
    }

    if (gap_ == gap) {
        return DockResult::handled;
    }

    gap_ = gap;

    return DockResult::changed;
}

DockResult DockController::resize(float width, float height) noexcept {
    if (!std::isfinite(width) || !std::isfinite(height) || width < 0.0F || height < 0.0F) {
        return DockResult::ignored;
    }

    if (bound_width_ == width && bound_height_ == height) {
        return DockResult::handled;
    }

    bound_width_ = width;
    bound_height_ = height;

    return DockResult::changed;
}

DockLayoutResult
DockController::layout(LayoutRect bounds, std::span<DockLeafRect> output) const noexcept {
    if (!std::isfinite(bounds.x) || !std::isfinite(bounds.y) || !std::isfinite(bounds.width) ||
        !std::isfinite(bounds.height)) {
        return DockLayoutResult::invalid_input;
    }

    if (bounds.width < 0.0F || bounds.height < 0.0F) {
        return DockLayoutResult::invalid_input;
    }

    std::array<DockLeafRect, kCapacity> staged{};
    std::size_t next = 0;
    bool overflow = false;
    const DockLayoutResult result = place(root_, bounds, staged, next, overflow);

    if (result != DockLayoutResult::complete) {
        return result;
    }

    if (next > output.size()) {
        return DockLayoutResult::insufficient_output;
    }

    for (std::size_t i = 0; i < next; ++i) {
        output[i] = staged[i];
    }

    if (overflow) {
        return DockLayoutResult::overflow;
    }

    return DockLayoutResult::complete;
}

std::size_t DockController::export_layout(std::span<DockRecord> output) const noexcept {
    std::size_t copied = 0;

    for (const Node& node : nodes_) {
        if (!node.used) {
            continue;
        }

        if (copied == output.size()) {
            break;
        }

        DockRecord& record = output[copied];
        record = DockRecord{
            .id = node.id,
            .parent = node.parent,
            .is_split = node.is_split,
            .axis = node.axis,
            .ratio = node.ratio,
            .min_first = node.min_first,
            .min_second = node.min_second,
            .first = node.first,
            .second = node.second,
            .tab_count = static_cast<std::uint32_t>(node.tab_count),
            .active = active_panel(node.id),
        };

        for (std::size_t i = 0; i < node.tab_count; ++i) {
            record.tabs[i] = node.tabs[i];
        }

        ++copied;
    }

    return copied;
}

DockResult DockController::import_layout(std::span<const DockRecord> records) noexcept {
    std::array<std::size_t, kCapacity> order{};
    std::size_t valid = 0;

    for (std::size_t i = 0; i < records.size(); ++i) {
        if (records[i].id == 0) {
            continue;
        }

        if (record_at(records, {order.data(), valid}, records[i].id) != kNotFound) {
            continue;
        }

        if (valid == kCapacity) {
            return DockResult::overflow;
        }

        order[valid] = i;
        ++valid;
    }

    if (valid == 0) {
        return DockResult::ignored;
    }

    const std::span<const std::size_t> known(order.data(), valid);
    std::uint64_t root_id = 0;

    for (std::size_t j = 0; j < valid; ++j) {
        const DockRecord& record = records[order[j]];

        if (record.parent == 0 || record_at(records, known, record.parent) == kNotFound) {
            root_id = record.id;
            break;
        }
    }

    if (root_id == 0) {
        return DockResult::ignored;
    }

    std::array<Node, kCapacity> next_nodes{};
    std::array<TabsController, kCapacity> next_strips{};
    std::array<std::uint64_t, kCapacity> path{};
    std::size_t placed = 0;
    std::uint64_t next_id = next_id_;

    const std::uint64_t built =
        build_record(records, known, root_id, 0, path, 0, next_nodes, next_strips, placed, next_id);

    if (built == 0 || placed == 0) {
        return DockResult::ignored;
    }

    nodes_ = next_nodes;
    strips_ = next_strips;
    root_ = built;
    used_ = placed;
    next_id_ = next_id;

    return DockResult::changed;
}

std::size_t DockController::record_at(
    std::span<const DockRecord> records,
    std::span<const std::size_t> order,
    std::uint64_t id
) const noexcept {
    if (id == 0) {
        return kNotFound;
    }

    for (std::size_t j = 0; j < order.size(); ++j) {
        if (records[order[j]].id == id) {
            return j;
        }
    }

    return kNotFound;
}

std::uint64_t DockController::build_record(
    std::span<const DockRecord> records,
    std::span<const std::size_t> order,
    std::uint64_t id,
    std::uint64_t parent,
    std::span<std::uint64_t> path,
    std::size_t depth,
    std::span<Node> nodes,
    std::span<TabsController> strips,
    std::size_t& placed,
    std::uint64_t& next_id
) noexcept {
    if (depth == path.size()) {
        return 0;
    }

    for (std::size_t i = 0; i < depth; ++i) {
        if (path[i] == id) {
            return 0;
        }
    }

    const std::size_t pos = record_at(records, order, id);

    if (pos == kNotFound || placed == nodes.size()) {
        return 0;
    }

    const DockRecord& record = records[order[pos]];
    const std::size_t slot = placed;
    ++placed;
    path[depth] = id;

    Node& node = nodes[slot];
    node.used = true;
    node.id = id;
    node.parent = parent;
    node.axis = record.axis;

    node.ratio = std::isfinite(record.ratio) ? clamp_unit(record.ratio) : 0.5F;
    node.min_first =
        std::isfinite(record.min_first) && record.min_first >= 0.0F ? record.min_first : 0.0F;
    node.min_second =
        std::isfinite(record.min_second) && record.min_second >= 0.0F ? record.min_second : 0.0F;

    const std::uint64_t first = build_record(
        records,
        order,
        record.first,
        id,
        path,
        depth + 1,
        nodes,
        strips,
        placed,
        next_id
    );
    const std::uint64_t second = build_record(
        records,
        order,
        record.second,
        id,
        path,
        depth + 1,
        nodes,
        strips,
        placed,
        next_id
    );

    node.is_split = record.is_split && first != 0 && second != 0;
    node.first = node.is_split ? first : 0;
    node.second = node.is_split ? second : 0;

    std::size_t tabs = record.tab_count;

    if (tabs > kMaxTabs) {
        tabs = kMaxTabs;
    }

    for (std::size_t t = 0; t < tabs; ++t) {
        const std::uint64_t panel = record.tabs[t];

        if (panel == 0) {
            continue;
        }

        bool duplicate = false;

        for (std::size_t u = 0; u < node.tab_count; ++u) {
            if (node.tabs[u] == panel) {
                duplicate = true;
                break;
            }
        }

        if (!duplicate) {
            node.tabs[node.tab_count] = panel;
            ++node.tab_count;
        }
    }

    if (node.tab_count > 0) {
        std::uint64_t active = node.tabs[0];

        for (std::size_t t = 0; t < node.tab_count; ++t) {
            if (node.tabs[t] == record.active) {
                active = record.active;
                break;
            }
        }

        const std::span<const std::uint64_t> members(node.tabs.data(), node.tab_count);
        strips[slot].select(active, members, std::span<const std::uint64_t>{});
    }

    if (node.id != static_cast<std::uint64_t>(-1) && node.id >= next_id) {
        next_id = node.id + 1;
    }

    return id;
}

std::size_t DockController::index_of(std::uint64_t id) const noexcept {
    if (id == 0) {
        return kNotFound;
    }

    for (std::size_t i = 0; i < kCapacity; ++i) {
        if (nodes_[i].used && nodes_[i].id == id) {
            return i;
        }
    }

    return kNotFound;
}

void DockController::collect_leaves(
    std::uint64_t id,
    std::span<std::uint64_t> output,
    std::size_t& next
) const noexcept {
    const std::size_t at = index_of(id);

    if (at == kNotFound || next == output.size()) {
        return;
    }

    const Node& node = nodes_[at];

    if (!node.is_split) {
        output[next] = id;
        ++next;

        return;
    }

    collect_leaves(node.first, output, next);
    collect_leaves(node.second, output, next);
}

DockLayoutResult DockController::place(
    std::uint64_t id,
    const LayoutRect& rect,
    std::span<DockLeafRect> output,
    std::size_t& next,
    bool& overflow
) const noexcept {
    const std::size_t at = index_of(id);

    if (at == kNotFound || next == output.size()) {
        return DockLayoutResult::invalid_input;
    }

    const Node& node = nodes_[at];

    if (!node.is_split) {
        DockLeafRect& slot = output[next];
        ++next;
        slot.leaf = id;
        slot.active_panel = active_panel(id);
        slot.has_strip = node.tab_count > 1;

        if (slot.has_strip) {
            const float strip_height =
                kDockStripHeight < rect.height ? kDockStripHeight : rect.height;
            slot.strip = {rect.x, rect.y, rect.width, strip_height};
            slot.content = {rect.x, rect.y + strip_height, rect.width, rect.height - strip_height};
        } else {
            slot.strip = {rect.x, rect.y, rect.width, 0.0F};
            slot.content = rect;
        }

        return DockLayoutResult::complete;
    }

    const SplitPaneRequest request{
        .total = rect,
        .fraction = node.ratio,
        .gap = gap_,
        .min_first = node.min_first,
        .min_second = node.min_second,
        .direction =
            node.axis == DockSplit::horizontal ? FlexDirection::row : FlexDirection::column,
    };
    SplitPaneLayout panes{};
    const SplitPaneResult result = resolve_split_pane(request, panes);

    if (result == SplitPaneResult::invalid_input) {
        return DockLayoutResult::invalid_input;
    }

    if (result == SplitPaneResult::unfulfillable) {
        overflow = true;
    }

    const DockLayoutResult first_result = place(node.first, panes.first, output, next, overflow);

    if (first_result != DockLayoutResult::complete) {
        return first_result;
    }

    return place(node.second, panes.second, output, next, overflow);
}

float DockController::clamp_unit(float value) noexcept {
    if (value < 0.0F) {
        return 0.0F;
    }

    if (value > 1.0F) {
        return 1.0F;
    }

    return value;
}
}
