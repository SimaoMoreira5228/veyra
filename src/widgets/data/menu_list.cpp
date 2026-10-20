#include "veyra/widgets/data/menu_list.hpp"

#include <cmath>

namespace veyra {
namespace {

constexpr float kSeparatorThickness = 1.0F;
constexpr float kMarkThickness = 1.5F;
constexpr float kTextAllowance = 16.0F;
constexpr float kCheckPadX = 6.0F;
constexpr float kCheckShortDrop = 1.0F;
constexpr float kCheckLegRise = 2.0F;
constexpr float kCheckShortRun = 3.0F;
constexpr float kCheckLongRun = 6.0F;
constexpr float kCheckTipDrop = 4.0F;
constexpr float kArrowTipInset = 6.0F;
constexpr float kArrowRun = 4.0F;
constexpr float kArrowHalfHeight = 4.0F;

bool valid_geometry(const MenuListGeometry& geometry) noexcept {
    if (!std::isfinite(geometry.row_height) || geometry.row_height < 0.0F) {
        return false;
    }

    if (!std::isfinite(geometry.shortcut_column_x)) {
        return false;
    }

    return true;
}

float resolve_row_height(const Theme& theme, const MenuListGeometry& geometry) noexcept {
    if (geometry.row_height > 0.0F) {
        return geometry.row_height;
    }

    return theme.space(tokens::space_medium).value + kTextAllowance;
}

bool needs_background(MenuListRowState state) noexcept {
    if (state.disabled || state.is_separator) {
        return false;
    }

    return state.active || state.checked;
}

std::size_t row_command_count(MenuListRowState state) noexcept {
    if (state.is_separator) {
        return 1;
    }

    auto count = std::size_t{};

    if (needs_background(state)) {
        ++count;
    }

    if (state.checked) {
        count += 2;
    }

    if (state.has_submenu) {
        count += 2;
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

MenuRowAppearance menu_row_appearance(const Theme& theme, MenuListRowState state) noexcept {
    Srgba8 background = theme.color(tokens::color_surface);

    if (!state.disabled && !state.is_separator) {
        if (state.active) {
            background = theme.color(tokens::color_action_hovered);
        } else if (state.checked) {
            background = theme.color(tokens::color_toggle_checked);
        }
    }

    return {
        .background = background,
        .text = theme.color(tokens::color_text_primary),
        .checkmark = theme.color(tokens::color_toggle_checked),
        .separator = theme.color(tokens::color_text_disabled),
        .submenu_arrow = theme.color(tokens::color_text_primary),
        .disabled_text = theme.color(tokens::color_text_disabled),
    };
}

std::size_t menu_list_required_commands(std::span<const MenuListRowState> rows) noexcept {
    auto required = std::size_t{};

    for (const auto& row : rows) {
        required += row_command_count(row);
    }

    return required;
}

MenuListOutput emit_menu_list(
    const Theme& theme,
    std::span<const MenuListRowState> rows,
    const MenuListGeometry& geometry,
    std::span<DisplayCommand> commands_out
) noexcept {
    if (!valid_geometry(geometry)) {
        return {
            .placed = 0,
            .required = 0,
            .result = MenuListEmitResult::invalid_input,
        };
    }

    const float row_height = resolve_row_height(theme, geometry);
    const std::size_t required = menu_list_required_commands(rows);

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

    for (auto index = std::size_t{}; index < rows.size(); ++index) {
        const MenuListRowState state = rows[index];
        const float top = geometry.list_bounds.y + static_cast<float>(index) * row_height;
        const float middle = top + row_height * 0.5F;
        const MenuRowAppearance appearance = menu_row_appearance(theme, state);

        if (state.is_separator) {
            push(
                FillRect{
                    .bounds =
                        {.x = geometry.list_bounds.x,
                         .y = top + (row_height - kSeparatorThickness) * 0.5F,
                         .width = geometry.list_bounds.width,
                         .height = kSeparatorThickness},
                    .color = appearance.separator,
                }
            );
        } else {
            if (needs_background(state)) {
                push(
                    FillRect{
                        .bounds =
                            {.x = geometry.list_bounds.x,
                             .y = top,
                             .width = geometry.list_bounds.width,
                             .height = row_height},
                        .color = appearance.background,
                    }
                );
            }

            if (state.checked) {
                const float check_x = geometry.list_bounds.x + kCheckPadX;
                const float joint_x = check_x + kCheckShortRun;
                const float joint_y = middle + kCheckLegRise;
                const Srgba8 mark =
                    state.disabled ? appearance.disabled_text : appearance.checkmark;

                push(
                    StrokeLine{
                        .start = {.x = check_x, .y = middle - kCheckShortDrop},
                        .end = {.x = joint_x, .y = joint_y},
                        .thickness = kMarkThickness,
                        .color = mark,
                    }
                );
                push(
                    StrokeLine{
                        .start = {.x = joint_x, .y = joint_y},
                        .end = {.x = joint_x + kCheckLongRun, .y = middle - kCheckTipDrop},
                        .thickness = kMarkThickness,
                        .color = mark,
                    }
                );
            }

            if (state.has_submenu) {
                const float tip_x =
                    geometry.list_bounds.x + geometry.list_bounds.width - kArrowTipInset;
                const float base_x = tip_x - kArrowRun;
                const Srgba8 arrow =
                    state.disabled ? appearance.disabled_text : appearance.submenu_arrow;

                push(
                    StrokeLine{
                        .start = {.x = base_x, .y = middle - kArrowHalfHeight},
                        .end = {.x = tip_x, .y = middle},
                        .thickness = kMarkThickness,
                        .color = arrow,
                    }
                );
                push(
                    StrokeLine{
                        .start = {.x = tip_x, .y = middle},
                        .end = {.x = base_x, .y = middle + kArrowHalfHeight},
                        .thickness = kMarkThickness,
                        .color = arrow,
                    }
                );
            }
        }
    }

    if (exhausted) {
        return {
            .placed = placed,
            .required = required,
            .result = MenuListEmitResult::insufficient_output,
        };
    }

    return {
        .placed = placed,
        .required = required,
        .result = MenuListEmitResult::complete,
    };
}

}
