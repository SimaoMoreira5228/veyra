#include "veyra/behaviors/datagrid.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using veyra::ColumnCustomization;
using veyra::ColumnEdge;
using veyra::DataGridDensity;
using veyra::DataGridMove;
using veyra::DataGridPin;
using veyra::DataGridResult;
using veyra::DataGridValueKind;
using veyra::SortDirection;

static_assert(noexcept(veyra::DataGridController{}.column_count()));
static_assert(
    noexcept(veyra::DataGridController{}.set_columns(std::span<const veyra::DataGridColumn>{}))
);
static_assert(noexcept(veyra::DataGridController{}
                           .move_cursor(DataGridMove::up, std::span<const std::uint64_t>{}, 0)));
static_assert(noexcept(veyra::DataGridController{}.sort_specs(std::span<veyra::SortSpec>{})));
static_assert(noexcept(veyra::DataGridController{}
                           .apply_customization(std::span<const veyra::ColumnCustomization>{})));

constexpr std::array<veyra::DataGridColumn, 3> kSpec{{
    {.id = 1, .width = 100.0F, .min_width = 40.0F, .max_width = 200.0F, .sortable = true},
    {.id = 2, .width = 80.0F, .min_width = 40.0F, .max_width = 200.0F, .sortable = true},
    {.id = 3, .width = 120.0F, .min_width = 40.0F, .max_width = 200.0F},
}};

constexpr std::array<std::uint64_t, 4> kRows{10, 20, 30, 40};

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

veyra::DataGridController make_grid() {
    auto grid = veyra::DataGridController{};
    const DataGridResult result = grid.set_columns(kSpec);

    require(result == DataGridResult::changed, "the fixture spec is accepted");

    return grid;
}

void check_replace_and_order() {
    auto grid = veyra::DataGridController{};
    require(grid.column_count() == 0, "a new grid holds no columns");

    require(
        grid.set_columns(kSpec) == DataGridResult::changed,
        "replacing the spec reports a change"
    );
    require(grid.column_count() == 3, "the replaced spec holds three columns");
    require(
        grid.set_columns(kSpec) == DataGridResult::handled,
        "replacing with an identical spec reports no change"
    );

    std::array<veyra::DataGridColumn, 3> wider{kSpec[0], kSpec[1], kSpec[2]};
    wider[0].width = 150.0F;
    require(
        grid.set_columns(wider) == DataGridResult::changed && grid.column_width(1) == 150.0F,
        "replacing with a changed width reports a change"
    );
    require(
        grid.set_columns(kSpec) == DataGridResult::changed,
        "restoring the fixture spec reports a change"
    );

    std::array<veyra::DataGridColumn, 3> back{};
    require(grid.copy_columns(back) == 3, "copy_columns fills the caller buffer");
    require(
        back[0].id == 1 && back[1].id == 2 && back[2].id == 3,
        "copy_columns keeps display order"
    );
    require(
        back[0].width == 100.0F && back[0].sortable && !back[0].hidden &&
            back[0].pinned == DataGridPin::none && !back[0].locked_position &&
            back[0].value_kind == DataGridValueKind::text,
        "column facets survive the round trip with fresh defaults"
    );

    std::array<veyra::DataGridColumn, 1> small{};
    require(
        grid.copy_columns(small) == 1 && small[0].id == 1,
        "a short buffer truncates to its own size"
    );

    std::array<veyra::DataGridColumn, 0> none{};
    require(
        grid.set_columns(none) == DataGridResult::changed && grid.column_count() == 0,
        "an empty spec clears the grid"
    );
    require(
        grid.set_columns(none) == DataGridResult::handled,
        "clearing an empty grid reports no change"
    );
}

void check_replace_rejects_bad_specs() {
    auto grid = make_grid();

    std::array<std::uint64_t, veyra::DataGridController::kCapacity + 1> too_many{};
    for (std::size_t i = 0; i < too_many.size(); ++i) {
        too_many[i] = static_cast<std::uint64_t>(i + 1);
    }
    std::array<veyra::DataGridColumn, veyra::DataGridController::kCapacity + 1> wide{};
    for (std::size_t i = 0; i < wide.size(); ++i) {
        wide[i] = {.id = too_many[i], .width = 50.0F, .min_width = 10.0F, .max_width = 100.0F};
    }
    require(
        grid.set_columns(wide) == DataGridResult::overflow && grid.column_count() == 3,
        "an over-capacity spec is rejected without touching the grid"
    );

    std::array<veyra::DataGridColumn, 2> duplicates{{
        {.id = 1, .width = 50.0F, .min_width = 10.0F, .max_width = 100.0F},
        {.id = 1, .width = 60.0F, .min_width = 10.0F, .max_width = 100.0F},
    }};
    require(
        grid.set_columns(duplicates) == DataGridResult::ignored && grid.column_count() == 3,
        "a duplicated id rejects the whole replace"
    );

    std::array<veyra::DataGridColumn, 1> null_id{{
        {.id = 0, .width = 50.0F, .min_width = 10.0F, .max_width = 100.0F},
    }};
    require(
        grid.set_columns(null_id) == DataGridResult::ignored && !grid.contains_column(0),
        "a null id rejects the whole replace"
    );

    std::array<veyra::DataGridColumn, 1> inverted{{
        {.id = 9, .width = 50.0F, .min_width = 100.0F, .max_width = 10.0F},
    }};
    require(
        grid.set_columns(inverted) == DataGridResult::ignored && !grid.contains_column(9),
        "an inverted clamp rejects the whole replace"
    );

    std::array<veyra::DataGridColumn, 1> negative{{
        {.id = 9, .width = 50.0F, .min_width = -10.0F, .max_width = 100.0F},
    }};
    require(
        grid.set_columns(negative) == DataGridResult::ignored,
        "a negative minimum rejects the whole replace"
    );

    std::array<veyra::DataGridColumn, 1> indefinite{{
        {.id = 9,
         .width = std::numeric_limits<float>::quiet_NaN(),
         .min_width = 10.0F,
         .max_width = 100.0F},
    }};
    require(
        grid.set_columns(indefinite) == DataGridResult::ignored && grid.column_count() == 3,
        "a NaN width rejects the whole replace"
    );
}

void check_resize_clamps_exactly_once() {
    auto grid = make_grid();

    require(
        grid.resize_column(1, 500.0F) == DataGridResult::changed && grid.column_width(1) == 200.0F,
        "a wide resize clamps to the maximum"
    );
    require(
        grid.resize_column(1, 1.0F) == DataGridResult::changed && grid.column_width(1) == 40.0F,
        "a narrow resize clamps to the minimum"
    );
    require(
        grid.resize_column(1, 40.0F) == DataGridResult::handled,
        "resizing to the stored width reports no change"
    );
    require(
        grid.resize_column(1, std::numeric_limits<float>::quiet_NaN()) == DataGridResult::ignored &&
            grid.column_width(1) == 40.0F,
        "a NaN width is rejected without touching the column"
    );
    require(
        grid.resize_column(99, 80.0F) == DataGridResult::ignored && grid.column_width(99) == 0.0F,
        "resizing an unknown column is ignored"
    );

    require(grid.set_hidden(2, true) == DataGridResult::changed, "hiding column two works");
    require(
        grid.resize_column(2, 150.0F) == DataGridResult::changed && grid.column_width(2) == 150.0F,
        "a hidden column keeps its width so show restores it"
    );
}

void check_reorder() {
    auto grid = make_grid();

    require(
        grid.move_column(3, 1, ColumnEdge::before) == DataGridResult::changed,
        "moving a column before the first reports a change"
    );

    std::array<std::uint64_t, 3> order{};
    require(grid.copy_visible_columns(order) == 3, "three columns stay visible");
    require(
        order[0] == 3 && order[1] == 1 && order[2] == 2,
        "the moved column lands before its target"
    );

    require(
        grid.move_column(3, 2, ColumnEdge::after) == DataGridResult::changed,
        "moving a column after the last reports a change"
    );
    require(grid.copy_visible_columns(order) == 3, "the order still reads back");
    require(
        order[0] == 1 && order[1] == 2 && order[2] == 3,
        "the moved column lands after its target"
    );

    require(
        grid.move_column(1, 1, ColumnEdge::before) == DataGridResult::handled,
        "moving a column onto itself reports no change"
    );
    require(
        grid.move_column(1, 2, ColumnEdge::before) == DataGridResult::handled,
        "a reorder that keeps the order reports no change"
    );
    require(
        grid.move_column(1, 99, ColumnEdge::before) == DataGridResult::ignored &&
            grid.move_column(99, 1, ColumnEdge::before) == DataGridResult::ignored,
        "unknown ids reject the reorder"
    );
}

void check_locked_columns_reject_reorder_as_source_and_target() {
    std::array<veyra::DataGridColumn, 3> spec{kSpec[0], kSpec[1], kSpec[2]};
    spec[0].locked_position = true;

    auto grid = veyra::DataGridController{};
    require(grid.set_columns(spec) == DataGridResult::changed, "the locked fixture is accepted");

    require(
        grid.move_column(1, 2, ColumnEdge::after) == DataGridResult::ignored,
        "a locked source column rejects the move"
    );
    require(
        grid.move_column(3, 1, ColumnEdge::before) == DataGridResult::ignored,
        "a locked target column rejects the move"
    );
    require(
        grid.move_column(2, 3, ColumnEdge::after) == DataGridResult::changed,
        "unlocked columns still reorder around the lock"
    );

    std::array<std::uint64_t, 3> order{};
    require(grid.copy_visible_columns(order) == 3, "the order reads back");
    require(
        order[0] == 1 && order[1] == 3 && order[2] == 2,
        "the lock held column one in place while the rest moved"
    );
}

void check_pinning_groups_visible_order() {
    auto grid = make_grid();

    require(grid.column_pin(1) == DataGridPin::none, "columns start unpinned");
    require(grid.column_pin(99) == DataGridPin::none, "an unknown column reports no pin");
    require(
        grid.set_pinned(99, DataGridPin::left) == DataGridResult::ignored,
        "pinning an unknown column is ignored"
    );
    require(
        grid.set_pinned(1, DataGridPin::none) == DataGridResult::handled,
        "repeating the stored pin reports no change"
    );

    require(
        grid.set_pinned(3, DataGridPin::left) == DataGridResult::changed &&
            grid.set_pinned(1, DataGridPin::right) == DataGridResult::changed,
        "pinning the outer columns works"
    );
    require(
        grid.column_pin(3) == DataGridPin::left && grid.column_pin(1) == DataGridPin::right,
        "pins read back"
    );

    std::array<std::uint64_t, 3> order{};
    require(grid.copy_visible_columns(order) == 3, "three columns stay visible");
    require(
        order[0] == 3 && order[1] == 2 && order[2] == 1,
        "pinned-left leads, unpinned sits middle, pinned-right trails"
    );

    std::array<veyra::DataGridColumn, 3> stored{};
    require(grid.copy_columns(stored) == 3, "the stored spec reads back");
    require(
        stored[0].id == 1 && stored[1].id == 2 && stored[2].id == 3,
        "pinning never reorders storage; only the visible projection groups"
    );
}

void check_pinning_constrains_reorder() {
    auto grid = make_grid();
    grid.set_pinned(1, DataGridPin::left);
    grid.set_pinned(2, DataGridPin::left);

    require(
        grid.move_column(2, 1, ColumnEdge::before) == DataGridResult::changed,
        "reordering inside one pinned section works"
    );

    std::array<std::uint64_t, 3> order{};
    grid.copy_visible_columns(order);
    require(
        order[0] == 2 && order[1] == 1 && order[2] == 3,
        "the pinned section reordered while the unpinned column stayed last"
    );

    require(
        grid.move_column(1, 3, ColumnEdge::after) == DataGridResult::ignored,
        "moving a pinned column across the pin boundary is rejected"
    );
    require(
        grid.move_column(3, 1, ColumnEdge::before) == DataGridResult::ignored,
        "moving an unpinned column across the pin boundary is rejected"
    );
    grid.copy_visible_columns(order);
    require(
        order[0] == 2 && order[1] == 1 && order[2] == 3,
        "rejected cross-boundary moves leave the order alone"
    );

    require(
        grid.resize_column(1, 150.0F) == DataGridResult::changed && grid.column_width(1) == 150.0F,
        "resizing a pinned column works like any other resize"
    );
}

void check_hide_and_show() {
    auto grid = make_grid();

    require(grid.visible_count() == 3, "every fixture column starts visible");
    require(!grid.column_hidden(2), "column two starts shown");
    require(grid.set_hidden(2, true) == DataGridResult::changed, "hiding reports a change");
    require(grid.column_hidden(2), "the hidden flag reads back");
    require(grid.visible_count() == 2, "the visible count drops");
    require(
        grid.set_hidden(2, true) == DataGridResult::handled,
        "repeating the same flag reports no change"
    );
    require(grid.set_hidden(99, true) == DataGridResult::ignored, "an unknown id is ignored");
    require(!grid.column_hidden(99), "an unknown column reports no hidden flag");

    std::array<std::uint64_t, 3> visible{};
    require(grid.copy_visible_columns(visible) == 2, "two columns stay visible");
    require(visible[0] == 1 && visible[1] == 3, "hidden columns drop out of visible order");

    require(grid.set_hidden(2, false) == DataGridResult::changed, "showing reports a change");
    require(grid.copy_visible_columns(visible) == 3, "the column returns to its slot");
    require(
        visible[0] == 1 && visible[1] == 2 && visible[2] == 3,
        "hiding never reorders; the slot is stable"
    );
}

void check_value_kind_round_trip() {
    std::array<veyra::DataGridColumn, 3> spec{kSpec[0], kSpec[1], kSpec[2]};
    spec[0].value_kind = DataGridValueKind::number;
    spec[1].value_kind = DataGridValueKind::date;
    spec[2].value_kind = DataGridValueKind::custom;

    auto grid = veyra::DataGridController{};
    require(grid.set_columns(spec) == DataGridResult::changed, "the tagged spec is accepted");

    std::array<veyra::DataGridColumn, 3> back{};
    require(grid.copy_columns(back) == 3, "the spec reads back");
    require(
        back[0].value_kind == DataGridValueKind::number &&
            back[1].value_kind == DataGridValueKind::date &&
            back[2].value_kind == DataGridValueKind::custom,
        "value kinds round-trip as pure tags for future comparators and editors"
    );
}

void check_multi_sort_replace_and_accumulate() {
    auto grid = make_grid();

    require(!grid.has_sort(), "a new grid sorts nothing");
    require(grid.sort_count() == 0, "the sort list starts empty");

    require(
        grid.sort_by(1, SortDirection::ascending) == DataGridResult::changed,
        "recording a sort reports a change"
    );
    require(grid.has_sort() && grid.sort_count() == 1, "one sort is stored");

    std::array<veyra::SortSpec, 8> sorts{};
    require(grid.sort_specs(sorts) == 1, "the sort list reads back");
    require(
        sorts[0].column == 1 && sorts[0].direction == SortDirection::ascending,
        "the sort records the column and direction"
    );
    require(
        grid.sort_by(1, SortDirection::ascending) == DataGridResult::handled,
        "repeating the same spec reports no change and never cycles"
    );
    require(
        grid.sort_by(1, SortDirection::descending) == DataGridResult::changed,
        "only an explicit call flips the direction; the grid never cycles it"
    );

    require(
        grid.sort_by(2, SortDirection::ascending) == DataGridResult::changed,
        "sorting another column replaces by default"
    );
    require(grid.sort_count() == 1, "replace keeps a single primary sort");
    require(grid.sort_specs(sorts) == 1 && sorts[0].column == 2, "the new column is primary");

    require(
        grid.sort_by(1, SortDirection::ascending, true) == DataGridResult::changed,
        "shift-click semantics append a secondary sort"
    );
    require(grid.sort_count() == 2, "both sorts are stored");
    require(
        grid.sort_specs(sorts) == 2 && sorts[0].column == 2 && sorts[1].column == 1,
        "accumulate appends after the primary instead of replacing it"
    );

    require(
        grid.sort_by(1, SortDirection::descending, true) == DataGridResult::changed,
        "re-adding an existing sort promotes it"
    );
    require(
        grid.sort_specs(sorts) == 2 && sorts[0].column == 1 && sorts[1].column == 2 &&
            sorts[0].direction == SortDirection::descending,
        "the promoted sort moves to front with its new direction"
    );
    require(
        grid.sort_by(1, SortDirection::descending, true) == DataGridResult::handled,
        "repeating the front sort reports no change"
    );

    require(
        grid.sort_by(3, SortDirection::ascending) == DataGridResult::ignored &&
            grid.sort_by(3, SortDirection::ascending, true) == DataGridResult::ignored,
        "an unsortable column rejects the sort either way"
    );
    require(
        grid.sort_by(99, SortDirection::ascending, true) == DataGridResult::ignored,
        "an unknown column rejects the accumulated sort"
    );
    require(grid.sort_count() == 2, "rejected sorts leave the list alone");

    require(grid.clear_sort() == DataGridResult::changed && !grid.has_sort(), "clearing drops it");
    require(grid.sort_count() == 0, "the list is empty after clearing");
    require(
        grid.clear_sort() == DataGridResult::handled,
        "clearing an empty list reports no change"
    );
}

void check_multi_sort_capacity() {
    std::array<veyra::DataGridColumn, 9> spec{};

    for (std::size_t i = 0; i < spec.size(); ++i) {
        spec[i] = {
            .id = static_cast<std::uint64_t>(i + 1),
            .width = 50.0F,
            .min_width = 10.0F,
            .max_width = 100.0F,
            .sortable = true,
        };
    }

    auto grid = veyra::DataGridController{};
    require(grid.set_columns(spec) == DataGridResult::changed, "the wide spec is accepted");
    require(grid.sort_by(1, SortDirection::ascending) == DataGridResult::changed, "first sort");

    for (std::uint64_t id = 2; id <= 8; ++id) {
        require(
            grid.sort_by(id, SortDirection::ascending, true) == DataGridResult::changed,
            "accumulating fills the ordered list"
        );
    }
    require(grid.sort_count() == 8, "the list holds eight sorts");
    require(
        grid.sort_by(9, SortDirection::ascending, true) == DataGridResult::overflow &&
            grid.sort_count() == 8,
        "a ninth accumulated sort overflows and leaves the list alone"
    );

    std::array<veyra::SortSpec, 4> short_buf{};
    require(grid.sort_specs(short_buf) == 4, "a short buffer truncates to its own size");
    require(short_buf[0].column == 1 && short_buf[3].column == 4, "truncation keeps list order");

    std::array<veyra::DataGridColumn, 9> resized{spec};
    for (std::size_t i = 0; i < resized.size(); ++i) {
        resized[i].width = 60.0F;
    }
    require(grid.set_columns(resized) == DataGridResult::changed, "resizing the spec works");
    require(grid.sort_count() == 8, "a full list survives a prune that drops nothing");

    std::array<veyra::SortSpec, 8> kept{};
    require(
        grid.sort_specs(kept) == 8 && kept[0].column == 1 && kept[7].column == 8,
        "the full list keeps order through the prune"
    );
}

void check_replace_prunes_stale_sorts() {
    auto grid = make_grid();
    grid.sort_by(1, SortDirection::ascending);
    grid.sort_by(2, SortDirection::descending, true);
    require(grid.sort_count() == 2, "two sorts are stored");

    require(grid.set_hidden(1, true) == DataGridResult::changed, "hiding the sorted column works");
    require(grid.sort_count() == 2, "hiding keeps the spec; the data order is the app's business");

    std::array<veyra::DataGridColumn, 1> narrow{{
        {.id = 2, .width = 80.0F, .min_width = 40.0F, .max_width = 200.0F, .sortable = true},
    }};
    require(grid.set_columns(narrow) == DataGridResult::changed, "narrowing the spec works");
    require(grid.sort_count() == 1, "dropping a sorted column prunes just that entry");

    std::array<veyra::SortSpec, 8> sorts{};
    require(
        grid.sort_specs(sorts) == 1 && sorts[0].column == 2 &&
            sorts[0].direction == SortDirection::descending,
        "the surviving sort keeps its place and direction"
    );

    std::array<veyra::DataGridColumn, 1> unsortable{{
        {.id = 2, .width = 80.0F, .min_width = 40.0F, .max_width = 200.0F},
    }};
    require(grid.set_columns(unsortable) == DataGridResult::changed, "clearing sortable works");
    require(!grid.has_sort(), "losing the sortable flag clears the spec");
}

void check_replace_clears_stale_cursor() {
    auto grid = make_grid();
    require(
        grid.set_cursor(10, 2, kRows) == DataGridResult::changed,
        "the fixture cursor is placed"
    );

    std::array<veyra::DataGridColumn, 1> narrow{{
        {.id = 3, .width = 120.0F, .min_width = 40.0F, .max_width = 200.0F},
    }};
    require(grid.set_columns(narrow) == DataGridResult::changed, "narrowing the spec works");
    require(!grid.has_cursor(), "dropping the focused column clears the cursor");
}

void check_customization_snapshot_round_trip() {
    auto grid = make_grid();
    grid.set_pinned(3, DataGridPin::left);
    grid.set_hidden(2, true);
    grid.resize_column(1, 150.0F);

    std::array<ColumnCustomization, 3> snapshot{};
    require(grid.copy_customization(snapshot) == 3, "the snapshot fills the caller buffer");
    require(
        snapshot[0].id == 1 && snapshot[0].width == 150.0F && !snapshot[0].hidden &&
            snapshot[0].pinned == DataGridPin::none && snapshot[0].position == 0,
        "the first entry carries width, flags, and display position"
    );
    require(
        snapshot[1].hidden && snapshot[2].pinned == DataGridPin::left,
        "hidden and pin flags survive the snapshot"
    );

    auto restored = veyra::DataGridController{};
    require(restored.set_columns(kSpec) == DataGridResult::changed, "a fresh grid takes defaults");
    require(
        restored.apply_customization(snapshot) == DataGridResult::changed,
        "applying the snapshot reports a change"
    );

    std::array<ColumnCustomization, 3> again{};
    require(restored.copy_customization(again) == 3, "the applied state snapshots again");
    require(
        again[0].id == 1 && again[0].width == 150.0F && again[1].hidden &&
            again[2].pinned == DataGridPin::left,
        "width, hidden, and pin flags transferred across controllers"
    );
    require(
        restored.apply_customization(snapshot) == DataGridResult::handled,
        "reapplying the same snapshot reports no change"
    );

    std::array<ColumnCustomization, 1> small{};
    require(
        restored.copy_customization(small) == 1 && small[0].id == 1,
        "a short snapshot buffer truncates to its own size"
    );
}

void check_customization_reorders_by_position() {
    auto grid = make_grid();

    std::array<ColumnCustomization, 3> reorder{{
        {.id = 3, .width = 120.0F, .position = 0},
        {.id = 1, .width = 100.0F, .position = 1},
        {.id = 2, .width = 80.0F, .position = 2},
    }};
    require(
        grid.apply_customization(reorder) == DataGridResult::changed,
        "applying a repositioned snapshot reports a change"
    );

    std::array<std::uint64_t, 3> order{};
    require(grid.copy_visible_columns(order) == 3, "three columns stay visible");
    require(
        order[0] == 3 && order[1] == 1 && order[2] == 2,
        "apply reorders by the stored positions"
    );

    std::array<ColumnCustomization, 1> partial{{
        {.id = 2, .width = 80.0F, .position = 0},
    }};
    require(
        grid.apply_customization(partial) == DataGridResult::changed,
        "a partial snapshot still applies"
    );
    require(grid.copy_visible_columns(order) == 3, "the order reads back");
    require(
        order[0] == 2 && order[1] == 3 && order[2] == 1,
        "mentioned columns lead by position; the rest keep relative order behind"
    );
}

void check_customization_ignores_unknown() {
    auto grid = make_grid();

    std::array<ColumnCustomization, 2> unknown{{
        {.id = 70, .width = 50.0F, .position = 0},
        {.id = 80, .width = 60.0F, .position = 1},
    }};
    require(
        grid.apply_customization(unknown) == DataGridResult::ignored,
        "a snapshot with only unknown ids is ignored"
    );

    std::array<ColumnCustomization, 0> empty{};
    require(
        grid.apply_customization(empty) == DataGridResult::ignored,
        "an empty snapshot is ignored"
    );

    std::array<std::uint64_t, 3> order{};
    require(grid.copy_visible_columns(order) == 3, "nothing moved");
    require(
        order[0] == 1 && order[1] == 2 && order[2] == 3,
        "ignored snapshots leave the order alone"
    );
}

void check_density_row_height() {
    auto grid = make_grid();

    require(grid.density() == DataGridDensity::standard, "standard density is the default");
    require(grid.density_row_height() == 32.0F, "the standard row height reads back");

    require(
        grid.set_density(DataGridDensity::compact) == DataGridResult::changed &&
            grid.density() == DataGridDensity::compact && grid.density_row_height() == 24.0F,
        "compact density reports a change with its row height"
    );
    require(
        grid.set_density(DataGridDensity::compact) == DataGridResult::handled,
        "repeating the density reports no change"
    );
    require(
        grid.set_density(DataGridDensity::comfortable) == DataGridResult::changed &&
            grid.density_row_height() == 40.0F,
        "comfortable density carries the tallest hint for later layout"
    );
}

void check_cursor_placement() {
    auto grid = make_grid();

    constexpr std::array<std::uint64_t, 0> no_rows{};
    veyra::CellMove idle = grid.move_cursor(DataGridMove::down, kRows, 2);
    require(idle.result == DataGridResult::ignored, "moves without a cursor are ignored");
    require(
        grid.begin_edit(kRows).result == DataGridResult::ignored,
        "edit without a cursor is ignored"
    );

    require(
        grid.set_cursor(10, 1, kRows) == DataGridResult::changed,
        "placing the cursor reports a change"
    );
    require(grid.has_cursor(), "the cursor reads back");

    const veyra::CellCursor at = grid.cursor();
    require(at.has_cursor && at.row == 10 && at.column == 1, "the cursor holds row and column");

    require(
        grid.set_cursor(10, 1, kRows) == DataGridResult::handled,
        "repeating the same cell reports no change"
    );
    require(
        grid.set_cursor(99, 1, kRows) == DataGridResult::ignored,
        "an unknown row is ignored and keeps the cursor"
    );
    require(
        grid.set_cursor(10, 99, kRows) == DataGridResult::ignored,
        "an unknown column is ignored and keeps the cursor"
    );
    require(
        grid.set_cursor(10, 1, no_rows) == DataGridResult::ignored,
        "an empty row span is ignored"
    );

    grid.set_hidden(2, true);
    require(
        grid.set_cursor(10, 2, kRows) == DataGridResult::ignored,
        "a hidden column refuses explicit focus"
    );

    require(grid.clear_cursor() == DataGridResult::changed && !grid.has_cursor(), "clearing works");
    require(
        grid.clear_cursor() == DataGridResult::handled,
        "clearing without a cursor reports no change"
    );
}

void require_move(
    veyra::DataGridController& grid,
    DataGridMove move,
    DataGridResult result,
    std::uint64_t row,
    std::uint64_t column
) {
    const veyra::CellMove outcome = grid.move_cursor(move, kRows, 2);

    require(
        outcome.result == result && outcome.row == row && outcome.column == column,
        "the cursor move lands where expected"
    );
}

void check_arrow_moves() {
    auto grid = make_grid();
    grid.set_cursor(20, 2, kRows);

    require_move(grid, DataGridMove::up, DataGridResult::moved, 10, 2);
    require_move(grid, DataGridMove::up, DataGridResult::handled, 10, 2);
    require_move(grid, DataGridMove::down, DataGridResult::moved, 20, 2);
    require_move(grid, DataGridMove::left, DataGridResult::moved, 20, 1);
    require_move(grid, DataGridMove::left, DataGridResult::handled, 20, 1);
    require_move(grid, DataGridMove::right, DataGridResult::moved, 20, 2);
    require_move(grid, DataGridMove::right, DataGridResult::moved, 20, 3);
    require_move(grid, DataGridMove::right, DataGridResult::handled, 20, 3);
    require_move(grid, DataGridMove::home, DataGridResult::moved, 20, 1);
    require_move(grid, DataGridMove::home, DataGridResult::handled, 20, 1);
    require_move(grid, DataGridMove::end, DataGridResult::moved, 20, 3);
    require_move(grid, DataGridMove::end, DataGridResult::handled, 20, 3);

    grid.set_cursor(40, 2, kRows);
    require_move(grid, DataGridMove::down, DataGridResult::handled, 40, 2);
}

void check_moves_skip_hidden_columns() {
    auto grid = make_grid();
    grid.set_hidden(2, true);
    grid.set_cursor(20, 1, kRows);

    require_move(grid, DataGridMove::right, DataGridResult::moved, 20, 3);
    require_move(grid, DataGridMove::left, DataGridResult::moved, 20, 1);
    require_move(grid, DataGridMove::end, DataGridResult::moved, 20, 3);
    require_move(grid, DataGridMove::home, DataGridResult::moved, 20, 1);

    grid.set_hidden(3, true);
    grid.set_cursor(20, 1, kRows);
    require_move(grid, DataGridMove::right, DataGridResult::handled, 20, 1);
    require_move(grid, DataGridMove::end, DataGridResult::handled, 20, 1);
}

void check_hide_snaps_focus_left() {
    auto grid = make_grid();
    grid.set_cursor(20, 2, kRows);

    require(grid.set_hidden(2, true) == DataGridResult::changed, "hiding the focused column works");

    const veyra::CellCursor at = grid.cursor();
    require(
        at.has_cursor && at.row == 20 && at.column == 1,
        "focus falls back to the predecessor, keeping the row"
    );

    grid.set_hidden(1, true);
    require(
        grid.cursor().column == 3,
        "hiding the first column falls forward when nothing sits left"
    );

    grid.set_hidden(3, true);
    require(grid.has_cursor(), "the cursor holder stays with no visible columns");
    require(
        grid.move_cursor(DataGridMove::left, kRows, 2).result == DataGridResult::ignored,
        "moves with no visible columns are ignored"
    );
    require(
        grid.begin_edit(kRows).result == DataGridResult::ignored,
        "edit with no visible columns is ignored"
    );
}

void check_page_and_tab_moves() {
    auto grid = make_grid();
    grid.set_cursor(10, 2, kRows);

    require_move(grid, DataGridMove::page_down, DataGridResult::moved, 30, 2);
    require_move(grid, DataGridMove::page_down, DataGridResult::moved, 40, 2);
    require_move(grid, DataGridMove::page_down, DataGridResult::handled, 40, 2);
    require_move(grid, DataGridMove::page_up, DataGridResult::moved, 20, 2);
    require_move(grid, DataGridMove::page_up, DataGridResult::moved, 10, 2);
    require_move(grid, DataGridMove::page_up, DataGridResult::handled, 10, 2);

    require(
        grid.move_cursor(DataGridMove::page_down, kRows, 0).result == DataGridResult::ignored,
        "a zero page step rejects the call"
    );
    require(
        grid.move_cursor(DataGridMove::page_up, kRows, 0).result == DataGridResult::ignored,
        "a zero page step rejects paging up too"
    );

    grid.set_cursor(10, 1, kRows);
    require_move(grid, DataGridMove::next, DataGridResult::moved, 10, 2);
    grid.set_cursor(10, 3, kRows);
    require_move(grid, DataGridMove::prev, DataGridResult::moved, 10, 2);

    grid.set_cursor(10, 3, kRows);
    require_move(grid, DataGridMove::next, DataGridResult::moved, 20, 1);
    require_move(grid, DataGridMove::prev, DataGridResult::moved, 10, 3);

    grid.set_cursor(40, 3, kRows);
    require_move(grid, DataGridMove::next, DataGridResult::handled, 40, 3);

    grid.set_cursor(10, 1, kRows);
    require_move(grid, DataGridMove::prev, DataGridResult::handled, 10, 1);
}

void check_rejected_moves() {
    auto grid = make_grid();
    grid.set_cursor(20, 2, kRows);

    constexpr std::array<std::uint64_t, 0> no_rows{};
    require(
        grid.move_cursor(DataGridMove::down, no_rows, 2).result == DataGridResult::ignored,
        "an empty row span rejects the move"
    );

    constexpr std::array<std::uint64_t, 2> other{70, 80};
    require(
        grid.move_cursor(DataGridMove::down, other, 2).result == DataGridResult::ignored,
        "a span without the focused row rejects the move"
    );
    require(
        grid.cursor().row == 20 && grid.cursor().column == 2,
        "rejected moves leave the cursor alone"
    );
    require(
        grid.begin_edit(other).result == DataGridResult::ignored,
        "edit with a stale row is ignored"
    );
}

void check_edit_commit_and_cancel() {
    auto grid = make_grid();
    grid.set_cursor(30, 3, kRows);

    require(!grid.edit_pending(), "no edit pends before begin_edit");
    require(
        grid.pending_edit().result == DataGridResult::ignored,
        "the pending accessor reports ignored with nothing pending"
    );
    require(
        grid.commit_edit().result == DataGridResult::ignored,
        "committing with no pending edit is ignored"
    );
    require(
        grid.cancel_edit() == DataGridResult::handled,
        "cancelling with no pending edit reports no change"
    );

    const veyra::CellEdit edit = grid.begin_edit(kRows);
    require(
        edit.result == DataGridResult::edit && edit.row == 30 && edit.column == 3,
        "begin_edit reports the focused cell for the app to open"
    );
    require(grid.edit_pending(), "the edit pends until the caller validates");
    require(grid.has_cursor(), "reporting edit never moves the cursor");

    const veyra::CellEdit pending = grid.pending_edit();
    require(
        pending.result == DataGridResult::edit && pending.row == 30 && pending.column == 3,
        "the pending accessor mirrors the open cell"
    );
    require(
        grid.begin_edit(kRows).result == DataGridResult::handled && grid.edit_pending(),
        "re-beginning the same cell reports no change and keeps the pending edit"
    );

    const veyra::CellEdit committed = grid.commit_edit();
    require(
        committed.result == DataGridResult::changed && committed.row == 30 && committed.column == 3,
        "commit clears the pending edit and reports the validated cell"
    );
    require(!grid.edit_pending(), "nothing pends after commit");

    grid.begin_edit(kRows);
    require(grid.cancel_edit() == DataGridResult::handled, "cancel reports handled");
    require(!grid.edit_pending(), "nothing pends after cancel");
    require(
        grid.cursor().row == 30 && grid.cursor().column == 3,
        "cancel leaves the cursor where the edit started"
    );
}

void check_edit_row_mode() {
    auto grid = make_grid();

    require(!grid.edit_row_mode(), "cell editing is the default");
    require(
        grid.edit_row_mode(true) == DataGridResult::changed && grid.edit_row_mode(),
        "switching to full-row mode reports a change"
    );
    require(
        grid.edit_row_mode(true) == DataGridResult::handled,
        "repeating the row mode reports no change"
    );
    require(
        grid.edit_row_mode(false) == DataGridResult::changed && !grid.edit_row_mode(),
        "switching back to cell mode works"
    );
}

void check_cursor_move_cancels_pending_edit() {
    auto grid = make_grid();
    grid.set_cursor(20, 2, kRows);
    grid.begin_edit(kRows);
    require(grid.edit_pending(), "the edit pends");

    const veyra::CellMove stayed = grid.move_cursor(DataGridMove::down, kRows, 2);
    require(
        stayed.result == DataGridResult::handled && stayed.row == 20 && stayed.column == 2,
        "moving with a pending edit cancels first and stays put, returning the cancel outcome"
    );
    require(!grid.edit_pending(), "the move cancelled the pending edit");

    require_move(grid, DataGridMove::down, DataGridResult::moved, 30, 2);
    require(grid.cursor().row == 30, "the retry moves after the cancel");
}

void check_set_cursor_cancels_pending_edit() {
    auto grid = make_grid();
    grid.set_cursor(20, 2, kRows);
    grid.begin_edit(kRows);

    require(
        grid.set_cursor(30, 2, kRows) == DataGridResult::handled,
        "refocusing with a pending edit cancels first, returning the cancel outcome"
    );
    require(!grid.edit_pending(), "the refocus cancelled the pending edit");
    require(
        grid.cursor().row == 20 && grid.cursor().column == 2,
        "the cursor stays until the caller retries"
    );
    require(
        grid.set_cursor(30, 2, kRows) == DataGridResult::changed,
        "the retry refocuses after the cancel"
    );
}

void check_sorting_cancels_pending_edit() {
    auto grid = make_grid();
    grid.sort_by(1, SortDirection::ascending);
    grid.set_cursor(20, 2, kRows);
    grid.begin_edit(kRows);

    require(
        grid.sort_by(2, SortDirection::ascending) == DataGridResult::handled,
        "sorting with a pending edit cancels first, returning the cancel outcome"
    );
    require(!grid.edit_pending(), "the sort cancelled the pending edit");
    require(grid.sort_count() == 1, "the sort waits until the caller retries");

    std::array<veyra::SortSpec, 8> sorts{};
    grid.sort_specs(sorts);
    require(sorts[0].column == 1, "the stored sort is untouched by the cancelled call");
    require(
        grid.sort_by(2, SortDirection::ascending) == DataGridResult::changed,
        "the retry sorts after the cancel"
    );

    grid.begin_edit(kRows);
    require(
        grid.clear_sort() == DataGridResult::handled && grid.sort_count() == 1,
        "clearing with a pending edit cancels first and keeps the sort"
    );
    require(
        grid.clear_sort() == DataGridResult::changed && !grid.has_sort(),
        "the retry clears after the cancel"
    );
}

void check_hiding_edited_column_cancels_pending_edit() {
    auto grid = make_grid();
    grid.set_cursor(20, 2, kRows);
    grid.begin_edit(kRows);

    require(
        grid.set_hidden(2, true) == DataGridResult::changed && !grid.edit_pending(),
        "hiding the edited column cancels the pending edit"
    );

    grid.set_hidden(2, false);
    grid.set_cursor(20, 2, kRows);
    grid.begin_edit(kRows);
    require(grid.edit_pending(), "a fresh edit pends again");

    std::array<veyra::DataGridColumn, 1> narrow{{
        {.id = 3, .width = 120.0F, .min_width = 40.0F, .max_width = 200.0F},
    }};
    require(grid.set_columns(narrow) == DataGridResult::changed, "narrowing the spec works");
    require(!grid.edit_pending(), "dropping the edited column cancels the pending edit");
}

void check_header_semantics() {
    auto grid = make_grid();
    grid.sort_by(1, SortDirection::ascending);
    grid.sort_by(2, SortDirection::descending, true);

    auto node = veyra::SemanticNodeData{
        .role = veyra::SemanticRole::text,
        .label = "unchanged",
    };
    require(!grid.write_header_semantics(1, {}, node), "an empty accessible name is rejected");
    require(node.role == veyra::SemanticRole::text, "rejected output stays unchanged");
    require(!grid.write_header_semantics(99, "Name", node), "an unknown column is rejected");

    require(grid.write_header_semantics(1, "Name", node), "the first sorted header writes");
    require(
        node.role == veyra::SemanticRole::column_header && node.label == "Name" &&
            veyra::has_state(node.states, veyra::SemanticState::sorted_ascending) &&
            !veyra::has_state(node.states, veyra::SemanticState::sorted_descending),
        "the ascending header exposes its direction"
    );

    require(grid.write_header_semantics(2, "Age", node), "the second sorted header writes");
    require(
        veyra::has_state(node.states, veyra::SemanticState::sorted_descending),
        "a multi-sort secondary still reads back its own direction"
    );
    require(
        grid.write_header_semantics(3, "Note", node) &&
            !veyra::has_state(node.states, veyra::SemanticState::sorted_ascending) &&
            !veyra::has_state(node.states, veyra::SemanticState::sorted_descending),
        "an unsorted header stays plain while the sort list holds others"
    );
}

void check_overflow_past_capacity() {
    auto grid = veyra::DataGridController{};

    std::array<veyra::DataGridColumn, veyra::DataGridController::kCapacity> exact{};
    for (std::size_t i = 0; i < exact.size(); ++i) {
        exact[i] = {
            .id = static_cast<std::uint64_t>(i + 1),
            .width = 50.0F,
            .min_width = 10.0F,
            .max_width = 100.0F,
            .sortable = true,
        };
    }
    require(
        grid.set_columns(exact) == DataGridResult::changed &&
            grid.column_count() == veyra::DataGridController::kCapacity,
        "a spec exactly at capacity is accepted"
    );

    std::array<veyra::DataGridColumn, veyra::DataGridController::kCapacity + 1> too_many{};
    for (std::size_t i = 0; i < too_many.size(); ++i) {
        too_many[i] = {
            .id = static_cast<std::uint64_t>(i + 1),
            .width = 50.0F,
            .min_width = 10.0F,
            .max_width = 100.0F,
        };
    }
    require(
        grid.set_columns(too_many) == DataGridResult::overflow &&
            grid.column_count() == veyra::DataGridController::kCapacity,
        "one column past capacity overflows without touching the spec"
    );
}

}

int main() {
    try {
        check_replace_and_order();
        check_replace_rejects_bad_specs();
        check_resize_clamps_exactly_once();
        check_reorder();
        check_locked_columns_reject_reorder_as_source_and_target();
        check_pinning_groups_visible_order();
        check_pinning_constrains_reorder();
        check_hide_and_show();
        check_value_kind_round_trip();
        check_multi_sort_replace_and_accumulate();
        check_multi_sort_capacity();
        check_replace_prunes_stale_sorts();
        check_replace_clears_stale_cursor();
        check_customization_snapshot_round_trip();
        check_customization_reorders_by_position();
        check_customization_ignores_unknown();
        check_density_row_height();
        check_cursor_placement();
        check_arrow_moves();
        check_moves_skip_hidden_columns();
        check_hide_snaps_focus_left();
        check_page_and_tab_moves();
        check_rejected_moves();
        check_edit_commit_and_cancel();
        check_edit_row_mode();
        check_cursor_move_cancels_pending_edit();
        check_set_cursor_cancels_pending_edit();
        check_sorting_cancels_pending_edit();
        check_hiding_edited_column_cancels_pending_edit();
        check_header_semantics();
        check_overflow_past_capacity();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
