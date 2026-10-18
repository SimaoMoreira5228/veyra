#include "veyra/behaviors/datagrid.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace veyra {

CellCursor DataGridController::cursor() const noexcept {
    return {
        .has_cursor = has_cursor_,
        .row = has_cursor_ ? cursor_row_ : std::uint64_t{},
        .column = has_cursor_ ? cursor_column_ : std::uint64_t{},
    };
}

bool DataGridController::has_cursor() const noexcept {
    return has_cursor_;
}

DataGridResult DataGridController::set_cursor(
    std::uint64_t row,
    std::uint64_t column,
    std::span<const std::uint64_t> rows
) noexcept {
    const std::size_t index = find(column);

    if (index == kNotFound || columns_[index].hidden) {
        return DataGridResult::ignored;
    }

    bool known_row = false;

    for (std::uint64_t id : rows) {
        if (id == row) {
            known_row = true;
            break;
        }
    }

    if (!known_row) {
        return DataGridResult::ignored;
    }

    if (has_cursor_ && cursor_row_ == row && cursor_column_ == column) {
        return DataGridResult::handled;
    }

    if (edit_pending_) {
        cancel_pending();
        return DataGridResult::handled;
    }

    has_cursor_ = true;
    cursor_row_ = row;
    cursor_column_ = column;

    return DataGridResult::changed;
}

DataGridResult DataGridController::clear_cursor() noexcept {
    if (!has_cursor_) {
        return DataGridResult::handled;
    }

    has_cursor_ = false;
    cursor_row_ = 0;
    cursor_column_ = 0;
    cancel_pending();

    return DataGridResult::changed;
}

CellMove DataGridController::move_cursor(
    DataGridMove move,
    std::span<const std::uint64_t> rows,
    std::size_t page_step
) noexcept {
    if (!has_cursor_ || rows.empty()) {
        return {};
    }

    constexpr std::size_t missing = static_cast<std::size_t>(-1);
    std::size_t row_index = missing;

    for (std::size_t i = 0; i < rows.size(); ++i) {
        if (rows[i] == cursor_row_) {
            row_index = i;
            break;
        }
    }

    if (row_index == missing) {
        return {};
    }

    std::array<std::uint64_t, kCapacity> visible{};
    const std::size_t visible_size = collect_visible(visible);

    if (visible_size == 0) {
        return {};
    }

    std::size_t column_index = missing;

    for (std::size_t i = 0; i < visible_size; ++i) {
        if (visible[i] == cursor_column_) {
            column_index = i;
            break;
        }
    }

    if (column_index == missing) {
        return {};
    }

    const auto settled = [this](DataGridResult result) {
        return CellMove{
            .result = result,
            .row = cursor_row_,
            .column = cursor_column_,
        };
    };

    if (edit_pending_) {
        cancel_pending();
        return settled(DataGridResult::handled);
    }

    switch (move) {
    case DataGridMove::up:
        if (row_index == 0) {
            return settled(DataGridResult::handled);
        }

        cursor_row_ = rows[row_index - 1];

        return settled(DataGridResult::moved);
    case DataGridMove::down:
        if (row_index + 1 == rows.size()) {
            return settled(DataGridResult::handled);
        }

        cursor_row_ = rows[row_index + 1];

        return settled(DataGridResult::moved);
    case DataGridMove::left:
        if (column_index == 0) {
            return settled(DataGridResult::handled);
        }

        cursor_column_ = visible[column_index - 1];

        return settled(DataGridResult::moved);
    case DataGridMove::right:
        if (column_index + 1 == visible_size) {
            return settled(DataGridResult::handled);
        }

        cursor_column_ = visible[column_index + 1];

        return settled(DataGridResult::moved);
    case DataGridMove::home:
        if (column_index == 0) {
            return settled(DataGridResult::handled);
        }

        cursor_column_ = visible.front();

        return settled(DataGridResult::moved);
    case DataGridMove::end:
        if (column_index + 1 == visible_size) {
            return settled(DataGridResult::handled);
        }

        cursor_column_ = visible[visible_size - 1];

        return settled(DataGridResult::moved);
    case DataGridMove::page_up:
        if (page_step == 0) {
            return {};
        }

        if (row_index == 0) {
            return settled(DataGridResult::handled);
        }

        cursor_row_ = rows[row_index >= page_step ? row_index - page_step : 0];

        return settled(DataGridResult::moved);
    case DataGridMove::page_down:
        if (page_step == 0) {
            return {};
        }

        if (row_index + 1 == rows.size()) {
            return settled(DataGridResult::handled);
        }

        {
            std::size_t landed = row_index + page_step;

            if (landed >= rows.size()) {
                landed = rows.size() - 1;
            }

            cursor_row_ = rows[landed];
        }

        return settled(DataGridResult::moved);
    case DataGridMove::next:
        if (column_index + 1 < visible_size) {
            cursor_column_ = visible[column_index + 1];

            return settled(DataGridResult::moved);
        }

        if (row_index + 1 == rows.size()) {
            return settled(DataGridResult::handled);
        }

        cursor_row_ = rows[row_index + 1];
        cursor_column_ = visible.front();

        return settled(DataGridResult::moved);
    case DataGridMove::prev:
        if (column_index > 0) {
            cursor_column_ = visible[column_index - 1];

            return settled(DataGridResult::moved);
        }

        if (row_index == 0) {
            return settled(DataGridResult::handled);
        }

        cursor_row_ = rows[row_index - 1];
        cursor_column_ = visible[visible_size - 1];

        return settled(DataGridResult::moved);
    }

    return {};
}

CellEdit DataGridController::begin_edit(std::span<const std::uint64_t> rows) noexcept {
    if (!has_cursor_) {
        return {};
    }

    const std::size_t index = find(cursor_column_);

    if (index == kNotFound || columns_[index].hidden) {
        return {};
    }

    for (std::uint64_t id : rows) {
        if (id == cursor_row_) {
            if (edit_pending_ && edit_row_ == cursor_row_ && edit_column_ == cursor_column_) {
                return CellEdit{
                    .result = DataGridResult::handled,
                    .row = cursor_row_,
                    .column = cursor_column_,
                };
            }

            edit_pending_ = true;
            edit_row_ = cursor_row_;
            edit_column_ = cursor_column_;

            return CellEdit{
                .result = DataGridResult::edit,
                .row = cursor_row_,
                .column = cursor_column_,
            };
        }
    }

    return {};
}

CellEdit DataGridController::commit_edit() noexcept {
    if (!edit_pending_) {
        return {};
    }

    const CellEdit committed{
        .result = DataGridResult::changed,
        .row = edit_row_,
        .column = edit_column_,
    };

    cancel_pending();

    return committed;
}

DataGridResult DataGridController::cancel_edit() noexcept {
    cancel_pending();

    return DataGridResult::handled;
}

bool DataGridController::edit_pending() const noexcept {
    return edit_pending_;
}

CellEdit DataGridController::pending_edit() const noexcept {
    if (!edit_pending_) {
        return {};
    }

    return CellEdit{
        .result = DataGridResult::edit,
        .row = edit_row_,
        .column = edit_column_,
    };
}

bool DataGridController::edit_row_mode() const noexcept {
    return edit_row_mode_;
}

DataGridResult DataGridController::edit_row_mode(bool row_mode) noexcept {
    if (edit_row_mode_ == row_mode) {
        return DataGridResult::handled;
    }

    edit_row_mode_ = row_mode;

    return DataGridResult::changed;
}

void DataGridController::snap_cursor_to_visible() noexcept {
    const std::size_t hidden = find(cursor_column_);

    if (hidden == kNotFound) {
        return;
    }

    for (std::size_t i = hidden; i > 0; --i) {
        if (!columns_[i - 1].hidden) {
            cursor_column_ = columns_[i - 1].id;
            return;
        }
    }

    for (std::size_t i = hidden + 1; i < count_; ++i) {
        if (!columns_[i].hidden) {
            cursor_column_ = columns_[i].id;
            return;
        }
    }
}

void DataGridController::drop_edit_for_column(std::uint64_t id) noexcept {
    if (edit_pending_ && edit_column_ == id) {
        cancel_pending();
    }
}

void DataGridController::cancel_pending() noexcept {
    edit_pending_ = false;
    edit_row_ = 0;
    edit_column_ = 0;
}

}
