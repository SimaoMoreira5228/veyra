#include "veyra/behaviors/cell_range.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

void check_normalize_both_directions() {
    constexpr veyra::CellRangeOrder forward = veyra::normalize_cell_range(1, 3, 0, 2);
    require(
        forward.first_row == 1 && forward.last_row == 3 && forward.first_column == 0 &&
            forward.last_column == 2,
        "a forward drag keeps anchor-first order"
    );

    constexpr veyra::CellRangeOrder backward = veyra::normalize_cell_range(3, 1, 2, 0);
    require(
        backward.first_row == 1 && backward.last_row == 3 && backward.first_column == 0 &&
            backward.last_column == 2,
        "a backward drag normalizes to the same rectangle"
    );

    constexpr veyra::CellRangeOrder single = veyra::normalize_cell_range(2, 2, 4, 4);
    require(
        single.first_row == 2 && single.last_row == 2 && single.first_column == 4 &&
            single.last_column == 4,
        "a single cell normalizes to a one-by-one rectangle"
    );

    constexpr veyra::CellRangeOrder mixed = veyra::normalize_cell_range(5, 1, 0, 7);
    require(
        mixed.first_row == 1 && mixed.last_row == 5 && mixed.first_column == 0 &&
            mixed.last_column == 7,
        "rows and columns normalize independently of each other"
    );
}

void check_anchor_and_extend() {
    auto control = veyra::CellRangeController{};
    require(!control.has_range(), "a new controller covers nothing");

    std::array<std::uint64_t, 5> rows{10, 20, 30, 40, 50};
    std::array<std::uint64_t, 4> columns{1, 2, 3, 4};
    require(
        control.extend_to(30, 2, rows, columns) == veyra::SelectionResult::ignored &&
            !control.has_range(),
        "extending without an anchor is ignored"
    );

    require(
        control.set_anchor(20, 2) == veyra::SelectionResult::changed && control.has_range(),
        "setting an anchor opens a range"
    );
    require(
        control.set_anchor(20, 2) == veyra::SelectionResult::handled,
        "repeating the same anchor reports no change"
    );
    require(
        control.row_span_count(rows) == 1 && control.column_span_count(columns) == 1,
        "an anchor alone covers exactly one cell"
    );
    require(control.contains(20, 2, 1, 1), "an anchor alone contains its own cell by id");
    require(!control.contains(20, 3, 1, 2), "an anchor alone contains nothing else");

    require(
        control.extend_to(40, 3, rows, columns) == veyra::SelectionResult::changed,
        "extending to a known cell changes the corner"
    );
    require(
        control.extend_to(40, 3, rows, columns) == veyra::SelectionResult::handled,
        "extending to the same corner reports no change"
    );

    const veyra::CellRange rect = control.range();
    require(
        rect.start_row == 20 && rect.start_column == 2 && rect.end_row == 40 &&
            rect.end_column == 3,
        "the stored rect keeps anchor-first direction after extension"
    );
    require(
        control.row_span_count(rows) == 3 && control.column_span_count(columns) == 2,
        "span counts cover anchor-to-corner inclusive"
    );
    require(
        control.contains(30, 2, 2, 1) && control.contains(20, 3, 1, 2) &&
            control.contains(40, 3, 3, 2),
        "interior cells and both corners are contained"
    );
    require(
        !control.contains(10, 2, 0, 1) && !control.contains(30, 4, 2, 3) &&
            !control.contains(50, 1, 4, 0),
        "cells outside either axis are excluded"
    );

    require(
        control.extend_to(10, 1, rows, columns) == veyra::SelectionResult::changed,
        "extending backward past the anchor changes the corner"
    );
    require(
        control.row_span_count(rows) == 2 && control.column_span_count(columns) == 2,
        "backward extension keeps inclusive counts from the new corner"
    );
    require(
        control.contains(20, 2, 1, 1) && !control.contains(30, 2, 2, 1),
        "the rectangle follows the latest corner, not the history"
    );
}

void check_unknown_ids_rejected() {
    auto control = veyra::CellRangeController{};
    control.set_anchor(20, 2);

    std::array<std::uint64_t, 5> rows{10, 20, 30, 40, 50};
    std::array<std::uint64_t, 4> columns{1, 2, 3, 4};
    require(
        control.extend_to(40, 3, rows, columns) == veyra::SelectionResult::changed,
        "a valid extension lands first"
    );

    require(
        control.extend_to(99, 3, rows, columns) == veyra::SelectionResult::ignored,
        "a corner row outside the span is ignored"
    );
    require(
        control.extend_to(40, 9, rows, columns) == veyra::SelectionResult::ignored,
        "a corner column outside the span is ignored"
    );

    std::array<std::uint64_t, 2> foreign_rows{60, 70};
    require(
        control.extend_to(60, 3, foreign_rows, columns) == veyra::SelectionResult::ignored,
        "an extension is ignored when the anchor left the visible span"
    );
    require(
        control.row_span_count(rows) == 3 && control.column_span_count(columns) == 2,
        "rejected extensions leave the stored rectangle untouched"
    );
    require(
        control.row_span_count(foreign_rows) == 0,
        "span counts report zero when the rect ids are absent"
    );

    std::array<std::uint64_t, 2> buffer{};
    const veyra::CellRangeCopy missing = control.covered_rows(foreign_rows, buffer);
    require(
        missing.result == veyra::SelectionResult::ignored && missing.count == 0,
        "materializing over a span without the rect is ignored"
    );
    require(
        control.covered_columns({}, buffer).result == veyra::SelectionResult::ignored,
        "materializing over an empty span is ignored"
    );
}

void check_row_mode_semantics() {
    auto control = veyra::CellRangeController{};
    require(control.set_mode(veyra::CellRangeMode::row), "switching to row mode reports a change");
    require(!control.set_mode(veyra::CellRangeMode::row), "repeating the mode reports no change");

    std::array<std::uint64_t, 5> rows{10, 20, 30, 40, 50};
    std::array<std::uint64_t, 4> columns{1, 2, 3, 4};
    control.set_anchor(20, 2);
    require(
        control.extend_to(99, 3, rows, columns) == veyra::SelectionResult::ignored,
        "row mode still rejects an unknown corner row"
    );
    require(
        control.extend_to(40, 9, rows, columns) == veyra::SelectionResult::changed,
        "row mode accepts the extension despite an unknown corner column"
    );

    const veyra::CellRange rect = control.range();
    require(
        rect.start_row == 20 && rect.end_row == 40 && rect.end_column == 2,
        "row mode collapses the corner column back onto the anchor"
    );
    require(
        control.contains(30, 4, 2, 3) && control.contains(20, 1, 1, 0),
        "row mode contains any column whose row sits in the band"
    );
    require(!control.contains(50, 2, 4, 1), "row mode still excludes rows outside the band");
    require(control.column_span_count(columns) == 4, "row mode spans the full column width");

    std::array<std::uint64_t, 4> wide{};
    const veyra::CellRangeCopy covered = control.covered_columns(columns, wide);
    require(
        covered.result == veyra::SelectionResult::handled && covered.count == 4 && wide[0] == 1 &&
            wide[3] == 4,
        "row mode materializes every visible column in order"
    );
}

void check_column_mode_semantics() {
    auto control = veyra::CellRangeController{};
    require(
        control.set_mode(veyra::CellRangeMode::column),
        "switching to column mode reports a change"
    );

    std::array<std::uint64_t, 5> rows{10, 20, 30, 40, 50};
    std::array<std::uint64_t, 4> columns{1, 2, 3, 4};
    control.set_anchor(20, 2);
    require(
        control.extend_to(40, 9, rows, columns) == veyra::SelectionResult::ignored,
        "column mode still rejects an unknown corner column"
    );
    require(
        control.extend_to(99, 4, rows, columns) == veyra::SelectionResult::changed,
        "column mode accepts the extension despite an unknown corner row"
    );

    const veyra::CellRange rect = control.range();
    require(
        rect.start_column == 2 && rect.end_column == 4 && rect.end_row == 20,
        "column mode collapses the corner row back onto the anchor"
    );
    require(
        control.contains(50, 3, 4, 2) && control.contains(10, 2, 0, 1),
        "column mode contains any row whose column sits in the band"
    );
    require(!control.contains(30, 1, 2, 0), "column mode still excludes columns outside the band");
    require(control.row_span_count(rows) == 5, "column mode spans the full row height");

    std::array<std::uint64_t, 5> tall{};
    const veyra::CellRangeCopy covered = control.covered_rows(rows, tall);
    require(
        covered.result == veyra::SelectionResult::handled && covered.count == 5 && tall[0] == 10 &&
            tall[4] == 50,
        "column mode materializes every visible row in order"
    );
}

void check_mode_switch_resets() {
    auto control = veyra::CellRangeController{};
    std::array<std::uint64_t, 3> rows{10, 20, 30};
    std::array<std::uint64_t, 3> columns{1, 2, 3};
    control.set_anchor(10, 1);
    control.extend_to(20, 2, rows, columns);
    require(control.has_range(), "a rect is live before the mode switch");

    require(
        control.set_mode(veyra::CellRangeMode::row),
        "a mode switch reports a change even with a live rect"
    );
    require(!control.has_range(), "a mode switch invalidates the stored geometry");
    require(
        control.row_span_count(rows) == 0 && control.column_span_count(columns) == 0,
        "span counts are empty after the reset"
    );
    require(!control.contains(10, 1, 0, 0), "containment is empty after the reset");
    require(
        control.extend_to(20, 1, rows, columns) == veyra::SelectionResult::ignored,
        "extending needs a fresh anchor after the reset"
    );
}

void check_covered_truncation() {
    auto control = veyra::CellRangeController{};
    std::array<std::uint64_t, 5> rows{10, 20, 30, 40, 50};
    std::array<std::uint64_t, 4> columns{1, 2, 3, 4};
    control.set_anchor(50, 4);
    control.extend_to(20, 2, rows, columns);

    std::array<std::uint64_t, 5> wide_rows{};
    const veyra::CellRangeCopy full_rows = control.covered_rows(rows, wide_rows);
    require(
        full_rows.result == veyra::SelectionResult::handled && full_rows.count == 4 &&
            wide_rows[0] == 20 && wide_rows[1] == 30 && wide_rows[2] == 40 && wide_rows[3] == 50,
        "covered rows follow visible order even when the drag ran backward"
    );

    std::array<std::uint64_t, 2> short_rows{};
    const veyra::CellRangeCopy cut_rows = control.covered_rows(rows, short_rows);
    require(
        cut_rows.result == veyra::SelectionResult::overflow && cut_rows.count == 2 &&
            short_rows[0] == 20 && short_rows[1] == 30,
        "a short row buffer truncates to its own prefix and overflows"
    );

    std::array<std::uint64_t, 4> wide_columns{};
    const veyra::CellRangeCopy full_columns = control.covered_columns(columns, wide_columns);
    require(
        full_columns.result == veyra::SelectionResult::handled && full_columns.count == 3 &&
            wide_columns[0] == 2 && wide_columns[2] == 4,
        "covered columns slice the visible order between anchor and corner"
    );

    std::array<std::uint64_t, 1> short_columns{};
    const veyra::CellRangeCopy cut_columns = control.covered_columns(columns, short_columns);
    require(
        cut_columns.result == veyra::SelectionResult::overflow && cut_columns.count == 1 &&
            short_columns[0] == 2,
        "a short column buffer truncates and overflows"
    );

    auto empty = veyra::CellRangeController{};
    std::array<std::uint64_t, 2> buffer{};
    require(
        empty.covered_rows(rows, buffer).result == veyra::SelectionResult::ignored,
        "materializing without a range is ignored"
    );
}

void check_span_counts_on_odd_shapes() {
    auto control = veyra::CellRangeController{};
    std::array<std::uint64_t, 5> rows{7, 3, 9, 1, 5};
    std::array<std::uint64_t, 6> columns{8, 2, 6, 4, 8, 0};

    control.set_anchor(9, 6);
    control.extend_to(3, 4, rows, columns);
    require(control.row_span_count(rows) == 2, "row counts use span positions, not id magnitude");
    require(
        control.column_span_count(columns) == 2,
        "column counts use span positions with duplicates trailing the slice"
    );

    control.set_anchor(5, 0);
    control.extend_to(7, 8, rows, columns);
    require(
        control.row_span_count(rows) == 5 && control.column_span_count(columns) == 6,
        "a corner-to-corner drag covers the whole odd grid"
    );

    control.set_anchor(1, 2);
    require(
        control.row_span_count(rows) == 1 && control.column_span_count(columns) == 1,
        "a lone anchor counts exactly one row and one column"
    );

    auto tall = veyra::CellRangeController{};
    tall.set_mode(veyra::CellRangeMode::column);
    tall.set_anchor(9, 6);
    require(
        tall.row_span_count(rows) == 5,
        "column mode counts every visible row regardless of the anchor"
    );

    auto wide = veyra::CellRangeController{};
    wide.set_mode(veyra::CellRangeMode::row);
    wide.set_anchor(9, 6);
    require(
        wide.column_span_count(columns) == 5,
        "row mode counts each distinct column once despite the doubled id"
    );
}

void check_pinned_visible_order() {
    constexpr std::array<std::uint64_t, 3> visible{2, 1, 3};

    auto control = veyra::CellRangeController{};
    std::array<std::uint64_t, 3> rows{10, 20, 30};
    control.set_anchor(10, 2);
    require(
        control.extend_to(20, 3, rows, visible) == veyra::SelectionResult::changed,
        "the range addresses the visible order with pins included"
    );
    require(
        control.column_span_count(visible) == 3,
        "a pinned-to-plain drag counts across the pin boundary"
    );

    std::array<std::uint64_t, 3> covered{};
    const veyra::CellRangeCopy copy = control.covered_columns(visible, covered);
    require(
        copy.result == veyra::SelectionResult::handled && copy.count == 3 && covered[0] == 2 &&
            covered[2] == 3,
        "covered columns replay the pinned-first order with no pin knowledge"
    );
}

void check_clear_and_disabled() {
    auto control = veyra::CellRangeController{};
    require(
        control.clear() == veyra::SelectionResult::handled,
        "clearing an empty range reports no change"
    );

    std::array<std::uint64_t, 3> rows{10, 20, 30};
    std::array<std::uint64_t, 3> columns{1, 2, 3};
    control.set_anchor(10, 1);
    control.extend_to(20, 2, rows, columns);
    require(
        control.clear() == veyra::SelectionResult::changed && !control.has_range(),
        "clearing a live range drops it"
    );
    require(
        control.clear() == veyra::SelectionResult::handled,
        "clearing twice reports no change the second time"
    );

    control.set_anchor(10, 1);
    require(control.set_disabled(true), "disabling reports a state change");
    require(!control.set_disabled(true), "repeating the disable reports no change");
    require(
        control.set_anchor(20, 2) == veyra::SelectionResult::ignored &&
            control.extend_to(30, 3, rows, columns) == veyra::SelectionResult::ignored &&
            control.clear() == veyra::SelectionResult::ignored &&
            !control.set_mode(veyra::CellRangeMode::row),
        "every mutation is ignored while disabled, including mode switches"
    );
    require(
        control.has_range() && control.range().start_row == 10,
        "rejected mutations leave the stored anchor untouched"
    );
    require(control.contains(10, 1, 0, 0), "position queries still answer while disabled");

    require(control.set_disabled(false), "enabling reports a state change");
    require(
        control.extend_to(30, 3, rows, columns) == veyra::SelectionResult::changed,
        "mutation works again after enabling"
    );
    require(control.mode() == veyra::CellRangeMode::cell, "the mode survives the disable cycle");
}

}

int main() {
    try {
        check_normalize_both_directions();
        check_anchor_and_extend();
        check_unknown_ids_rejected();
        check_row_mode_semantics();
        check_column_mode_semantics();
        check_mode_switch_resets();
        check_covered_truncation();
        check_span_counts_on_odd_shapes();
        check_pinned_visible_order();
        check_clear_and_disabled();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
