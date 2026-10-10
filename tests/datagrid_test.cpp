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

using veyra::ColumnEdge;
using veyra::DataGridMove;
using veyra::DataGridResult;
using veyra::SortDirection;

static_assert(noexcept(veyra::DataGridController{}.column_count()));
static_assert(
    noexcept(veyra::DataGridController{}.set_columns(std::span<const veyra::DataGridColumn>{}))
);
static_assert(noexcept(veyra::DataGridController{}
                           .move_cursor(DataGridMove::up, std::span<const std::uint64_t>{}, 0)));

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
        back[0].width == 100.0F && back[0].sortable && !back[0].hidden,
        "column fields survive the round trip"
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

void check_sort_spec_and_click_policy() {
    auto grid = make_grid();

    require(!grid.has_sort(), "a new grid sorts nothing");
    require(
        grid.sort_by(1, SortDirection::ascending) == DataGridResult::changed,
        "recording a sort reports a change"
    );

    const veyra::SortSpec spec = grid.sort_spec();
    require(
        spec.has_sort && spec.column == 1 && spec.direction == SortDirection::ascending,
        "the sort spec records the column and direction"
    );
    require(
        grid.sort_by(1, SortDirection::ascending) == DataGridResult::handled,
        "repeating the same spec reports no change and never cycles"
    );
    require(
        grid.sort_by(1, SortDirection::descending) == DataGridResult::changed &&
            grid.sort_spec().direction == SortDirection::descending,
        "only an explicit call flips the direction; the grid never cycles it"
    );
    require(
        grid.sort_by(3, SortDirection::ascending) == DataGridResult::ignored,
        "an unsortable column rejects the sort"
    );
    require(
        grid.sort_by(99, SortDirection::ascending) == DataGridResult::ignored,
        "an unknown column rejects the sort"
    );

    require(grid.set_hidden(1, true) == DataGridResult::changed, "hiding the sorted column works");
    require(
        grid.has_sort() && grid.sort_spec().column == 1,
        "hiding keeps the spec; the data order is the app's business"
    );

    require(grid.clear_sort() == DataGridResult::changed && !grid.has_sort(), "clearing drops it");
    require(
        grid.clear_sort() == DataGridResult::handled,
        "clearing an empty spec reports no change"
    );
}

void check_replace_clears_stale_sort_and_cursor() {
    auto grid = make_grid();
    grid.sort_by(2, SortDirection::ascending);
    require(
        grid.set_cursor(10, 2, kRows) == DataGridResult::changed,
        "the fixture cursor is placed"
    );

    std::array<veyra::DataGridColumn, 1> narrow{{
        {.id = 3, .width = 120.0F, .min_width = 40.0F, .max_width = 200.0F},
    }};
    require(grid.set_columns(narrow) == DataGridResult::changed, "narrowing the spec works");
    require(!grid.has_sort(), "dropping the sorted column clears the spec");
    require(!grid.has_cursor(), "dropping the focused column clears the cursor");
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

void check_edit_intent() {
    auto grid = make_grid();
    grid.set_cursor(30, 3, kRows);

    const veyra::CellEdit edit = grid.begin_edit(kRows);
    require(
        edit.result == DataGridResult::edit && edit.row == 30 && edit.column == 3,
        "begin_edit reports the focused cell for the app to open"
    );
    require(grid.has_cursor(), "reporting edit never moves the cursor");
}

void check_header_semantics() {
    auto grid = make_grid();
    grid.sort_by(1, SortDirection::ascending);

    auto node = veyra::SemanticNodeData{
        .role = veyra::SemanticRole::text,
        .label = "unchanged",
    };
    require(!grid.write_header_semantics(1, {}, node), "an empty accessible name is rejected");
    require(node.role == veyra::SemanticRole::text, "rejected output stays unchanged");
    require(!grid.write_header_semantics(99, "Name", node), "an unknown column is rejected");

    require(grid.write_header_semantics(1, "Name", node), "the sorted header writes a record");
    require(
        node.role == veyra::SemanticRole::column_header && node.label == "Name" &&
            veyra::has_state(node.states, veyra::SemanticState::sorted_ascending) &&
            !veyra::has_state(node.states, veyra::SemanticState::sorted_descending),
        "the sorted header exposes its direction"
    );

    grid.sort_by(2, SortDirection::descending);
    require(grid.write_header_semantics(2, "Age", node), "the second header writes a record");
    require(
        veyra::has_state(node.states, veyra::SemanticState::sorted_descending),
        "a descending sort reads back"
    );
    require(
        grid.write_header_semantics(1, "Name", node) &&
            !veyra::has_state(node.states, veyra::SemanticState::sorted_ascending) &&
            !veyra::has_state(node.states, veyra::SemanticState::sorted_descending),
        "single-column sort leaves the other headers plain"
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
        check_hide_and_show();
        check_sort_spec_and_click_policy();
        check_replace_clears_stale_sort_and_cursor();
        check_cursor_placement();
        check_arrow_moves();
        check_moves_skip_hidden_columns();
        check_hide_snaps_focus_left();
        check_page_and_tab_moves();
        check_rejected_moves();
        check_edit_intent();
        check_header_semantics();
        check_overflow_past_capacity();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
