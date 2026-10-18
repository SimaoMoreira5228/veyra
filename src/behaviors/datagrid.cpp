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

DataGridPin DataGridController::column_pin(std::uint64_t id) const noexcept {
    const std::size_t index = find(id);

    if (index == kNotFound) {
        return DataGridPin::none;
    }

    return columns_[index].pinned;
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
            if (!same_spec(columns_[i], columns[i])) {
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

    if (edit_pending_) {
        const std::size_t edited = find(edit_column_);

        if (edited == kNotFound || columns_[edited].hidden) {
            cancel_pending();
        }
    }

    prune_sorts();

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

    if (columns_[from].locked_position || columns_[onto].locked_position) {
        return DataGridResult::ignored;
    }

    if (columns_[from].pinned != columns_[onto].pinned) {
        return DataGridResult::ignored;
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

    if (hidden) {
        drop_edit_for_column(id);
    }

    return DataGridResult::changed;
}

DataGridResult DataGridController::set_pinned(std::uint64_t id, DataGridPin pin) noexcept {
    const std::size_t index = find(id);

    if (index == kNotFound) {
        return DataGridResult::ignored;
    }

    if (columns_[index].pinned == pin) {
        return DataGridResult::handled;
    }

    columns_[index].pinned = pin;

    return DataGridResult::changed;
}

std::size_t
DataGridController::copy_customization(std::span<ColumnCustomization> output) const noexcept {
    std::size_t copied = count_;

    if (copied > output.size()) {
        copied = output.size();
    }

    for (std::size_t i = 0; i < copied; ++i) {
        output[i] = ColumnCustomization{
            .id = columns_[i].id,
            .width = columns_[i].width,
            .hidden = columns_[i].hidden,
            .pinned = columns_[i].pinned,
            .position = static_cast<std::uint32_t>(i),
        };
    }

    return copied;
}

DataGridResult
DataGridController::apply_customization(std::span<const ColumnCustomization> custom) noexcept {
    bool known = false;

    for (const ColumnCustomization& entry : custom) {
        if (find(entry.id) != kNotFound) {
            known = true;
            break;
        }
    }

    if (!known) {
        return DataGridResult::ignored;
    }

    bool changed = false;

    for (const ColumnCustomization& entry : custom) {
        const std::size_t index = find(entry.id);

        if (index == kNotFound) {
            continue;
        }

        if (std::isfinite(entry.width)) {
            const float clamped =
                clamp_width(entry.width, columns_[index].min_width, columns_[index].max_width);

            if (columns_[index].width != clamped) {
                columns_[index].width = clamped;
                changed = true;
            }
        }

        if (columns_[index].hidden != entry.hidden) {
            columns_[index].hidden = entry.hidden;
            changed = true;
        }

        if (columns_[index].pinned != entry.pinned) {
            columns_[index].pinned = entry.pinned;
            changed = true;
        }
    }

    std::array<std::uint64_t, kCapacity> ranked_ids{};
    std::array<std::uint32_t, kCapacity> ranked_pos{};
    std::size_t ranked_count = 0;

    for (const ColumnCustomization& entry : custom) {
        if (find(entry.id) == kNotFound) {
            continue;
        }

        bool duplicate = false;

        for (std::size_t i = 0; i < ranked_count; ++i) {
            if (ranked_ids[i] == entry.id) {
                duplicate = true;
                break;
            }
        }

        if (!duplicate) {
            ranked_ids[ranked_count] = entry.id;
            ranked_pos[ranked_count] = entry.position;
            ++ranked_count;
        }
    }

    for (std::size_t i = 1; i < ranked_count; ++i) {
        const std::uint64_t key_id = ranked_ids[i];
        const std::uint32_t key_pos = ranked_pos[i];
        std::size_t j = i;

        while (j > 0 && ranked_pos[j - 1] > key_pos) {
            ranked_ids[j] = ranked_ids[j - 1];
            ranked_pos[j] = ranked_pos[j - 1];
            --j;
        }

        ranked_ids[j] = key_id;
        ranked_pos[j] = key_pos;
    }

    std::array<DataGridColumn, kCapacity> next{};
    std::size_t placed = 0;

    for (std::size_t i = 0; i < ranked_count; ++i) {
        next[placed] = columns_[find(ranked_ids[i])];
        ++placed;
    }

    for (std::size_t i = 0; i < count_; ++i) {
        bool mentioned = false;

        for (std::size_t j = 0; j < ranked_count; ++j) {
            if (ranked_ids[j] == columns_[i].id) {
                mentioned = true;
                break;
            }
        }

        if (!mentioned) {
            next[placed] = columns_[i];
            ++placed;
        }
    }

    for (std::size_t i = 0; i < count_; ++i) {
        if (next[i].id != columns_[i].id) {
            changed = true;
            break;
        }
    }

    if (changed) {
        for (std::size_t i = 0; i < count_; ++i) {
            columns_[i] = next[i];
        }

        if (has_cursor_) {
            const std::size_t cursor = find(cursor_column_);

            if (cursor == kNotFound || columns_[cursor].hidden) {
                has_cursor_ = false;
                cursor_row_ = 0;
                cursor_column_ = 0;
            }
        }

        if (edit_pending_) {
            const std::size_t edited = find(edit_column_);

            if (edited == kNotFound || columns_[edited].hidden) {
                cancel_pending();
            }
        }
    }

    return changed ? DataGridResult::changed : DataGridResult::handled;
}

bool DataGridController::has_sort() const noexcept {
    return sort_count_ > 0;
}

std::size_t DataGridController::sort_count() const noexcept {
    return sort_count_;
}

std::size_t DataGridController::sort_specs(std::span<SortSpec> output) const noexcept {
    std::size_t copied = sort_count_;

    if (copied > output.size()) {
        copied = output.size();
    }

    for (std::size_t i = 0; i < copied; ++i) {
        output[i] = sorts_[i];
    }

    return copied;
}

DataGridResult DataGridController::sort_by(
    std::uint64_t column,
    SortDirection direction,
    bool accumulate
) noexcept {
    const std::size_t index = find(column);

    if (index == kNotFound || !columns_[index].sortable) {
        return DataGridResult::ignored;
    }

    if (edit_pending_) {
        cancel_pending();
        return DataGridResult::handled;
    }

    if (!accumulate) {
        if (sort_count_ == 1 && sorts_[0].column == column && sorts_[0].direction == direction) {
            return DataGridResult::handled;
        }

        sorts_[0] = SortSpec{.column = column, .direction = direction};
        sort_count_ = 1;

        return DataGridResult::changed;
    }

    std::size_t at = kNotFound;

    for (std::size_t i = 0; i < sort_count_; ++i) {
        if (sorts_[i].column == column) {
            at = i;
            break;
        }
    }

    if (at != kNotFound) {
        const SortSpec promoted{.column = column, .direction = direction};

        if (at == 0 && sorts_[0].direction == direction) {
            return DataGridResult::handled;
        }

        for (std::size_t i = at; i > 0; --i) {
            sorts_[i] = sorts_[i - 1];
        }
        sorts_[0] = promoted;

        return DataGridResult::changed;
    }

    if (sort_count_ == kSortCapacity) {
        return DataGridResult::overflow;
    }

    sorts_[sort_count_] = SortSpec{.column = column, .direction = direction};
    ++sort_count_;

    return DataGridResult::changed;
}

DataGridResult DataGridController::clear_sort() noexcept {
    if (sort_count_ == 0) {
        return DataGridResult::handled;
    }

    if (edit_pending_) {
        cancel_pending();
        return DataGridResult::handled;
    }

    sort_count_ = 0;
    sorts_[0] = SortSpec{};

    return DataGridResult::changed;
}

DataGridDensity DataGridController::density() const noexcept {
    return density_;
}

DataGridResult DataGridController::set_density(DataGridDensity density) noexcept {
    if (density_ == density) {
        return DataGridResult::handled;
    }

    density_ = density;

    return DataGridResult::changed;
}

float DataGridController::density_row_height() const noexcept {
    switch (density_) {
    case DataGridDensity::compact:
        return 24.0F;
    case DataGridDensity::comfortable:
        return 40.0F;
    default:
        return 32.0F;
    }
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

    for (std::size_t i = 0; i < sort_count_; ++i) {
        if (sorts_[i].column != column) {
            continue;
        }

        if (sorts_[i].direction == SortDirection::ascending) {
            states = states | SemanticState::sorted_ascending;
        } else {
            states = states | SemanticState::sorted_descending;
        }

        break;
    }

    output = {
        .role = SemanticRole::column_header,
        .label = accessible_name,
        .states = states,
        .actions = std::uint16_t{},
    };

    return true;
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
    constexpr std::array<DataGridPin, 3> bands{
        DataGridPin::left,
        DataGridPin::none,
        DataGridPin::right,
    };

    std::size_t copied = 0;

    for (DataGridPin band : bands) {
        for (std::size_t i = 0; i < count_ && copied < output.size(); ++i) {
            if (!columns_[i].hidden && columns_[i].pinned == band) {
                output[copied] = columns_[i].id;
                ++copied;
            }
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

bool DataGridController::same_spec(
    const DataGridColumn& before,
    const DataGridColumn& after
) const noexcept {
    return before.id == after.id && before.width == after.width &&
        before.min_width == after.min_width && before.max_width == after.max_width &&
        before.sortable == after.sortable && before.hidden == after.hidden &&
        before.pinned == after.pinned && before.locked_position == after.locked_position &&
        before.value_kind == after.value_kind;
}

std::size_t DataGridController::prune_sorts() noexcept {
    std::size_t kept = 0;

    for (std::size_t i = 0; i < sort_count_; ++i) {
        const std::size_t index = find(sorts_[i].column);

        if (index == kNotFound || !columns_[index].sortable) {
            continue;
        }

        sorts_[kept] = sorts_[i];
        ++kept;
    }

    sort_count_ = kept;

    if (kept < kSortCapacity) {
        sorts_[kept] = SortSpec{};
    }

    return kept;
}

}
