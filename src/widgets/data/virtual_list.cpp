#include "veyra/widgets/data/virtual_list.hpp"

#include <cmath>

namespace veyra {
namespace {

constexpr float kSeparatorThickness = 1.0F;
constexpr float kOutlineThickness = 2.0F;

bool valid_geometry(const VirtualListRowsGeometry& geometry) noexcept {
    if (!std::isfinite(geometry.row_height) || geometry.row_height <= 0.0F) {
        return false;
    }

    if (!std::isfinite(geometry.sticky_header_height) || geometry.sticky_header_height < 0.0F) {
        return false;
    }

    return true;
}

bool needs_background(VirtualListRowState state) noexcept {
    return state.placeholder || state.selected;
}

std::size_t row_command_count(VirtualListRowState state) noexcept {
    auto count = std::size_t{1};

    if (needs_background(state)) {
        ++count;
    }

    if (state.current) {
        ++count;
    }

    return count;
}

bool push_command(
    std::span<DisplayCommand> output,
    std::size_t& placed,
    DisplayCommand command
) noexcept {
    if (placed >= output.size()) {
        return false;
    }

    output[placed] = command;
    ++placed;

    return true;
}

}

VirtualListRowAppearance
virtual_list_row_appearance(const Theme& theme, VirtualListRowState state) noexcept {
    auto background = theme.color(tokens::color_surface);

    if (state.placeholder) {
        background = theme.color(tokens::color_action_disabled);
    } else if (state.selected) {
        background = theme.color(tokens::color_action_hovered);
    }

    return {
        .background = background,
        .separator = theme.color(tokens::color_text_disabled),
        .current_outline = theme.color(tokens::color_action_primary),
    };
}

VirtualListRowsOutput emit_virtual_list_rows(
    const Theme& theme,
    std::span<const VirtualListRowState> rows,
    const VirtualListRowsGeometry& geometry,
    std::span<DisplayCommand> commands_out
) noexcept {
    if (!valid_geometry(geometry)) {
        return {
            .placed = 0,
            .required = 0,
            .result = VirtualListEmitResult::invalid_input,
        };
    }

    auto required = std::size_t{};

    if (geometry.sticky_header_height > 0.0F) {
        required += 2;
    }

    for (const auto& row : rows) {
        required += row_command_count(row);
    }

    auto placed = std::size_t{};
    auto exhausted = false;

    auto push = [&](DisplayCommand command) noexcept {
        if (exhausted) {
            return;
        }

        if (!push_command(commands_out, placed, command)) {
            exhausted = true;
        }
    };

    if (geometry.sticky_header_height > 0.0F) {
        push(
            FillRect{
                .bounds =
                    {.x = geometry.list_bounds.x,
                     .y = geometry.list_bounds.y,
                     .width = geometry.list_bounds.width,
                     .height = geometry.sticky_header_height},
                .color = theme.color(tokens::color_surface),
            }
        );
        push(
            FillRect{
                .bounds =
                    {.x = geometry.list_bounds.x,
                     .y = geometry.list_bounds.y + geometry.sticky_header_height -
                         kSeparatorThickness,
                     .width = geometry.list_bounds.width,
                     .height = kSeparatorThickness},
                .color = theme.color(tokens::color_text_disabled),
            }
        );
    }

    for (auto index = std::size_t{}; index < rows.size(); ++index) {
        const auto top = geometry.list_bounds.y + geometry.sticky_header_height +
            static_cast<float>(index) * geometry.row_height;
        const auto appearance = virtual_list_row_appearance(theme, rows[index]);

        if (needs_background(rows[index])) {
            push(
                FillRect{
                    .bounds =
                        {.x = geometry.list_bounds.x,
                         .y = top,
                         .width = geometry.list_bounds.width,
                         .height = geometry.row_height},
                    .color = appearance.background,
                }
            );
        }

        push(
            FillRect{
                .bounds =
                    {.x = geometry.list_bounds.x,
                     .y = top + geometry.row_height - kSeparatorThickness,
                     .width = geometry.list_bounds.width,
                     .height = kSeparatorThickness},
                .color = appearance.separator,
            }
        );

        if (rows[index].current) {
            push(
                StrokeRect{
                    .bounds =
                        {.x = geometry.list_bounds.x,
                         .y = top,
                         .width = geometry.list_bounds.width,
                         .height = geometry.row_height},
                    .thickness = kOutlineThickness,
                    .color = appearance.current_outline,
                }
            );
        }
    }

    if (exhausted) {
        return {
            .placed = placed,
            .required = required,
            .result = VirtualListEmitResult::insufficient_output,
        };
    }

    return {
        .placed = placed,
        .required = required,
        .result = VirtualListEmitResult::complete,
    };
}

}
