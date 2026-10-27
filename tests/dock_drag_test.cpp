#include "veyra/layout/panels/dock.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using veyra::DockController;
using veyra::DockLayoutResult;
using veyra::DockLeafRect;
using veyra::DockRecord;
using veyra::DockResult;
using veyra::DockSplit;
using veyra::LayoutRect;

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

bool near(float actual, float expected) {
    return std::abs(actual - expected) < 0.001F;
}

bool near_rect(LayoutRect actual, LayoutRect expected) {
    return near(actual.x, expected.x) && near(actual.y, expected.y) &&
        near(actual.width, expected.width) && near(actual.height, expected.height);
}

DockRecord read_record(DockController& dock, std::uint64_t id) {
    std::array<DockRecord, DockController::kCapacity> dump{};

    const std::span<const DockRecord> records(dump.data(), dock.export_layout(dump));

    for (const auto& record : records) {
        if (record.id == id) {
            return record;
        }
    }

    return DockRecord{};
}

std::size_t read_tabs(DockController& dock, std::uint64_t leaf, std::span<std::uint64_t> output) {
    const DockRecord record = read_record(dock, leaf);
    std::size_t copied = record.tab_count;

    if (copied > output.size()) {
        copied = output.size();
    }

    for (std::size_t i = 0; i < copied; ++i) {
        output[i] = record.tabs[i];
    }

    return copied;
}

std::size_t snapshot(DockController& dock, std::span<DockRecord> output) {
    return dock.export_layout(output);
}

void require_snapshots_equal(
    std::span<const DockRecord> before,
    std::span<const DockRecord> after
) {
    require(before.size() == after.size(), "cancel leaves the record count alone");

    for (const auto& wanted : before) {
        const DockRecord* match = nullptr;

        for (const auto& candidate : after) {
            if (candidate.id == wanted.id) {
                match = &candidate;
                break;
            }
        }

        require(match != nullptr, "cancel keeps every node id");
        require(match->parent == wanted.parent, "cancel keeps every parent link");
        require(match->is_split == wanted.is_split, "cancel keeps every node kind");
        require(match->axis == wanted.axis, "cancel keeps every split axis");
        require(near(match->ratio, wanted.ratio), "cancel keeps every ratio");
        require(
            near(match->min_first, wanted.min_first) && near(match->min_second, wanted.min_second),
            "cancel keeps every minimum"
        );
        require(match->first == wanted.first, "cancel keeps every first link");
        require(match->second == wanted.second, "cancel keeps every second link");
        require(match->tab_count == wanted.tab_count, "cancel keeps every tab count");
        require(match->active == wanted.active, "cancel keeps every active panel");

        for (std::uint32_t t = 0; t < wanted.tab_count; ++t) {
            require(match->tabs[t] == wanted.tabs[t], "cancel keeps every tab order");
        }
    }
}

DockLeafRect find_entry(std::span<const DockLeafRect> rects, std::uint64_t leaf) {
    for (const auto& entry : rects) {
        if (entry.leaf == leaf) {
            return entry;
        }
    }

    throw std::runtime_error("leaf rect is missing");
}

void require_rects_equal(
    std::span<const DockLeafRect> before,
    std::span<const DockLeafRect> after
) {
    require(before.size() == after.size(), "cancel keeps the leaf count");

    for (std::size_t i = 0; i < before.size(); ++i) {
        require(before[i].leaf == after[i].leaf, "cancel keeps the leaf order");
        require(before[i].has_strip == after[i].has_strip, "cancel keeps every strip flag");
        require(before[i].active_panel == after[i].active_panel, "cancel keeps every active panel");
        require(near_rect(before[i].strip, after[i].strip), "cancel keeps every strip rect");
        require(near_rect(before[i].content, after[i].content), "cancel keeps every content rect");
    }
}

void check_drag_tab_between_leaves() {
    auto dock = DockController{};
    const std::uint64_t root = dock.root();
    std::uint64_t second = 0;
    require(
        dock.split(root, DockSplit::horizontal, 0.5F, second) == DockResult::changed,
        "setup split lands"
    );

    std::array<std::uint64_t, 2> leaves{};
    require(dock.copy_leaves(leaves) == 2, "setup leaves read back");
    const std::uint64_t first = leaves[0];

    dock.tab_add(first, 11);
    dock.tab_add(first, 12);
    dock.tab_add(first, 13);
    require(dock.tab_activate(first, 12) == DockResult::changed, "setup activation lands");
    dock.tab_add(second, 21);

    const auto bounds = LayoutRect{.width = 200.0F, .height = 100.0F};
    std::array<DockLeafRect, 2> before{};
    require(dock.layout(bounds, before) == DockLayoutResult::complete, "setup lays out");
    require(find_entry(before, first).has_strip, "three tabs show a strip before the drag");
    require(!find_entry(before, second).has_strip, "one tab shows no strip before the drag");

    require(dock.tab_close(first, 12) == DockResult::changed, "drag lifts the active tab");
    require(dock.tab_add(second, 12) == DockResult::changed, "drag drops onto the target leaf");

    require(dock.node_count() == 3, "a between-leaves drag adds no nodes");
    require(dock.tab_count(first) == 2, "the source keeps its remaining tabs");
    require(dock.active_panel(first) == 13, "closing the active tab falls back to the next tab");

    std::array<std::uint64_t, 4> tabs{};
    require(read_tabs(dock, first, tabs) == 2, "source tabs read back");
    require(tabs[0] == 11 && tabs[1] == 13, "source order compacts around the dragged tab");
    require(read_tabs(dock, second, tabs) == 2, "target tabs read back");
    require(tabs[0] == 21 && tabs[1] == 12, "a dropped tab appends in insertion order");
    require(dock.active_panel(second) == 12, "adding a tab selects it");

    std::array<DockLeafRect, 2> after{};
    require(dock.layout(bounds, after) == DockLayoutResult::complete, "the drag lays out");
    require(find_entry(after, first).leaf == first, "the source leaf keeps its handle");
    require(find_entry(after, second).leaf == second, "the target leaf keeps its handle");
    require(find_entry(after, first).has_strip, "two tabs show a strip after the drag");
    require(find_entry(after, second).has_strip, "two tabs show a strip after the drag");
    require(
        near(find_entry(after, first).content.width, 100.0F) &&
            near(find_entry(after, second).content.width, 100.0F),
        "a between-leaves drag leaves the split geometry alone"
    );
    require(
        near(
            find_entry(after, first).content.x + find_entry(after, first).content.width,
            find_entry(after, second).content.x
        ),
        "halves still meet edge to edge after the drag"
    );
}

void check_drag_creates_split() {
    auto dock = DockController{};
    const std::uint64_t root = dock.root();
    std::uint64_t second = 0;
    require(
        dock.split(root, DockSplit::horizontal, 0.5F, second) == DockResult::changed,
        "setup split lands"
    );

    std::array<std::uint64_t, 2> halves{};
    require(dock.copy_leaves(halves) == 2, "setup halves read back");
    const std::uint64_t first = halves[0];
    dock.tab_add(first, 11);
    dock.tab_add(first, 12);
    dock.tab_add(second, 21);

    std::uint64_t nested = 0;
    require(
        dock.split(second, DockSplit::vertical, 0.5F, nested) == DockResult::changed,
        "dropping on the edge splits the target leaf"
    );
    require(dock.tab_close(first, 11) == DockResult::changed, "drag lifts the tab");
    require(dock.tab_add(nested, 11) == DockResult::changed, "drag drops into the new leaf");

    require(dock.node_count() == 5, "edge drop is two splits plus three leaves");
    require(dock.is_split(root) && dock.is_split(second), "both splits read back");

    std::array<std::uint64_t, 3> leaves{};
    require(dock.copy_leaves(leaves) == 3, "three leaves read back");
    require(leaves[0] == first, "the source leaf sorts first");
    require(leaves[2] == nested, "the new leaf sorts last");
    require(
        read_record(dock, second).first == leaves[1],
        "old panels move to the fresh first child"
    );
    require(read_record(dock, nested).parent == second, "the new leaf points at the target split");
    require(near(read_record(dock, root).ratio, 0.5F), "the outer ratio pins");
    require(near(read_record(dock, second).ratio, 0.5F), "the edge-drop ratio pins");

    std::array<std::uint64_t, 4> tabs{};
    require(read_tabs(dock, first, tabs) == 1 && tabs[0] == 12, "the source keeps its tab");
    require(dock.active_panel(first) == 12, "closing an inactive tab keeps the selection");
    require(read_tabs(dock, leaves[1], tabs) == 1 && tabs[0] == 21, "old target tabs stay put");
    require(read_tabs(dock, nested, tabs) == 1 && tabs[0] == 11, "the new leaf holds the drag");
    require(dock.active_panel(nested) == 11, "the dropped tab activates");

    const auto bounds = LayoutRect{.width = 200.0F, .height = 100.0F};
    std::array<DockLeafRect, 3> rects{};
    require(dock.layout(bounds, rects) == DockLayoutResult::complete, "the new tree lays out");
    require(
        near(find_entry(rects, first).content.width, 100.0F),
        "the source keeps its outer half"
    );
    require(
        near(find_entry(rects, nested).content.width, 100.0F) &&
            near(find_entry(rects, nested).content.height, 50.0F),
        "the new leaf takes half of the split target"
    );
    require(
        near(find_entry(rects, leaves[1]).content.height, 50.0F),
        "the old target half stacks above the drop"
    );
}

void check_tear_off_between_controllers() {
    auto window_a = DockController{};
    const std::uint64_t root_a = window_a.root();
    std::uint64_t second_a = 0;
    require(
        window_a.split(root_a, DockSplit::horizontal, 0.5F, second_a) == DockResult::changed,
        "setup split lands"
    );

    std::array<std::uint64_t, 2> leaves{};
    require(window_a.copy_leaves(leaves) == 2, "setup leaves read back");
    const std::uint64_t first_a = leaves[0];
    window_a.tab_add(first_a, 11);
    window_a.tab_add(first_a, 12);
    window_a.tab_add(second_a, 21);

    auto window_b = DockController{};
    const std::uint64_t root_b = window_b.root();

    require(window_a.tab_close(first_a, 11) == DockResult::changed, "tear-off lifts the tab");
    require(window_b.tab_add(root_b, 11) == DockResult::changed, "tear-off lands in the window");

    require(window_a.copy_leaves(leaves) == 2, "a shared leaf survives the tear-off");
    require(window_a.tab_count(first_a) == 1, "the source leaf keeps its tab");
    require(window_b.tab_count(root_b) == 1, "the new window gains its tab");
    require(window_b.active_panel(root_b) == 11, "the torn-off tab activates");

    require(window_a.tab_close(second_a, 21) == DockResult::changed, "last tab lifts");
    require(window_b.tab_add(root_b, 21) == DockResult::changed, "last tab lands in the window");

    require(window_a.copy_leaves(leaves) == 1, "the emptied leaf dissolves");
    require(window_a.node_count() == 1, "leaf plus split leave the table");
    require(window_a.root() == first_a, "the surviving sibling becomes the root");
    require(window_a.tab_count(first_a) == 1, "surviving panels ride out the dissolve");

    std::array<std::uint64_t, 4> tabs{};
    require(read_tabs(window_b, root_b, tabs) == 2, "window tabs read back");
    require(tabs[0] == 11 && tabs[1] == 21, "torn-off tabs append in arrival order");
    require(window_b.active_panel(root_b) == 21, "each arrival selects itself");

    const auto bounds = LayoutRect{.width = 200.0F, .height = 100.0F};
    std::array<DockLeafRect, 1> rect_a{};
    std::array<DockLeafRect, 1> rect_b{};
    require(window_a.layout(bounds, rect_a) == DockLayoutResult::complete, "the source lays out");
    require(window_b.layout(bounds, rect_b) == DockLayoutResult::complete, "the window lays out");
    require(
        near(rect_a[0].content.width, 200.0F) && near(rect_b[0].content.width, 200.0F),
        "both single leaves fill their own window"
    );
}

void check_tear_off_last_panel_of_root() {
    auto window_a = DockController{};
    const std::uint64_t root_a = window_a.root();
    window_a.tab_add(root_a, 11);

    auto window_b = DockController{};
    const std::uint64_t root_b = window_b.root();

    require(window_a.tab_close(root_a, 11) == DockResult::changed, "last root tab lifts");
    require(window_b.tab_add(root_b, 11) == DockResult::changed, "last root tab lands");

    require(window_a.node_count() == 1, "the empty root leaf stays in the table");
    require(window_a.root() == root_a, "the root handle never moves");
    require(!window_a.is_split(root_a), "the root stays a plain leaf");
    require(window_a.tab_count(root_a) == 0, "the source leaf is empty");
    require(window_a.active_panel(root_a) == 0, "nothing is active in an empty leaf");
    require(window_b.tab_count(root_b) == 1, "the new window gains the tab");
    require(window_b.active_panel(root_b) == 11, "the torn-off tab activates");

    const auto bounds = LayoutRect{.width = 200.0F, .height = 100.0F};
    std::array<DockLeafRect, 1> rect_a{};
    require(
        window_a.layout(bounds, rect_a) == DockLayoutResult::complete,
        "the empty root lays out"
    );
    require(!rect_a[0].has_strip, "an empty leaf shows no strip");
    require(
        near(rect_a[0].content.width, 200.0F) && near(rect_a[0].content.height, 100.0F),
        "the empty root still fills its window"
    );

    require(window_a.tab_add(root_a, 12) == DockResult::changed, "an emptied root takes new tabs");
    require(window_a.active_panel(root_a) == 12, "the next tab activates");
}

void check_cancel_mid_drag() {
    auto window_a = DockController{};
    const std::uint64_t root_a = window_a.root();
    std::uint64_t second_a = 0;
    window_a.split(root_a, DockSplit::horizontal, 0.5F, second_a);

    std::array<std::uint64_t, 2> leaves{};
    window_a.copy_leaves(leaves);
    window_a.tab_add(leaves[0], 11);
    window_a.tab_add(leaves[0], 12);
    window_a.tab_add(second_a, 21);

    auto window_b = DockController{};
    window_b.tab_add(window_b.root(), 31);

    std::array<DockRecord, DockController::kCapacity> snap_a_before{};
    std::array<DockRecord, DockController::kCapacity> snap_b_before{};
    const std::span<const DockRecord> before_a(
        snap_a_before.data(),
        snapshot(window_a, snap_a_before)
    );
    const std::span<const DockRecord> before_b(
        snap_b_before.data(),
        snapshot(window_b, snap_b_before)
    );

    const auto bounds = LayoutRect{.width = 200.0F, .height = 100.0F};
    std::array<DockLeafRect, 2> rect_a_before{};
    std::array<DockLeafRect, 1> rect_b_before{};
    require(
        window_a.layout(bounds, rect_a_before) == DockLayoutResult::complete,
        "source lays out"
    );
    require(
        window_b.layout(bounds, rect_b_before) == DockLayoutResult::complete,
        "window lays out"
    );

    std::array<DockRecord, DockController::kCapacity> snap_a_after{};
    std::array<DockRecord, DockController::kCapacity> snap_b_after{};
    const std::span<const DockRecord> after_a(
        snap_a_after.data(),
        snapshot(window_a, snap_a_after)
    );
    const std::span<const DockRecord> after_b(
        snap_b_after.data(),
        snapshot(window_b, snap_b_after)
    );

    std::array<DockLeafRect, 2> rect_a_after{};
    std::array<DockLeafRect, 1> rect_b_after{};
    require(
        window_a.layout(bounds, rect_a_after) == DockLayoutResult::complete,
        "source relayouts"
    );
    require(
        window_b.layout(bounds, rect_b_after) == DockLayoutResult::complete,
        "window relayouts"
    );

    require_snapshots_equal(before_a, after_a);
    require_snapshots_equal(before_b, after_b);
    require_rects_equal(rect_a_before, rect_a_after);
    require_rects_equal(rect_b_before, rect_b_after);
}

}

int main() {
    try {
        check_drag_tab_between_leaves();
        check_drag_creates_split();
        check_tear_off_between_controllers();
        check_tear_off_last_panel_of_root();
        check_cancel_mid_drag();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
