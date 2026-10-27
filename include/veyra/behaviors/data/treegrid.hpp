#ifndef VEYRA_BEHAVIORS_TREEGRID_HPP
#define VEYRA_BEHAVIORS_TREEGRID_HPP

#include "veyra/behaviors/data/datagrid.hpp"
#include "veyra/behaviors/selection/tree_cursor.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace veyra {

struct TreeGridCell {
    TreeCursorResult result{TreeCursorResult::ignored};
    std::uint64_t row{};
    std::uint64_t column{};
};

inline TreeGridCell resolve_treegrid_move(
    DataGridController& grid,
    std::span<const TreeCursorRow> rows,
    std::span<std::uint64_t> scratch,
    std::uint64_t tree_column,
    DataGridMove move,
    std::size_t page_step
) noexcept {
    if (rows.empty() || scratch.size() < rows.size()) {
        return {};
    }

    const auto cursor = grid.cursor();

    if (!cursor.has_cursor) {
        return {};
    }

    if ((move == DataGridMove::left || move == DataGridMove::right) &&
        cursor.column == tree_column) {
        for (auto index = std::size_t{}; index < rows.size(); ++index) {
            scratch[index] = rows[index].id;
        }

        const auto ids = std::span<const std::uint64_t>{scratch.data(), rows.size()};
        const auto tree_move =
            move == DataGridMove::left ? TreeCursorMove::left : TreeCursorMove::right;
        const auto outcome = resolve_tree_cursor(rows, cursor.row, tree_move);

        if (outcome.result == TreeCursorResult::moved) {
            const auto placed = grid.set_cursor(outcome.id, cursor.column, ids);

            if (placed == DataGridResult::ignored) {
                return {};
            }

            return TreeGridCell{
                .result = TreeCursorResult::moved,
                .row = outcome.id,
                .column = cursor.column,
            };
        }

        return TreeGridCell{
            .result = outcome.result,
            .row = outcome.id,
            .column = cursor.column,
        };
    }

    for (auto index = std::size_t{}; index < rows.size(); ++index) {
        scratch[index] = rows[index].id;
    }

    const auto ids = std::span<const std::uint64_t>{scratch.data(), rows.size()};

    if (move == DataGridMove::home || move == DataGridMove::end) {
        const auto edge = move == DataGridMove::home ? rows.front().id : rows.back().id;
        const auto placed = grid.set_cursor(edge, cursor.column, ids);

        if (placed == DataGridResult::changed) {
            return TreeGridCell{
                .result = TreeCursorResult::moved,
                .row = edge,
                .column = cursor.column,
            };
        }

        if (placed == DataGridResult::handled) {
            return TreeGridCell{
                .result = TreeCursorResult::handled,
                .row = cursor.row,
                .column = cursor.column,
            };
        }

        return {};
    }

    const auto moved = grid.move_cursor(move, ids, page_step);

    if (moved.result == DataGridResult::moved) {
        return TreeGridCell{
            .result = TreeCursorResult::moved,
            .row = moved.row,
            .column = moved.column,
        };
    }

    if (moved.result == DataGridResult::handled) {
        return TreeGridCell{
            .result = TreeCursorResult::handled,
            .row = moved.row,
            .column = moved.column,
        };
    }

    return {};
}

}

#endif
