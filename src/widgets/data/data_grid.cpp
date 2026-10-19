#include "veyra/widgets/data/data_grid.hpp"

namespace veyra {

DataGridHeaderAppearance
data_grid_header_appearance(const Theme& theme, DataGridHeaderState state) noexcept {
    Srgba8 background = theme.color(tokens::color_surface);
    if (state.sorted_asc || state.sorted_desc || state.pinned) {
        background = theme.color(tokens::color_action_hovered);
    }

    return {
        .background = background,
        .text = theme.color(tokens::color_text_primary),
        .sort_arrow = theme.color(tokens::color_action_primary),
        .separator = theme.color(tokens::color_text_disabled),
        .pinned_separator = theme.color(tokens::color_action_primary),
    };
}

DataGridCellAppearance
data_grid_cell_appearance(const Theme& theme, DataGridCellState state) noexcept {
    Srgba8 background = theme.color(tokens::color_surface);
    if (state.selected || state.in_range) {
        background = theme.color(tokens::color_action_hovered);
    }

    Srgba8 outline = theme.color(tokens::color_text_disabled);
    if (state.current || state.edit_pending || state.in_range) {
        outline = theme.color(tokens::color_action_primary);
    }

    return {.background = background, .range_outline = outline};
}

namespace {

bool header_sorted_asc(std::uint64_t id, std::span<const SortSpec> sorts) noexcept {
    for (const auto& spec : sorts) {
        if (spec.column == id) {
            return spec.direction == SortDirection::ascending;
        }
    }

    return false;
}

bool header_sorted_desc(std::uint64_t id, std::span<const SortSpec> sorts) noexcept {
    for (const auto& spec : sorts) {
        if (spec.column == id) {
            return spec.direction == SortDirection::descending;
        }
    }

    return false;
}

bool header_pinned(DataGridPin pin) noexcept {
    return pin == DataGridPin::left || pin == DataGridPin::right;
}

bool header_boundary(const DataGridColumn& lead, const DataGridColumn& trail) noexcept {
    if (lead.pinned == DataGridPin::left && trail.pinned != DataGridPin::left) {
        return true;
    }

    return lead.pinned != DataGridPin::right && trail.pinned == DataGridPin::right;
}

std::size_t
header_need(std::span<const DataGridColumn> columns, std::span<const SortSpec> sorts) noexcept {
    std::size_t need = 1;

    for (std::size_t index = 0; index < columns.size(); ++index) {
        const bool sorted = header_sorted_asc(columns[index].id, sorts) ||
            header_sorted_desc(columns[index].id, sorts);
        const bool pinned = header_pinned(columns[index].pinned);

        if (sorted || pinned) {
            ++need;
        }

        if (sorted && columns[index].width >= 24.0F) {
            ++need;
        }

        if (index + 1 < columns.size()) {
            ++need;
        }
    }

    return need;
}

}

DataGridEmit emit_data_grid_header(
    const Theme& theme,
    std::span<const DataGridColumn> columns,
    std::span<const SortSpec> sorts,
    DataGridHeaderGeometry geometry,
    std::span<DisplayCommand> commands_out
) noexcept {
    if (commands_out.size() < header_need(columns, sorts)) {
        return {.count = 0, .overflow = true};
    }

    float total = 0.0F;
    for (const auto& column : columns) {
        total += column.width;
    }

    std::size_t count = 0;
    commands_out[count++] = FillRect{
        .bounds = {.x = geometry.x, .y = geometry.y, .width = total, .height = geometry.row_height},
        .color = theme.color(tokens::color_surface),
    };

    float cursor = geometry.x;
    for (std::size_t index = 0; index < columns.size(); ++index) {
        const auto& column = columns[index];
        const bool asc = header_sorted_asc(column.id, sorts);
        const bool desc = header_sorted_desc(column.id, sorts);
        const bool pinned = header_pinned(column.pinned);
        const DataGridHeaderAppearance chrome = data_grid_header_appearance(
            theme,
            {.sorted_asc = asc, .sorted_desc = desc, .pinned = pinned}
        );

        if (chrome.background != theme.color(tokens::color_surface)) {
            commands_out[count++] = FillRect{
                .bounds =
                    {.x = cursor,
                     .y = geometry.y,
                     .width = column.width,
                     .height = geometry.row_height},
                .color = chrome.background,
            };
        }

        if ((asc || desc) && column.width >= 24.0F) {
            const float slot = asc ? geometry.y + geometry.row_height / 2.0F - 4.0F
                                   : geometry.y + geometry.row_height / 2.0F + 1.0F;
            commands_out[count++] = FillRect{
                .bounds =
                    {.x = cursor + column.width - 14.0F, .y = slot, .width = 8.0F, .height = 3.0F},
                .color = chrome.sort_arrow,
            };
        }

        if (index + 1 < columns.size()) {
            const bool divider = header_boundary(column, columns[index + 1]);
            const float edge = divider ? 2.0F : 1.0F;
            commands_out[count++] = FillRect{
                .bounds =
                    {.x = cursor + column.width - edge,
                     .y = geometry.y,
                     .width = edge,
                     .height = geometry.row_height},
                .color = divider ? chrome.pinned_separator : chrome.separator,
            };
        }

        cursor += column.width;
    }

    return {.count = count, .overflow = false};
}

DataGridEmit emit_data_grid_range(
    const Theme& theme,
    const DataGridRangeGeometry& geometry,
    std::span<DisplayCommand> commands_out
) noexcept {
    const bool current = geometry.has_current || geometry.edit_pending;
    const std::size_t need = (geometry.has_range ? 1 : 0) + (current ? 1 : 0);
    if (commands_out.size() < need) {
        return {.count = 0, .overflow = true};
    }

    std::size_t count = 0;
    if (geometry.has_range) {
        commands_out[count++] = StrokeRect{
            .bounds = geometry.bounds,
            .thickness = 1.0F,
            .color = theme.color(tokens::color_action_primary),
        };
    }

    if (current) {
        commands_out[count++] = StrokeRect{
            .bounds = geometry.current,
            .thickness = 2.0F,
            .color = theme.color(tokens::color_action_primary),
        };
    }

    return {.count = count, .overflow = false};
}

DataGridEmit emit_data_grid_cell_background(
    const Theme& theme,
    const DataGridCellAppearance& appearance,
    LayoutRect bounds,
    std::span<DisplayCommand> commands_out
) noexcept {
    if (appearance.background == theme.color(tokens::color_surface)) {
        return {.count = 0, .overflow = false};
    }

    if (commands_out.empty()) {
        return {.count = 0, .overflow = true};
    }

    commands_out[0] = FillRect{.bounds = bounds, .color = appearance.background};

    return {.count = 1, .overflow = false};
}

}
