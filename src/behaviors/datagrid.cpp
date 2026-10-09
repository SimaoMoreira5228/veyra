#include "veyra/behaviors/datagrid.hpp"

#include <cmath>

namespace veyra {

std::size_t DataGridController::column_count() const noexcept {
    return count_;
}

std::size_t DataGridController::visible_count() const noexcept {
    std::size_t visible = 0;

    for (std::size_t i = 0; i < count_; ++i) {
        if (!columns_[i].hidden) {
            ++visible;
        }
    }

    return visible;
}

bool DataGridController::contains_column(std::uint64_t id) const noexcept {
    return find(id) != kNotFound;
}

bool DataGridController::column_hidden(std::uint64_t id) const noexcept {
    const std::size_t index = find(id);

    if (index == kNotFound) {
        return false;
    }

    return columns_[index].hidden;
}

float DataGridController::column_width(std::uint64_t id) const noexcept {
    const std::size_t index = find(id);

    if (index == kNotFound) {
        return 0.0F;
    }

    return columns_[index].width;
}

std::size_t DataGridController::copy_columns(std::span<DataGridColumn> output) const noexcept {
    std::size_t copied = count_;

    if (copied > output.size()) {
        copied = output.size();
    }

    for (std::size_t i = 0; i < copied; ++i) {
        output[i] = columns_[i];
    }

    return copied;
}

std::size_t
DataGridController::copy_visible_columns(std::span<std::uint64_t> output) const noexcept {
    return collect_visible(output);
}

DataGridResult DataGridController::set_columns(std::span<const DataGridColumn> columns) noexcept {
    if (columns.size() > kCapacity) {
        return DataGridResult::overflow;
    }

    for (const DataGridColumn& column : columns) {
        if (!valid_spec(column)) {
            return DataGridResult::ignored;
        }
    }

    for (std::size_t i = 0; i < columns.size(); ++i) {
        for (std::size_t j = i + 1; j < columns.size(); ++j) {
            if (columns[i].id == columns[j].id) {
                return DataGridResult::ignored;
            }
        }
    }

    bool identical = count_ == columns.size();

    if (identical) {
        for (std::size_t i = 0; i < count_; ++i) {
            const DataGridColumn& before = columns_[i];
            const DataGridColumn& after = columns[i];

            if (before.id != after.id || before.width != after.width ||
                before.min_width != after.min_width || before.max_width != after.max_width ||
                before.sortable != after.sortable || before.hidden != after.hidden) {
                identical = false;
                break;
            }
        }
    }

    if (identical) {
        return DataGridResult::handled;
    }

    for (std::size_t i = 0; i < columns.size(); ++i) {
        const float clamped =
            clamp_width(columns[i].width, columns[i].min_width, columns[i].max_width);

        columns_[i] = columns[i];
        columns_[i].width = clamped;
    }
    count_ = columns.size();

    if (has_cursor_) {
        const std::size_t cursor = find(cursor_column_);

        if (cursor == kNotFound || columns_[cursor].hidden) {
            has_cursor_ = false;
            cursor_row_ = 0;
            cursor_column_ = 0;
        }
    }

    if (has_sort_) {
        const std::size_t sorted = find(sort_column_);

        if (sorted == kNotFound || !columns_[sorted].sortable) {
            has_sort_ = false;
            sort_column_ = 0;
        }
    }

    return DataGridResult::changed;
}

DataGridResult DataGridController::resize_column(std::uint64_t id, float width) noexcept {
    const std::size_t index = find(id);

    if (index == kNotFound) {
        return DataGridResult::ignored;
    }

    if (!std::isfinite(width)) {
        return DataGridResult::ignored;
    }

    const float clamped = clamp_width(width, columns_[index].min_width, columns_[index].max_width);

    if (columns_[index].width == clamped) {
        return DataGridResult::handled;
    }

    columns_[index].width = clamped;

    return DataGridResult::changed;
}

DataGridResult
DataGridController::move_column(std::uint64_t id, std::uint64_t target, ColumnEdge edge) noexcept {
    const std::size_t from = find(id);
    const std::size_t onto = find(target);

    if (from == kNotFound || onto == kNotFound) {
        return DataGridResult::ignored;
    }

    if (from == onto) {
        return DataGridResult::handled;
    }

    std::array<DataGridColumn, kCapacity> order{};

    for (std::size_t i = 0; i < count_; ++i) {
        order[i] = columns_[i];
    }

    const DataGridColumn moving = order[from];

    for (std::size_t i = from; i + 1 < count_; ++i) {
        order[i] = order[i + 1];
    }

    std::size_t insert = onto;

    if (onto > from) {
        --insert;
    }

    if (edge == ColumnEdge::after) {
        ++insert;
    }

    for (std::size_t i = count_ - 1; i > insert; --i) {
        order[i] = order[i - 1];
    }
    order[insert] = moving;

    bool identical = true;

    for (std::size_t i = 0; i < count_; ++i) {
        if (order[i].id != columns_[i].id) {
            identical = false;
            break;
        }
    }

    if (identical) {
        return DataGridResult::handled;
    }

    for (std::size_t i = 0; i < count_; ++i) {
        columns_[i] = order[i];
    }

    return DataGridResult::changed;
}

DataGridResult DataGridController::set_hidden(std::uint64_t id, bool hidden) noexcept {
    const std::size_t index = find(id);

    if (index == kNotFound) {
        return DataGridResult::ignored;
    }

    if (columns_[index].hidden == hidden) {
        return DataGridResult::handled;
    }

    columns_[index].hidden = hidden;

    if (hidden && has_cursor_ && cursor_column_ == id) {
        snap_cursor_to_visible();
    }

    return DataGridResult::changed;
}

SortSpec DataGridController::sort_spec() const noexcept {
    return {
        .has_sort = has_sort_,
        .column = has_sort_ ? sort_column_ : std::uint64_t{},
        .direction = sort_direction_,
    };
}

bool DataGridController::has_sort() const noexcept {
    return has_sort_;
}

DataGridResult DataGridController::sort_by(std::uint64_t column, SortDirection direction) noexcept {
    const std::size_t index = find(column);

    if (index == kNotFound || !columns_[index].sortable) {
        return DataGridResult::ignored;
    }

    if (has_sort_ && sort_column_ == column && sort_direction_ == direction) {
        return DataGridResult::handled;
    }

    has_sort_ = true;
    sort_column_ = column;
    sort_direction_ = direction;

    return DataGridResult::changed;
}

DataGridResult DataGridController::clear_sort() noexcept {
    if (!has_sort_) {
        return DataGridResult::handled;
    }

    has_sort_ = false;
    sort_column_ = 0;

    return DataGridResult::changed;
}

bool DataGridController::write_header_semantics(
    std::uint64_t column,
    std::string_view accessible_name,
    SemanticNodeData& output
) const noexcept {
    if (accessible_name.empty()) {
        return false;
    }

    if (find(column) == kNotFound) {
        return false;
    }

    std::uint16_t states{};

    if (has_sort_ && sort_column_ == column) {
        if (sort_direction_ == SortDirection::ascending) {
            states = states | SemanticState::sorted_ascending;
        } else {
            states = states | SemanticState::sorted_descending;
        }
    }

    output = {
        .role = SemanticRole::column_header,
        .label = accessible_name,
        .states = states,
        .actions = std::uint16_t{},
    };

    return true;
}

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
            return CellEdit{
                .result = DataGridResult::edit,
                .row = cursor_row_,
                .column = cursor_column_,
            };
        }
    }

    return {};
}

std::size_t DataGridController::find(std::uint64_t id) const noexcept {
    for (std::size_t i = 0; i < count_; ++i) {
        if (columns_[i].id == id) {
            return i;
        }
    }

    return kNotFound;
}

std::size_t DataGridController::collect_visible(std::span<std::uint64_t> output) const noexcept {
    std::size_t copied = 0;

    for (std::size_t i = 0; i < count_ && copied < output.size(); ++i) {
        if (!columns_[i].hidden) {
            output[copied] = columns_[i].id;
            ++copied;
        }
    }

    return copied;
}

bool DataGridController::valid_spec(const DataGridColumn& column) noexcept {
    if (column.id == 0) {
        return false;
    }

    if (!std::isfinite(column.width) || !std::isfinite(column.min_width) ||
        !std::isfinite(column.max_width)) {
        return false;
    }

    if (column.min_width < 0.0F || column.min_width > column.max_width) {
        return false;
    }

    return true;
}

float DataGridController::clamp_width(float width, float low, float high) noexcept {
    if (width < low) {
        return low;
    }

    if (width > high) {
        return high;
    }

    return width;
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

}
