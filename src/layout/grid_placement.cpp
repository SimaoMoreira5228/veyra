#include "grid_placement.hpp"

#include <algorithm>
#include <cassert>
#include <limits>

namespace veyra::grid_detail {
namespace {

bool cell_is_occupied(
    std::span<const std::uint64_t> occupancy,
    std::size_t column_count,
    std::size_t row,
    std::size_t column
) noexcept {
    const auto cell = row * column_count + column;
    const auto word = cell / 64;
    const auto bit = cell % 64;
    return (occupancy[word] & (std::uint64_t{1} << bit)) != 0;
}

void occupy_cells(
    std::span<std::uint64_t> occupancy,
    std::size_t column_count,
    const GridItem& item,
    const GridItemPlacement& placement
) noexcept {
    for (std::size_t row = placement.row; row < placement.row + item.row_span; ++row) {
        for (std::size_t column = placement.column; column < placement.column + item.column_span;
             ++column) {
            const auto cell = row * column_count + column;
            const auto word = cell / 64;
            const auto bit = cell % 64;
            occupancy[word] |= std::uint64_t{1} << bit;
        }
    }
}

bool placement_fits(
    std::span<const std::uint64_t> occupancy,
    std::size_t column_count,
    const GridItem& item,
    std::size_t row,
    std::size_t column
) noexcept {
    for (std::size_t item_row = row; item_row < row + item.row_span; ++item_row) {
        for (std::size_t item_column = column; item_column < column + item.column_span;
             ++item_column) {
            if (cell_is_occupied(occupancy, column_count, item_row, item_column)) {
                return false;
            }
        }
    }
    return true;
}

}

bool has_automatic_placement(std::span<const GridItem> items) noexcept {
    for (const auto& item : items) {
        if (!item.row || !item.column) {
            return true;
        }
    }
    return false;
}

GridLayoutResult resolve_placements(
    std::span<const GridItem> items,
    std::size_t row_count,
    std::size_t column_count,
    std::span<GridItemPlacement> placements,
    std::span<std::uint64_t> occupancy
) noexcept {
    if (!has_automatic_placement(items)) {
        return GridLayoutResult::complete;
    }
    if (row_count != 0 && column_count > std::numeric_limits<std::size_t>::max() / row_count) {
        return GridLayoutResult::invalid_input;
    }

    const auto cell_count = row_count * column_count;
    const auto occupancy_words = cell_count / 64 + (cell_count % 64 == 0 ? 0 : 1);
    if (placements.size() < items.size() || occupancy.size() < occupancy_words) {
        return GridLayoutResult::insufficient_workspace;
    }
    if (cell_count == 0) {
        return GridLayoutResult::no_placement;
    }
    for (const auto& item : items) {
        if (item.row_span > row_count || item.column_span > column_count) {
            return GridLayoutResult::no_placement;
        }
    }

    std::fill_n(occupancy.begin(), occupancy_words, std::uint64_t{});
    for (std::size_t index = 0; index < items.size(); ++index) {
        const auto& item = items[index];
        if (!item.row || !item.column) {
            continue;
        }

        placements[index] = {.row = *item.row, .column = *item.column};
        occupy_cells(occupancy, column_count, item, placements[index]);
    }

    std::size_t automatic_cursor = 0;
    std::size_t automatic_row_cursor = 0;
    for (std::size_t index = 0; index < items.size(); ++index) {
        const auto& item = items[index];
        if (item.row && item.column) {
            continue;
        }

        bool found = false;
        if (item.row && !item.column) {
            const auto last_column = column_count - item.column_span;
            for (std::size_t column = 0; column <= last_column; ++column) {
                if (!placement_fits(occupancy, column_count, item, *item.row, column)) {
                    continue;
                }
                placements[index] = {.row = *item.row, .column = column};
                found = true;
                break;
            }
        } else if (!item.row && item.column) {
            const auto last_row = row_count - item.row_span;
            for (std::size_t row = automatic_row_cursor; row <= last_row; ++row) {
                if (!placement_fits(occupancy, column_count, item, row, *item.column)) {
                    continue;
                }
                placements[index] = {.row = row, .column = *item.column};
                automatic_row_cursor = row + item.row_span;
                found = true;
                break;
            }
        } else {
            for (auto cell = automatic_cursor; cell < cell_count; ++cell) {
                const auto row = cell / column_count;
                const auto column = cell % column_count;
                if (item.row_span > row_count - row || item.column_span > column_count - column ||
                    !placement_fits(occupancy, column_count, item, row, column)) {
                    continue;
                }
                placements[index] = {.row = row, .column = column};
                automatic_cursor = cell + 1;
                found = true;
                break;
            }
        }

        if (!found) {
            return GridLayoutResult::no_placement;
        }
        occupy_cells(occupancy, column_count, item, placements[index]);
    }

    return GridLayoutResult::complete;
}

GridItemPlacement item_placement(
    const GridItem& item,
    std::size_t index,
    std::span<const GridItemPlacement> placements
) noexcept {
    if (!placements.empty()) {
        return placements[index];
    }
    assert(item.row.has_value() && item.column.has_value());
    return {.row = *item.row, .column = *item.column};
}

}
