#include "veyra/layout/dock.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using veyra::DockController;
using veyra::DockLayoutResult;
using veyra::DockRecord;
using veyra::DockResult;
using veyra::DockSplit;

static_assert(noexcept(DockController{}.node_count()));
static_assert(noexcept(DockController{}.contains(1)));
static_assert(noexcept(DockController{}.active_panel(1)));
static_assert(noexcept(DockController{}.copy_leaves(std::span<std::uint64_t>{})));
static_assert(noexcept(std::declval<DockController&>().tab_add(1, 2)));
static_assert(noexcept(std::declval<DockController&>().tab_close(1, 2)));
static_assert(noexcept(std::declval<DockController&>().set_ratio(1, 0.5F)));
static_assert(noexcept(std::declval<DockController&>().resize(10.0F, 10.0F)));
static_assert(noexcept(std::declval<const DockController&>()
                           .layout(veyra::LayoutRect{}, std::span<veyra::DockLeafRect>{})));
static_assert(
    noexcept(std::declval<DockController&>().import_layout(std::span<const DockRecord>{}))
);

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

bool near(float actual, float expected) {
    return std::abs(actual - expected) < 0.001F;
}

veyra::LayoutRect find_leaf(std::span<const veyra::DockLeafRect> rects, std::uint64_t leaf) {
    for (const auto& entry : rects) {
        if (entry.leaf == leaf) {
            return entry.content;
        }
    }

    throw std::runtime_error("leaf rect is missing");
}

const DockRecord* find_record(std::span<const DockRecord> records, std::uint64_t id) {
    for (const auto& record : records) {
        if (record.id == id) {
            return &record;
        }
    }

    return nullptr;
}

std::size_t read_tabs(DockController& dock, std::uint64_t leaf, std::span<std::uint64_t> output) {
    std::array<DockRecord, DockController::kCapacity> dump{};
    const std::span<const DockRecord> records(dump.data(), dock.export_layout(dump));
    const DockRecord* match = find_record(records, leaf);

    if (match == nullptr) {
        return 0;
    }

    std::size_t copied = match->tab_count;

    if (copied > output.size()) {
        copied = output.size();
    }

    for (std::size_t i = 0; i < copied; ++i) {
        output[i] = match->tabs[i];
    }

    return copied;
}

DockRecord read_record(DockController& dock, std::uint64_t id) {
    std::array<DockRecord, DockController::kCapacity> dump{};
    const std::span<const DockRecord> records(dump.data(), dock.export_layout(dump));
    const DockRecord* match = find_record(records, id);

    if (match == nullptr) {
        return DockRecord{};
    }

    return *match;
}

void check_split_ratios() {
    auto dock = DockController{};
    const std::uint64_t root = dock.root();
    require(root != 0, "a fresh dock owns a root leaf");
    require(dock.node_count() == 1, "a fresh dock holds one root leaf");

    std::uint64_t second = 0;
    require(
        dock.split(root, DockSplit::horizontal, 0.5F, second) == DockResult::changed,
        "splitting the root reports a change"
    );
    require(second != 0 && second != root, "the new leaf gets a distinct handle");
    require(dock.node_count() == 3, "leaf plus split plus new leaf is three nodes");
    require(dock.is_split(root), "the old leaf id now names the split");
    require(!dock.is_split(second), "the new leaf is a plain leaf");
    require(read_record(dock, second).parent == root, "the new leaf points at the split");

    std::array<std::uint64_t, 2> leaves{};
    require(dock.copy_leaves(leaves) == 2, "two leaves read back");
    require(leaves[1] == second, "the new leaf sorts second");
    require(leaves[0] != root && leaves[0] != second, "the old panels move to a fresh first leaf");
    require(read_record(dock, leaves[0]).parent == root, "the first leaf points at the split");

    std::array<veyra::DockLeafRect, 2> rects{};
    const auto bounds = veyra::LayoutRect{.x = 0.0F, .y = 0.0F, .width = 100.0F, .height = 50.0F};
    require(
        dock.layout(bounds, rects) == DockLayoutResult::complete,
        "an even split lays out clean"
    );

    const veyra::LayoutRect first_rect = find_leaf(rects, leaves[0]);
    const veyra::LayoutRect second_rect = find_leaf(rects, second);
    require(
        near(first_rect.width, 50.0F) && near(second_rect.width, 50.0F),
        "an even split halves the extent"
    );
    require(near(first_rect.x + first_rect.width, second_rect.x), "halves meet edge to edge");
}

void check_horizontal_and_vertical_geometry() {
    auto dock = DockController{};
    std::uint64_t second = 0;
    dock.split(dock.root(), DockSplit::horizontal, 0.25F, second);

    std::array<std::uint64_t, 2> leaves{};
    require(dock.copy_leaves(leaves) == 2, "two leaves read back");
    require(
        leaves[1] == second && leaves[0] != dock.root(),
        "leaves report first-child first, new leaf second"
    );
    const std::uint64_t first = leaves[0];

    std::array<veyra::DockLeafRect, 2> rects{};
    const auto bounds = veyra::LayoutRect{.width = 100.0F, .height = 50.0F};
    require(dock.layout(bounds, rects) == DockLayoutResult::complete, "horizontal lays out");

    const veyra::LayoutRect first_rect = find_leaf(rects, first);
    const veyra::LayoutRect other = find_leaf(rects, second);
    require(near(first_rect.width, 25.0F) && near(other.width, 75.0F), "ratio cuts the width");
    require(
        near(first_rect.height, 50.0F) && near(other.height, 50.0F),
        "row split keeps full height"
    );
    require(near(first_rect.x + first_rect.width, other.x), "siblings meet without overlap");

    require(
        dock.set_ratio(dock.root(), 0.5F) == DockResult::changed,
        "moving the ratio reports a change"
    );
    require(
        dock.set_ratio(dock.root(), 0.5F) == DockResult::handled,
        "repeating the ratio reports no change"
    );
    require(near(read_record(dock, dock.root()).ratio, 0.5F), "the ratio reads back");
    require(dock.layout(bounds, rects) == DockLayoutResult::complete, "relayout works");
    require(near(find_leaf(rects, first).width, 50.0F), "the stored ratio drives the next layout");

    auto stacked = DockController{};
    std::uint64_t lower = 0;
    stacked.split(stacked.root(), DockSplit::vertical, 0.5F, lower);

    std::array<std::uint64_t, 2> rows{};
    require(stacked.copy_leaves(rows) == 2, "stacked leaves read back");
    require(stacked.layout(bounds, rects) == DockLayoutResult::complete, "vertical lays out");
    require(
        near(find_leaf(rects, rows[0]).height, 25.0F) &&
            near(find_leaf(rects, lower).height, 25.0F),
        "vertical stacks along the height"
    );
    require(near(find_leaf(rects, rows[0]).width, 100.0F), "column split keeps full width");
}

void check_gap_and_resize() {
    auto dock = DockController{};
    std::uint64_t second = 0;
    dock.split(dock.root(), DockSplit::horizontal, 0.5F, second);

    std::array<std::uint64_t, 2> leaves{};
    require(dock.copy_leaves(leaves) == 2, "split leaves read back");
    const std::uint64_t first = leaves[0];

    require(dock.set_gap(10.0F) == DockResult::changed, "setting the gap reports a change");
    require(dock.set_gap(10.0F) == DockResult::handled, "repeating the gap is a no-op");
    require(dock.set_gap(-1.0F) == DockResult::ignored, "a negative gap is rejected");

    std::array<veyra::DockLeafRect, 2> rects{};
    const auto bounds = veyra::LayoutRect{.width = 100.0F, .height = 40.0F};
    dock.layout(bounds, rects);
    require(
        near(find_leaf(rects, first).width, 45.0F),
        "a rejected gap leaves the stored gap alone"
    );
    dock.layout(bounds, rects);
    require(
        near(find_leaf(rects, first).width, 45.0F) && near(find_leaf(rects, second).width, 45.0F),
        "the gap comes off the shared extent"
    );

    require(dock.resize(100.0F, 40.0F) == DockResult::changed, "resizing stores the bounds");
    require(dock.resize(100.0F, 40.0F) == DockResult::handled, "repeating bounds is a no-op");
    require(dock.resize(-5.0F, 40.0F) == DockResult::ignored, "negative bounds are rejected");
    require(
        dock.resize(100.0F, 40.0F) == DockResult::handled,
        "a rejected resize leaves the stored bounds alone"
    );
}

void check_tab_add_activate_close() {
    auto dock = DockController{};
    const std::uint64_t root = dock.root();

    require(dock.tab_add(root, 11) == DockResult::changed, "first panel lands");
    require(dock.active_panel(root) == 11, "the first panel activates on add");
    require(dock.tab_add(root, 12) == DockResult::changed, "second panel lands");
    require(dock.active_panel(root) == 12, "adding a panel activates it");
    require(
        dock.tab_add(root, 12) == DockResult::handled && dock.tab_count(root) == 2,
        "re-adding a panel is a no-op"
    );
    require(dock.tab_add(root, 0) == DockResult::ignored, "a null panel id is rejected");
    require(dock.tab_add(999, 13) == DockResult::ignored, "an unknown leaf is rejected");

    std::array<std::uint64_t, 4> tabs{};
    require(read_tabs(dock, root, tabs) == 2, "both panels read back");
    require(tabs[0] == 11 && tabs[1] == 12, "tab order follows insertion");

    require(
        dock.tab_activate(root, 11) == DockResult::changed && dock.active_panel(root) == 11,
        "activating switches the panel"
    );
    require(
        dock.tab_activate(root, 11) == DockResult::handled,
        "repeating the active panel is a no-op"
    );
    require(
        dock.tab_activate(root, 99) == DockResult::ignored && dock.active_panel(root) == 11,
        "activating a missing panel is ignored"
    );

    require(dock.tab_add(root, 13) == DockResult::changed, "third panel lands");
    require(dock.tab_activate(root, 11) == DockResult::changed, "back to the first panel");
    require(dock.tab_close(root, 12) == DockResult::changed, "closing a middle tab works");
    require(dock.tab_count(root) == 2, "the middle tab drops out");
    require(dock.active_panel(root) == 11, "closing an inactive tab keeps the selection");
    require(read_tabs(dock, root, tabs) == 2 && tabs[0] == 11 && tabs[1] == 13, "order compacts");

    require(dock.tab_close(root, 11) == DockResult::changed, "closing the active tab works");
    require(dock.active_panel(root) == 13, "the next tab takes over");
    require(
        dock.tab_close(root, 77) == DockResult::ignored && dock.tab_count(root) == 1,
        "closing a missing panel is ignored"
    );
}

void check_last_tab_dissolve() {
    auto dock = DockController{};
    const std::uint64_t root = dock.root();
    std::uint64_t second = 0;
    dock.split(root, DockSplit::horizontal, 0.5F, second);

    std::array<std::uint64_t, 2> leaves{};
    require(dock.copy_leaves(leaves) == 2, "split leaves read back");
    const std::uint64_t first = leaves[0];
    dock.tab_add(first, 11);
    dock.tab_add(second, 21);
    dock.tab_add(second, 22);

    require(
        dock.tab_close(second, 21) == DockResult::changed && dock.copy_leaves(leaves) == 2,
        "closing one of two tabs keeps the leaf"
    );
    require(
        dock.tab_close(second, 22) == DockResult::changed,
        "closing the last tab of a split leaf reports a change"
    );
    require(dock.copy_leaves(leaves) == 1, "the emptied leaf dissolves");
    require(dock.node_count() == 1, "leaf plus split leave the table");
    require(dock.root() == first, "the surviving sibling becomes the root");
    require(!dock.is_split(first), "the survivor is a plain leaf");
    require(dock.tab_count(first) == 1 && dock.active_panel(first) == 11, "its panels survive");

    std::array<veyra::DockLeafRect, 1> rects{};
    const auto bounds = veyra::LayoutRect{.width = 100.0F, .height = 50.0F};
    require(dock.layout(bounds, rects) == DockLayoutResult::complete, "the survivor lays out");
    require(
        near(rects[0].content.width, 100.0F) && rects[0].leaf == first,
        "the survivor fills the whole bounds"
    );
}

void check_root_last_tab_stays_empty() {
    auto dock = DockController{};
    const std::uint64_t root = dock.root();
    dock.tab_add(root, 11);

    require(dock.tab_close(root, 11) == DockResult::changed, "closing the only tab works");
    require(dock.tab_count(root) == 0, "the root leaf is empty");
    require(dock.active_panel(root) == 0, "nothing is active in an empty leaf");
    require(dock.node_count() == 1, "the empty root leaf stays");

    require(dock.tab_add(root, 12) == DockResult::changed, "an empty leaf takes new tabs");
    require(dock.active_panel(root) == 12, "the new tab activates");
}

void check_tab_strip_geometry() {
    auto dock = DockController{};
    const std::uint64_t root = dock.root();
    dock.tab_add(root, 11);

    std::array<veyra::DockLeafRect, 1> rects{};
    const auto bounds = veyra::LayoutRect{.width = 100.0F, .height = 50.0F};
    require(dock.layout(bounds, rects) == DockLayoutResult::complete, "single tab lays out");
    require(!rects[0].has_strip, "one tab shows no strip");
    require(
        near(rects[0].content.height, 50.0F) && near(rects[0].strip.height, 0.0F),
        "content fills the leaf when the strip is hidden"
    );

    dock.tab_add(root, 12);
    require(dock.layout(bounds, rects) == DockLayoutResult::complete, "two tabs lay out");
    require(rects[0].has_strip, "two tabs show a strip");
    require(
        near(rects[0].strip.height, veyra::kDockStripHeight),
        "the strip takes the fixed chrome height"
    );
    require(
        near(rects[0].strip.height + rects[0].content.height, 50.0F),
        "strip plus content fill the leaf without overlap"
    );
    require(rects[0].active_panel == 12, "the active panel rides along");
    require(dock.layout(bounds, rects) == DockLayoutResult::complete, "relayout works");
    require(near(rects[0].content.y, veyra::kDockStripHeight), "content starts below the strip");
}

void check_minimum_stealing_and_deficit_overflow() {
    auto dock = DockController{};
    const std::uint64_t root = dock.root();
    std::uint64_t second = 0;
    dock.split(root, DockSplit::horizontal, 0.1F, second);

    std::array<std::uint64_t, 2> leaves{};
    require(dock.copy_leaves(leaves) == 2, "split leaves read back");
    const std::uint64_t first = leaves[0];

    require(
        dock.set_minimums(root, 40.0F, 0.0F) == DockResult::changed,
        "minimums report a change"
    );
    require(
        dock.set_minimums(root, 40.0F, 0.0F) == DockResult::handled,
        "repeating minimums is a no-op"
    );
    require(
        dock.set_minimums(root, -1.0F, 0.0F) == DockResult::ignored,
        "negative minimums are rejected"
    );

    std::array<veyra::DockLeafRect, 2> rects{};
    const auto bounds = veyra::LayoutRect{.width = 100.0F, .height = 40.0F};
    require(
        dock.layout(bounds, rects) == DockLayoutResult::complete,
        "a satisfiable minimum still completes"
    );
    require(
        near(find_leaf(rects, first).width, 40.0F) && near(find_leaf(rects, second).width, 60.0F),
        "the first minimum steals from the flexible sibling"
    );

    require(
        dock.set_minimums(root, 80.0F, 80.0F) == DockResult::changed,
        "oversized minimums store fine"
    );
    require(
        dock.layout(bounds, rects) == DockLayoutResult::overflow,
        "minimums past the extent report overflow"
    );

    const veyra::LayoutRect first_rect = find_leaf(rects, first);
    const veyra::LayoutRect other = find_leaf(rects, second);
    require(
        near(first_rect.x + first_rect.width, other.x),
        "overflow rects still meet without silent overlap"
    );
    require(near(first_rect.width + other.width, 100.0F), "overflow rects still tile the extent");
    require(
        near(first_rect.width, 50.0F) && near(other.width, 50.0F),
        "unfulfillable minimums degrade proportionally"
    );
}

void check_persistence_round_trip() {
    auto dock = DockController{};
    const std::uint64_t root = dock.root();
    std::uint64_t second = 0;
    dock.split(root, DockSplit::horizontal, 0.25F, second);

    std::array<std::uint64_t, 2> halves{};
    require(dock.copy_leaves(halves) == 2, "split halves read back");
    const std::uint64_t first = halves[0];

    dock.set_minimums(root, 30.0F, 50.0F);
    dock.set_gap(8.0F);
    dock.tab_add(first, 11);
    dock.tab_add(first, 12);
    dock.tab_activate(first, 11);
    dock.tab_add(second, 21);

    std::uint64_t nested = 0;
    dock.split(second, DockSplit::vertical, 0.75F, nested);

    std::array<std::uint64_t, 3> thirds{};
    require(dock.copy_leaves(thirds) == 3, "nested leaves read back");
    dock.tab_add(nested, 31);

    std::array<DockRecord, DockController::kCapacity> snapshot{};
    const std::size_t stored = dock.export_layout(snapshot);
    require(stored == 5, "two splits plus three leaves is five records");

    auto restored = DockController{};
    require(
        restored.import_layout(std::span<const DockRecord>(snapshot.data(), stored)) ==
            DockResult::changed,
        "importing a snapshot reports a change"
    );
    require(restored.node_count() == 5, "every node comes back");
    require(near(read_record(restored, root).ratio, 0.25F), "ratios survive the trip");
    require(restored.active_panel(first) == 11, "active tabs survive the trip");
    require(restored.tab_count(nested) == 1, "nested tabs survive the trip");

    std::array<DockRecord, DockController::kCapacity> again{};
    const std::size_t restwo = restored.export_layout(again);
    require(restwo == stored, "the restored tree exports the same count");

    for (std::size_t i = 0; i < stored; ++i) {
        const DockRecord* match = find_record({again.data(), restwo}, snapshot[i].id);
        require(match != nullptr, "every exported id comes back");

        require(match->parent == snapshot[i].parent, "parents match");
        require(match->is_split == snapshot[i].is_split, "kinds match");
        require(match->first == snapshot[i].first, "first links match");
        require(match->second == snapshot[i].second, "second links match");
        require(near(match->ratio, snapshot[i].ratio), "ratios match");
        require(
            near(match->min_first, snapshot[i].min_first) &&
                near(match->min_second, snapshot[i].min_second),
            "minimums match"
        );
        require(match->tab_count == snapshot[i].tab_count, "tab counts match");
        require(match->active == snapshot[i].active, "active panels match");

        for (std::uint32_t t = 0; t < snapshot[i].tab_count; ++t) {
            require(match->tabs[t] == snapshot[i].tabs[t], "tab order matches");
        }
    }

    const auto bounds = veyra::LayoutRect{.width = 200.0F, .height = 100.0F};
    std::array<veyra::DockLeafRect, 3> before{};
    std::array<veyra::DockLeafRect, 3> after{};
    dock.set_gap(0.0F);
    restored.set_gap(0.0F);
    require(dock.layout(bounds, before) == DockLayoutResult::complete, "source lays out");
    require(restored.layout(bounds, after) == DockLayoutResult::complete, "restore lays out");

    for (std::size_t i = 0; i < 3; ++i) {
        require(before[i].leaf == after[i].leaf, "leaf order matches");
        require(
            near(before[i].content.x, after[i].content.x) &&
                near(before[i].content.y, after[i].content.y) &&
                near(before[i].content.width, after[i].content.width) &&
                near(before[i].content.height, after[i].content.height),
            "restored geometry matches"
        );
    }
}

void check_import_ignores_unknown() {
    auto dock = DockController{};
    const std::uint64_t root = dock.root();
    std::uint64_t second = 0;
    dock.split(root, DockSplit::horizontal, 0.5F, second);

    std::array<std::uint64_t, 2> halves{};
    require(dock.copy_leaves(halves) == 2, "split halves read back");
    dock.tab_add(halves[0], 11);

    std::array<DockRecord, DockController::kCapacity> snapshot{};
    const std::size_t stored = dock.export_layout(snapshot);

    std::array<DockRecord, DockController::kCapacity + 1> foreign{};
    for (std::size_t i = 0; i < stored; ++i) {
        foreign[i] = snapshot[i];
    }

    for (std::size_t i = 0; i < stored; ++i) {
        if (foreign[i].is_split) {
            foreign[i].second = 9999;
            break;
        }
    }

    auto patched = DockController{};
    require(
        patched.import_layout(std::span<const DockRecord>(foreign.data(), stored)) ==
            DockResult::changed,
        "a snapshot with a dangling child still imports"
    );
    require(!patched.is_split(root), "a split missing a child degrades to a leaf");
    require(patched.tab_count(root) == 0, "a degraded split carries no tabs of its own");
    require(patched.tab_count(halves[0]) == 1, "the surviving child keeps its tab");

    std::array<DockRecord, 2> nulls{};
    require(
        patched.import_layout(nulls) == DockResult::ignored,
        "a snapshot with no usable records is ignored"
    );
    require(!patched.is_split(root), "ignored imports leave the tree alone");

    std::array<DockRecord, 0> empty{};
    require(patched.import_layout(empty) == DockResult::ignored, "an empty snapshot is ignored");
}

void check_invalid_geometry_and_ids() {
    auto dock = DockController{};
    const float quiet_nan = std::numeric_limits<float>::quiet_NaN();

    std::uint64_t fresh = 0;
    require(
        dock.split(999, DockSplit::horizontal, 0.5F, fresh) == DockResult::ignored,
        "splitting an unknown leaf is ignored"
    );
    require(
        dock.split(dock.root(), DockSplit::horizontal, quiet_nan, fresh) == DockResult::ignored,
        "a NaN ratio rejects the split"
    );

    std::uint64_t second = 0;
    require(
        dock.split(dock.root(), DockSplit::horizontal, 1.5F, second) == DockResult::changed,
        "an out-of-range ratio clamps instead of failing"
    );
    require(near(read_record(dock, dock.root()).ratio, 1.0F), "the ratio clamps to the edge");
    require(
        dock.split(dock.root(), DockSplit::horizontal, 0.5F, fresh) == DockResult::ignored,
        "splitting a split node is ignored"
    );
    require(dock.tab_add(dock.root(), 11) == DockResult::ignored, "a split node takes no tabs");
    require(dock.set_ratio(second, 0.5F) == DockResult::ignored, "a leaf takes no ratio");
    require(
        dock.set_ratio(999, 0.5F) == DockResult::ignored &&
            dock.set_ratio(dock.root(), quiet_nan) == DockResult::ignored,
        "unknown nodes and NaN ratios are ignored"
    );

    std::array<veyra::DockLeafRect, 2> rects{
        veyra::DockLeafRect{.leaf = 7},
        veyra::DockLeafRect{.leaf = 8},
    };
    const auto bad = veyra::LayoutRect{.width = quiet_nan, .height = 40.0F};
    require(dock.layout(bad, rects) == DockLayoutResult::invalid_input, "NaN bounds are rejected");
    require(rects[0].leaf == 7 && rects[1].leaf == 8, "rejected layout leaves output alone");

    const auto negative = veyra::LayoutRect{.width = -10.0F, .height = 40.0F};
    require(
        dock.layout(negative, rects) == DockLayoutResult::invalid_input,
        "negative bounds are rejected"
    );

    const auto bounds = veyra::LayoutRect{.width = 100.0F, .height = 40.0F};
    std::array<veyra::DockLeafRect, 1> short_out{};
    require(
        dock.layout(bounds, short_out) == DockLayoutResult::insufficient_output,
        "a short buffer is reported"
    );
}

void check_tab_capacity_overflow() {
    auto dock = DockController{};
    const std::uint64_t root = dock.root();

    for (std::uint64_t panel = 1; panel <= DockController::kMaxTabs; ++panel) {
        require(dock.tab_add(root, panel) == DockResult::changed, "tabs fill to capacity");
    }

    require(
        dock.tab_add(root, 999) == DockResult::overflow && dock.tab_count(root) == 16,
        "one tab past capacity overflows without touching the leaf"
    );
}

void check_node_capacity_overflow() {
    auto dock = DockController{};
    std::uint64_t leaf = dock.root();
    bool overflowed = false;

    for (int i = 0; i < 100; ++i) {
        std::uint64_t next = 0;
        const DockResult result = dock.split(leaf, DockSplit::horizontal, 0.5F, next);

        if (result == DockResult::overflow) {
            overflowed = true;
            break;
        }

        require(result == DockResult::changed, "splits land until the table fills");
        leaf = next;
    }

    require(overflowed, "the fixed node table eventually overflows");
    require(dock.node_count() <= DockController::kCapacity, "overflow never grows past capacity");
    require(dock.contains(leaf), "overflow leaves the tree alone");
}

}

int main() {
    try {
        check_split_ratios();
        check_horizontal_and_vertical_geometry();
        check_gap_and_resize();
        check_tab_add_activate_close();
        check_last_tab_dissolve();
        check_root_last_tab_stays_empty();
        check_tab_strip_geometry();
        check_minimum_stealing_and_deficit_overflow();
        check_persistence_round_trip();
        check_import_ignores_unknown();
        check_invalid_geometry_and_ids();
        check_tab_capacity_overflow();
        check_node_capacity_overflow();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
