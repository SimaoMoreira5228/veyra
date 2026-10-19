#include "veyra/widgets/data/tree_view.hpp"

#include <cmath>

namespace veyra {
namespace {

constexpr float kSeparatorThickness = 1.0F;
constexpr float kOutlineThickness = 2.0F;
constexpr float kGuideThickness = 1.0F;
constexpr float kChevronThickness = 1.5F;
constexpr float kChevronArm = 5.0F;
constexpr float kChevronHalfHeight = 4.0F;
constexpr float kGuideCenter = 3.0F;

bool valid_geometry(const TreeViewRowsGeometry& geometry) noexcept {
    if (!std::isfinite(geometry.row_height) || geometry.row_height <= 0.0F) {
        return false;
    }

    if (!std::isfinite(geometry.sticky_header_height) || geometry.sticky_header_height < 0.0F) {
        return false;
    }

    return true;
}

float resolve_indent(const Theme& theme, const TreeViewRowsGeometry& geometry) noexcept {
    if (std::isfinite(geometry.indent_width) && geometry.indent_width > 0.0F) {
        return geometry.indent_width;
    }

    const float fallback = theme.space(tokens::space_medium).value;

    if (!std::isfinite(fallback) || fallback <= 0.0F) {
        return 0.0F;
    }

    return fallback;
}

bool needs_background(TreeViewRowState state) noexcept {
    return state.placeholder || state.selected;
}

bool needs_chevron(const TreeCursorRow& row) noexcept {
    return row.has_children;
}

bool pending_chevron(const TreeCursorRow& row) noexcept {
    return row.has_children && !row.children_known;
}

std::size_t row_command_count(TreeViewRowState state, const TreeCursorRow& row) noexcept {
    auto count = std::size_t{1};

    if (needs_background(state)) {
        ++count;
    }

    count += static_cast<std::size_t>(row.depth);

    if (needs_chevron(row)) {
        count += 2;
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

TreeViewRowAppearance tree_view_row_appearance(
    const Theme& theme,
    TreeViewRowState state,
    const TreeCursorRow& row
) noexcept {
    auto background = theme.color(tokens::color_surface);

    if (state.placeholder) {
        background = theme.color(tokens::color_action_disabled);
    } else if (state.selected) {
        background = theme.color(tokens::color_action_hovered);
    }

    auto chevron = theme.color(tokens::color_action_primary);

    if (pending_chevron(row)) {
        chevron = theme.color(tokens::color_text_disabled);
    }

    return {
        .background = background,
        .separator = theme.color(tokens::color_text_disabled),
        .current_outline = theme.color(tokens::color_action_primary),
        .chevron = chevron,
        .guide = theme.color(tokens::color_text_disabled),
    };
}

std::size_t tree_view_required_commands(
    std::span<const TreeCursorRow> rows,
    std::span<const TreeViewRowState> states,
    const TreeViewRowsGeometry& geometry
) noexcept {
    if (rows.size() != states.size()) {
        return 0;
    }

    auto required = std::size_t{};

    if (geometry.sticky_header_height > 0.0F) {
        required += 2;
    }

    for (std::size_t index = 0; index < rows.size(); ++index) {
        required += row_command_count(states[index], rows[index]);
    }

    return required;
}

TreeViewRowsOutput emit_tree_view_rows(
    const Theme& theme,
    std::span<const TreeCursorRow> rows,
    std::span<const TreeViewRowState> states,
    const TreeViewRowsGeometry& geometry,
    std::span<DisplayCommand> commands_out
) noexcept {
    if (!valid_geometry(geometry) || rows.size() != states.size()) {
        return {
            .placed = 0,
            .required = 0,
            .result = TreeViewEmitResult::invalid_input,
        };
    }

    const float indent = resolve_indent(theme, geometry);
    const std::size_t required = tree_view_required_commands(rows, states, geometry);

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

    for (std::size_t index = 0; index < rows.size(); ++index) {
        const TreeCursorRow& row = rows[index];
        const TreeViewRowState state = states[index];
        const float top = geometry.list_bounds.y + geometry.sticky_header_height +
            static_cast<float>(index) * geometry.row_height;
        const float middle = top + geometry.row_height * 0.5F;
        const TreeViewRowAppearance appearance = tree_view_row_appearance(theme, state, row);

        if (needs_background(state)) {
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

        for (std::uint32_t level = 0; level < row.depth; ++level) {
            const float guide_x =
                geometry.list_bounds.x + static_cast<float>(level) * indent + kGuideCenter;
            push(
                StrokeLine{
                    .start = {.x = guide_x, .y = top},
                    .end = {.x = guide_x, .y = top + geometry.row_height},
                    .thickness = kGuideThickness,
                    .color = appearance.guide,
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

        if (needs_chevron(row)) {
            const float chevron_x = geometry.list_bounds.x + static_cast<float>(row.depth) * indent;

            if (row.expanded) {
                push(
                    StrokeLine{
                        .start = {.x = chevron_x, .y = middle - 2.0F},
                        .end = {.x = chevron_x + kGuideCenter, .y = middle + 1.0F},
                        .thickness = kChevronThickness,
                        .color = appearance.chevron,
                    }
                );
                push(
                    StrokeLine{
                        .start = {.x = chevron_x + kGuideCenter, .y = middle + 1.0F},
                        .end = {.x = chevron_x + 2.0F * kGuideCenter, .y = middle - 2.0F},
                        .thickness = kChevronThickness,
                        .color = appearance.chevron,
                    }
                );
            } else {
                push(
                    StrokeLine{
                        .start = {.x = chevron_x, .y = middle - kChevronHalfHeight},
                        .end = {.x = chevron_x + kChevronArm, .y = middle},
                        .thickness = kChevronThickness,
                        .color = appearance.chevron,
                    }
                );
                push(
                    StrokeLine{
                        .start = {.x = chevron_x + kChevronArm, .y = middle},
                        .end = {.x = chevron_x, .y = middle + kChevronHalfHeight},
                        .thickness = kChevronThickness,
                        .color = appearance.chevron,
                    }
                );
            }
        }

        if (state.current) {
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
            .result = TreeViewEmitResult::insufficient_output,
        };
    }

    return {
        .placed = placed,
        .required = required,
        .result = TreeViewEmitResult::complete,
    };
}

}
