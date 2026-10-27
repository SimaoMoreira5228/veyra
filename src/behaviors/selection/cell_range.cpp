#include "veyra/behaviors/selection/cell_range.hpp"

namespace veyra {

bool CellRangeController::has_range() const noexcept {
    return has_range_;
}

CellRangeMode CellRangeController::mode() const noexcept {
    return mode_;
}

bool CellRangeController::disabled() const noexcept {
    return disabled_;
}

CellRange CellRangeController::range() const noexcept {
    return {
        .start_row = anchor_row_,
        .start_column = anchor_column_,
        .end_row = corner_row_,
        .end_column = corner_column_,
    };
}

bool CellRangeController::set_mode(CellRangeMode mode) noexcept {
    if (disabled_) {
        return false;
    }

    if (mode_ == mode) {
        return false;
    }

    mode_ = mode;
    has_range_ = false;
    anchor_row_ = 0;
    anchor_column_ = 0;
    corner_row_ = 0;
    corner_column_ = 0;
    anchor_row_pos_ = kNotFound;
    corner_row_pos_ = kNotFound;
    anchor_column_pos_ = kNotFound;
    corner_column_pos_ = kNotFound;

    return true;
}

bool CellRangeController::set_disabled(bool disabled) noexcept {
    if (disabled_ == disabled) {
        return false;
    }

    disabled_ = disabled;

    return true;
}

SelectionResult CellRangeController::set_anchor(std::uint64_t row, std::uint64_t column) noexcept {
    if (disabled_) {
        return SelectionResult::ignored;
    }

    if (has_range_ && anchor_row_ == row && anchor_column_ == column && corner_row_ == row &&
        corner_column_ == column) {
        return SelectionResult::handled;
    }

    anchor_row_ = row;
    anchor_column_ = column;
    corner_row_ = row;
    corner_column_ = column;
    anchor_row_pos_ = kNotFound;
    corner_row_pos_ = kNotFound;
    anchor_column_pos_ = kNotFound;
    corner_column_pos_ = kNotFound;
    has_range_ = true;

    return SelectionResult::changed;
}

SelectionResult CellRangeController::extend_to(
    std::uint64_t row,
    std::uint64_t column,
    std::span<const std::uint64_t> rows,
    std::span<const std::uint64_t> columns
) noexcept {
    if (disabled_) {
        return SelectionResult::ignored;
    }

    if (!has_range_) {
        return SelectionResult::ignored;
    }

    if (mode_ == CellRangeMode::row) {
        const std::size_t anchor_pos = find_in(rows, anchor_row_);
        const std::size_t corner_pos = find_in(rows, row);

        if (anchor_pos == kNotFound || corner_pos == kNotFound) {
            return SelectionResult::ignored;
        }

        if (corner_row_ == row) {
            return SelectionResult::handled;
        }

        corner_row_ = row;
        corner_column_ = anchor_column_;
        anchor_row_pos_ = anchor_pos;
        corner_row_pos_ = corner_pos;

        return SelectionResult::changed;
    }

    if (mode_ == CellRangeMode::column) {
        const std::size_t anchor_pos = find_in(columns, anchor_column_);
        const std::size_t corner_pos = find_in(columns, column);

        if (anchor_pos == kNotFound || corner_pos == kNotFound) {
            return SelectionResult::ignored;
        }

        if (corner_column_ == column) {
            return SelectionResult::handled;
        }

        corner_row_ = anchor_row_;
        corner_column_ = column;
        anchor_column_pos_ = anchor_pos;
        corner_column_pos_ = corner_pos;

        return SelectionResult::changed;
    }

    const std::size_t anchor_row_pos = find_in(rows, anchor_row_);
    const std::size_t corner_row_pos = find_in(rows, row);
    const std::size_t anchor_column_pos = find_in(columns, anchor_column_);
    const std::size_t corner_column_pos = find_in(columns, column);

    if (anchor_row_pos == kNotFound || corner_row_pos == kNotFound ||
        anchor_column_pos == kNotFound || corner_column_pos == kNotFound) {
        return SelectionResult::ignored;
    }

    if (corner_row_ == row && corner_column_ == column) {
        return SelectionResult::handled;
    }

    corner_row_ = row;
    corner_column_ = column;
    anchor_row_pos_ = anchor_row_pos;
    corner_row_pos_ = corner_row_pos;
    anchor_column_pos_ = anchor_column_pos;
    corner_column_pos_ = corner_column_pos;

    return SelectionResult::changed;
}

SelectionResult CellRangeController::clear() noexcept {
    if (disabled_) {
        return SelectionResult::ignored;
    }

    if (!has_range_) {
        return SelectionResult::handled;
    }

    has_range_ = false;
    anchor_row_ = 0;
    anchor_column_ = 0;
    corner_row_ = 0;
    corner_column_ = 0;
    anchor_row_pos_ = kNotFound;
    corner_row_pos_ = kNotFound;
    anchor_column_pos_ = kNotFound;
    corner_column_pos_ = kNotFound;

    return SelectionResult::changed;
}

bool CellRangeController::contains(
    std::uint64_t row,
    std::uint64_t column,
    std::size_t row_order,
    std::size_t column_order
) const noexcept {
    if (!has_range_) {
        return false;
    }

    if (mode_ == CellRangeMode::row) {
        if (anchor_row_pos_ == kNotFound || corner_row_pos_ == kNotFound) {
            return row == anchor_row_;
        }

        const CellRangeOrder bounds = normalize_cell_range(anchor_row_pos_, corner_row_pos_, 0, 0);

        return row_order >= bounds.first_row && row_order <= bounds.last_row;
    }

    if (mode_ == CellRangeMode::column) {
        if (anchor_column_pos_ == kNotFound || corner_column_pos_ == kNotFound) {
            return column == anchor_column_;
        }

        const CellRangeOrder bounds =
            normalize_cell_range(0, 0, anchor_column_pos_, corner_column_pos_);

        return column_order >= bounds.first_column && column_order <= bounds.last_column;
    }

    if (anchor_row_pos_ == kNotFound || corner_row_pos_ == kNotFound ||
        anchor_column_pos_ == kNotFound || corner_column_pos_ == kNotFound) {
        return row == anchor_row_ && column == anchor_column_;
    }

    const CellRangeOrder bounds = normalize_cell_range(
        anchor_row_pos_,
        corner_row_pos_,
        anchor_column_pos_,
        corner_column_pos_
    );

    return row_order >= bounds.first_row && row_order <= bounds.last_row &&
        column_order >= bounds.first_column && column_order <= bounds.last_column;
}

std::size_t
CellRangeController::row_span_count(std::span<const std::uint64_t> rows) const noexcept {
    if (!has_range_) {
        return 0;
    }

    if (mode_ == CellRangeMode::column) {
        return unique_count(rows);
    }

    const std::size_t anchor_pos = find_in(rows, anchor_row_);
    const std::size_t corner_pos = find_in(rows, corner_row_);

    if (anchor_pos == kNotFound || corner_pos == kNotFound) {
        return 0;
    }

    const CellRangeOrder bounds = normalize_cell_range(anchor_pos, corner_pos, 0, 0);

    return bounds.last_row - bounds.first_row + 1;
}

std::size_t
CellRangeController::column_span_count(std::span<const std::uint64_t> columns) const noexcept {
    if (!has_range_) {
        return 0;
    }

    if (mode_ == CellRangeMode::row) {
        return unique_count(columns);
    }

    const std::size_t anchor_pos = find_in(columns, anchor_column_);
    const std::size_t corner_pos = find_in(columns, corner_column_);

    if (anchor_pos == kNotFound || corner_pos == kNotFound) {
        return 0;
    }

    const CellRangeOrder bounds = normalize_cell_range(0, 0, anchor_pos, corner_pos);

    return bounds.last_column - bounds.first_column + 1;
}

CellRangeCopy CellRangeController::covered_rows(
    std::span<const std::uint64_t> rows,
    std::span<std::uint64_t> output
) const noexcept {
    if (!has_range_) {
        return {.result = SelectionResult::ignored, .count = 0};
    }

    if (mode_ == CellRangeMode::column) {
        return copy_unique(rows, output);
    }

    const std::size_t anchor_pos = find_in(rows, anchor_row_);
    const std::size_t corner_pos = find_in(rows, corner_row_);

    if (anchor_pos == kNotFound || corner_pos == kNotFound) {
        return {.result = SelectionResult::ignored, .count = 0};
    }

    const CellRangeOrder bounds = normalize_cell_range(anchor_pos, corner_pos, 0, 0);

    return copy_slice(rows, bounds.first_row, bounds.last_row, output);
}

CellRangeCopy CellRangeController::covered_columns(
    std::span<const std::uint64_t> columns,
    std::span<std::uint64_t> output
) const noexcept {
    if (!has_range_) {
        return {.result = SelectionResult::ignored, .count = 0};
    }

    if (mode_ == CellRangeMode::row) {
        return copy_unique(columns, output);
    }

    const std::size_t anchor_pos = find_in(columns, anchor_column_);
    const std::size_t corner_pos = find_in(columns, corner_column_);

    if (anchor_pos == kNotFound || corner_pos == kNotFound) {
        return {.result = SelectionResult::ignored, .count = 0};
    }

    const CellRangeOrder bounds = normalize_cell_range(0, 0, anchor_pos, corner_pos);

    return copy_slice(columns, bounds.first_column, bounds.last_column, output);
}

std::size_t
CellRangeController::find_in(std::span<const std::uint64_t> span, std::uint64_t id) noexcept {
    for (std::size_t i = 0; i < span.size(); ++i) {
        if (span[i] == id) {
            return i;
        }
    }

    return kNotFound;
}

std::size_t CellRangeController::unique_count(std::span<const std::uint64_t> span) noexcept {
    std::size_t unique = 0;

    for (std::size_t i = 0; i < span.size(); ++i) {
        bool duplicate = false;

        for (std::size_t j = 0; j < i; ++j) {
            if (span[j] == span[i]) {
                duplicate = true;
                break;
            }
        }

        if (!duplicate) {
            ++unique;
        }
    }

    return unique;
}

CellRangeCopy CellRangeController::copy_unique(
    std::span<const std::uint64_t> span,
    std::span<std::uint64_t> output
) noexcept {
    const std::size_t total = unique_count(span);
    std::size_t copied = 0;

    for (std::size_t i = 0; i < span.size() && copied < output.size(); ++i) {
        bool duplicate = false;

        for (std::size_t j = 0; j < copied; ++j) {
            if (output[j] == span[i]) {
                duplicate = true;
                break;
            }
        }

        if (duplicate) {
            continue;
        }

        output[copied] = span[i];
        ++copied;
    }

    if (total > output.size()) {
        return {.result = SelectionResult::overflow, .count = copied};
    }

    return {.result = SelectionResult::handled, .count = copied};
}

CellRangeCopy CellRangeController::copy_slice(
    std::span<const std::uint64_t> span,
    std::size_t first,
    std::size_t last,
    std::span<std::uint64_t> output
) noexcept {
    const std::size_t total = last - first + 1;
    std::size_t copied = total;

    if (copied > output.size()) {
        copied = output.size();
    }

    for (std::size_t i = 0; i < copied; ++i) {
        output[i] = span[first + i];
    }

    if (total > output.size()) {
        return {.result = SelectionResult::overflow, .count = copied};
    }

    return {.result = SelectionResult::handled, .count = copied};
}

}
