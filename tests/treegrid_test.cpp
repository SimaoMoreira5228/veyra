#include "veyra/behaviors/data/treegrid.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

constexpr std::uint64_t kTreeColumn = 10;
constexpr std::uint64_t kSizeColumn = 20;
constexpr std::uint64_t kDateColumn = 30;

struct Fixture {
    bool expanded_root{true};
    bool expanded_archives{false};
    bool loaded_archives{false};

    std::size_t build(std::span<veyra::TreeCursorRow> output) const noexcept {
        auto count = std::size_t{};

        const auto push = [&](std::uint64_t id, std::uint32_t depth, bool kids, bool open) {
            if (count >= output.size()) {
                return;
            }

            output[count++] = veyra::TreeCursorRow{
                .id = id,
                .depth = depth,
                .has_children = kids,
                .expanded = open,
            };
        };

        push(1, 0, true, expanded_root);

        if (expanded_root) {
            push(2, 1, false, false);
            push(3, 1, true, expanded_archives);

            if (expanded_archives && loaded_archives) {
                push(4, 2, false, false);
                push(5, 2, false, false);
            }
        }

        push(6, 0, false, false);

        return count;
    }
};

struct Harness {
    Fixture model;
    veyra::DataGridController grid;
    std::array<veyra::TreeCursorRow, 8> window{};
    std::array<std::uint64_t, 8> scratch{};

    Harness() {
        const std::array<veyra::DataGridColumn, 3> columns{{
            {.id = kTreeColumn, .width = 160.0F, .min_width = 40.0F, .max_width = 400.0F},
            {.id = kSizeColumn,
             .width = 80.0F,
             .min_width = 40.0F,
             .max_width = 200.0F,
             .sortable = true},
            {.id = kDateColumn,
             .width = 120.0F,
             .min_width = 40.0F,
             .max_width = 240.0F,
             .sortable = true},
        }};

        require(
            grid.set_columns(columns) == veyra::DataGridResult::changed,
            "the tree grid columns install"
        );
    }

    std::span<const veyra::TreeCursorRow> rows() {
        return {window.data(), model.build(window)};
    }

    veyra::TreeGridCell move(veyra::DataGridMove direction, std::size_t page_step = 1) {
        return veyra::resolve_treegrid_move(
            grid,
            rows(),
            scratch,
            kTreeColumn,
            direction,
            page_step
        );
    }

    void place(std::uint64_t row, std::uint64_t column) {
        auto ids = std::array<std::uint64_t, 8>{};
        const auto built = rows();

        for (auto index = std::size_t{}; index < built.size(); ++index) {
            ids[index] = built[index].id;
        }

        require(
            grid.set_cursor(
                row,
                column,
                std::span<const std::uint64_t>{ids.data(), built.size()}
            ) != veyra::DataGridResult::ignored,
            "the cursor lands on a visible cell"
        );
    }
};

void check_roaming_keeps_column() {
    auto harness = Harness{};
    harness.place(2, kSizeColumn);

    auto step = harness.move(veyra::DataGridMove::down);
    require(
        step.result == veyra::TreeCursorResult::moved && step.row == 3 &&
            step.column == kSizeColumn,
        "down crosses into the collapsed subtree keeping the column"
    );

    step = harness.move(veyra::DataGridMove::down);
    require(
        step.result == veyra::TreeCursorResult::moved && step.row == 6 &&
            step.column == kSizeColumn,
        "down skips hidden descendants keeping the column"
    );

    step = harness.move(veyra::DataGridMove::down);
    require(
        step.result == veyra::TreeCursorResult::handled && step.row == 6,
        "down at the last row stays put"
    );

    step = harness.move(veyra::DataGridMove::up);
    require(
        step.result == veyra::TreeCursorResult::moved && step.row == 3,
        "up climbs back out of the last row"
    );
}

void check_tree_column_routes_to_tree() {
    auto harness = Harness{};
    harness.place(3, kTreeColumn);

    auto step = harness.move(veyra::DataGridMove::right);
    require(
        step.result == veyra::TreeCursorResult::expand && step.row == 3 &&
            step.column == kTreeColumn,
        "right on a collapsed node asks to expand"
    );

    harness.model.expanded_archives = true;
    step = harness.move(veyra::DataGridMove::right);
    require(
        step.result == veyra::TreeCursorResult::handled && step.row == 3,
        "right on an expanded but unloaded node stays put"
    );

    harness.model.loaded_archives = true;
    step = harness.move(veyra::DataGridMove::right);
    require(
        step.result == veyra::TreeCursorResult::moved && step.row == 4,
        "right on a loaded parent moves into its first child"
    );

    harness.place(2, kTreeColumn);
    step = harness.move(veyra::DataGridMove::right);
    require(
        step.result == veyra::TreeCursorResult::handled && step.row == 2,
        "right on a leaf without children stays put"
    );
}

void check_left_collapses_and_climbs() {
    auto harness = Harness{};
    harness.model.expanded_archives = true;
    harness.model.loaded_archives = true;
    harness.place(4, kTreeColumn);

    auto step = harness.move(veyra::DataGridMove::left);
    require(
        step.result == veyra::TreeCursorResult::moved && step.row == 3,
        "left on a nested child climbs to its parent"
    );

    step = harness.move(veyra::DataGridMove::left);
    require(
        step.result == veyra::TreeCursorResult::collapse && step.row == 3,
        "left on an expanded node asks to collapse"
    );

    harness.model.expanded_archives = false;
    harness.place(1, kTreeColumn);
    step = harness.move(veyra::DataGridMove::left);
    require(
        step.result == veyra::TreeCursorResult::collapse && step.row == 1,
        "left on the root collapses with a sibling subtree shut"
    );
}

void check_cells_ignore_tree() {
    auto harness = Harness{};
    harness.place(2, kSizeColumn);

    auto step = harness.move(veyra::DataGridMove::left);
    require(
        step.result == veyra::TreeCursorResult::moved && step.row == 2 &&
            step.column == kTreeColumn,
        "left in a value column moves into the tree column"
    );

    step = harness.move(veyra::DataGridMove::left);
    require(
        step.result == veyra::TreeCursorResult::moved && step.row == 1 &&
            step.column == kTreeColumn,
        "left on a leaf in the tree column climbs instead of collapsing"
    );

    harness.place(1, kTreeColumn);
    step = harness.move(veyra::DataGridMove::left);
    require(
        step.result == veyra::TreeCursorResult::collapse && step.row == 1,
        "left on the expanded root asks to collapse"
    );

    harness.place(2, kSizeColumn);
    step = harness.move(veyra::DataGridMove::right);
    require(
        step.result == veyra::TreeCursorResult::moved && step.row == 2 &&
            step.column == kDateColumn,
        "right in a value column moves cells again"
    );

    step = harness.move(veyra::DataGridMove::right);
    require(step.result == veyra::TreeCursorResult::handled, "right at the last column stays put");
}

void check_home_end_jump_rows() {
    auto harness = Harness{};
    harness.model.expanded_archives = true;
    harness.model.loaded_archives = true;
    harness.place(5, kSizeColumn);

    auto step = harness.move(veyra::DataGridMove::home);
    require(
        step.result == veyra::TreeCursorResult::moved && step.row == 1 &&
            step.column == kSizeColumn,
        "home jumps to the first visible row keeping the column"
    );

    step = harness.move(veyra::DataGridMove::home);
    require(
        step.result == veyra::TreeCursorResult::handled && step.row == 1,
        "home on the first row stays put"
    );

    step = harness.move(veyra::DataGridMove::end);
    require(
        step.result == veyra::TreeCursorResult::moved && step.row == 6 &&
            step.column == kSizeColumn,
        "end jumps to the last visible row keeping the column"
    );
}

void check_collapse_orphans_cursor() {
    auto harness = Harness{};
    harness.model.expanded_archives = true;
    harness.model.loaded_archives = true;
    harness.place(4, kSizeColumn);

    harness.model.expanded_archives = false;

    const auto stale = harness.move(veyra::DataGridMove::down);
    require(
        stale.result == veyra::TreeCursorResult::ignored,
        "a cursor on a collapsed-away row no longer moves"
    );

    auto ids = std::array<std::uint64_t, 8>{};
    const auto built = harness.rows();

    for (auto index = std::size_t{}; index < built.size(); ++index) {
        ids[index] = built[index].id;
    }

    require(
        harness.grid.set_cursor(
            4,
            kSizeColumn,
            std::span<const std::uint64_t>{ids.data(), built.size()}
        ) == veyra::DataGridResult::ignored,
        "a collapsed-away row refuses a fresh cursor"
    );
}

void check_home_end_reanchor_orphaned_cursor() {
    auto harness = Harness{};
    harness.model.expanded_archives = true;
    harness.model.loaded_archives = true;
    harness.place(4, kSizeColumn);

    harness.model.expanded_archives = false;

    const auto go_home = harness.move(veyra::DataGridMove::home);
    require(
        go_home.result == veyra::TreeCursorResult::moved && go_home.row == 1 &&
            go_home.column == kSizeColumn,
        "home on a collapsed-away row re-anchors to the first visible row"
    );

    harness.model.expanded_archives = true;
    harness.place(5, kSizeColumn);
    harness.model.expanded_archives = false;

    const auto go_end = harness.move(veyra::DataGridMove::end);
    require(
        go_end.result == veyra::TreeCursorResult::moved && go_end.row == 6 &&
            go_end.column == kSizeColumn,
        "end on a collapsed-away row re-anchors to the last visible row"
    );
}

void check_rejects_bad_inputs() {
    auto harness = Harness{};
    const auto empty = std::span<const veyra::TreeCursorRow>{};

    require(
        veyra::resolve_treegrid_move(
            harness.grid,
            empty,
            harness.scratch,
            kTreeColumn,
            veyra::DataGridMove::down,
            1
        )
                .result == veyra::TreeCursorResult::ignored,
        "an empty window never moves"
    );

    auto tiny = std::array<std::uint64_t, 1>{};
    harness.place(2, kSizeColumn);
    require(
        veyra::resolve_treegrid_move(
            harness.grid,
            harness.rows(),
            tiny,
            kTreeColumn,
            veyra::DataGridMove::down,
            1
        )
                .result == veyra::TreeCursorResult::ignored,
        "a scratch buffer smaller than the window is rejected"
    );

    auto fresh = Harness{};
    require(
        fresh.move(veyra::DataGridMove::down).result == veyra::TreeCursorResult::ignored,
        "moving without a cursor is ignored"
    );

    fresh.place(2, kSizeColumn);
    require(
        fresh.move(veyra::DataGridMove::page_down, 0).result == veyra::TreeCursorResult::ignored,
        "a zero page step is ignored"
    );
}

void check_tab_order_wraps() {
    auto harness = Harness{};
    harness.place(2, kDateColumn);

    auto step = harness.move(veyra::DataGridMove::next);
    require(
        step.result == veyra::TreeCursorResult::moved && step.row == 3 &&
            step.column == kTreeColumn,
        "tab past the last column drops to the next row's tree cell"
    );

    step = harness.move(veyra::DataGridMove::prev);
    require(
        step.result == veyra::TreeCursorResult::moved && step.row == 2 &&
            step.column == kDateColumn,
        "shift-tab climbs back to the previous row's last cell"
    );

    harness.place(2, kSizeColumn);
    step = harness.move(veyra::DataGridMove::page_down, 2);
    require(
        step.result == veyra::TreeCursorResult::moved && step.row == 6,
        "a page step lands on the window-relative row"
    );

    harness.place(6, kSizeColumn);
    step = harness.move(veyra::DataGridMove::page_up, 2);
    require(
        step.result == veyra::TreeCursorResult::moved && step.row == 2 &&
            step.column == kSizeColumn,
        "a page step upward lands on the window-relative row keeping the column"
    );
}

void check_sort_and_columns_coexist() {
    auto harness = Harness{};
    require(
        harness.grid.sort_by(kSizeColumn, veyra::SortDirection::descending) ==
            veyra::DataGridResult::changed,
        "a tree grid still sorts by a value column"
    );

    harness.place(2, kSizeColumn);
    const auto step = harness.move(veyra::DataGridMove::down);
    require(
        step.result == veyra::TreeCursorResult::moved && step.row == 3,
        "hierarchy roaming works under an active sort"
    );

    require(
        harness.grid.set_hidden(kDateColumn, true) == veyra::DataGridResult::changed,
        "hiding the last column succeeds"
    );

    const auto across = harness.move(veyra::DataGridMove::right);
    require(
        across.result == veyra::TreeCursorResult::handled,
        "a hidden column is not a move target"
    );

    auto header = veyra::SemanticNodeData{};
    require(
        harness.grid.write_header_semantics(kTreeColumn, "Name", header) &&
            header.role == veyra::SemanticRole::column_header,
        "the tree column still reports a column header"
    );
}

}

int main() {
    try {
        check_roaming_keeps_column();
        check_tree_column_routes_to_tree();
        check_left_collapses_and_climbs();
        check_cells_ignore_tree();
        check_home_end_jump_rows();
        check_collapse_orphans_cursor();
        check_home_end_reanchor_orphaned_cursor();
        check_rejects_bad_inputs();
        check_tab_order_wraps();
        check_sort_and_columns_coexist();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';

        return 1;
    }

    return 0;
}
