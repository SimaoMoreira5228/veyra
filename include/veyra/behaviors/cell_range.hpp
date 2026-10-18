#ifndef VEYRA_BEHAVIORS_CELL_RANGE_HPP
#define VEYRA_BEHAVIORS_CELL_RANGE_HPP

#include "veyra/behaviors/selection.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace veyra {

enum class CellRangeMode : std::uint8_t {
    cell,
    row,
    column,
};

struct CellRange {
    std::uint64_t start_row{};
    std::uint64_t start_column{};
    std::uint64_t end_row{};
    std::uint64_t end_column{};
};

struct CellRangeOrder {
    std::size_t first_row{};
    std::size_t last_row{};
    std::size_t first_column{};
    std::size_t last_column{};
};

inline constexpr CellRangeOrder normalize_cell_range(
    std::size_t anchor_row,
    std::size_t corner_row,
    std::size_t anchor_column,
    std::size_t corner_column
) noexcept {
    CellRangeOrder bounds{};

    if (anchor_row < corner_row) {
        bounds.first_row = anchor_row;
        bounds.last_row = corner_row;
    } else {
        bounds.first_row = corner_row;
        bounds.last_row = anchor_row;
    }

    if (anchor_column < corner_column) {
        bounds.first_column = anchor_column;
        bounds.last_column = corner_column;
    } else {
        bounds.first_column = corner_column;
        bounds.last_column = anchor_column;
    }

    return bounds;
}

struct CellRangeCopy {
    SelectionResult result{SelectionResult::ignored};
    std::size_t count{};
};

class CellRangeController {
  public:
    bool has_range() const noexcept;
    CellRangeMode mode() const noexcept;
    bool disabled() const noexcept;
    CellRange range() const noexcept;

    bool set_mode(CellRangeMode mode) noexcept;
    bool set_disabled(bool disabled) noexcept;

    SelectionResult set_anchor(std::uint64_t row, std::uint64_t column) noexcept;
    SelectionResult extend_to(
        std::uint64_t row,
        std::uint64_t column,
        std::span<const std::uint64_t> rows,
        std::span<const std::uint64_t> columns
    ) noexcept;
    SelectionResult clear() noexcept;

    // NOTE: position arguments are caller-mapped orders from the visible spans; cached
    // anchor/corner positions refresh on extend_to, so callers must re-extend after any
    // row/column reorder before position queries stay valid.
    bool contains(
        std::uint64_t row,
        std::uint64_t column,
        std::size_t row_order,
        std::size_t column_order
    ) const noexcept;
    std::size_t row_span_count(std::span<const std::uint64_t> rows) const noexcept;
    std::size_t column_span_count(std::span<const std::uint64_t> columns) const noexcept;
    CellRangeCopy covered_rows(
        std::span<const std::uint64_t> rows,
        std::span<std::uint64_t> output
    ) const noexcept;
    CellRangeCopy covered_columns(
        std::span<const std::uint64_t> columns,
        std::span<std::uint64_t> output
    ) const noexcept;

  private:
    static constexpr std::size_t kNotFound = static_cast<std::size_t>(-1);

    static std::size_t find_in(std::span<const std::uint64_t> span, std::uint64_t id) noexcept;
    static std::size_t unique_count(std::span<const std::uint64_t> span) noexcept;
    static CellRangeCopy
    copy_unique(std::span<const std::uint64_t> span, std::span<std::uint64_t> output) noexcept;
    static CellRangeCopy copy_slice(
        std::span<const std::uint64_t> span,
        std::size_t first,
        std::size_t last,
        std::span<std::uint64_t> output
    ) noexcept;

    std::uint64_t anchor_row_{};
    std::uint64_t anchor_column_{};
    std::uint64_t corner_row_{};
    std::uint64_t corner_column_{};
    std::size_t anchor_row_pos_{kNotFound};
    std::size_t corner_row_pos_{kNotFound};
    std::size_t anchor_column_pos_{kNotFound};
    std::size_t corner_column_pos_{kNotFound};
    CellRangeMode mode_{CellRangeMode::cell};
    bool has_range_{};
    bool disabled_{};
};

}

#endif
