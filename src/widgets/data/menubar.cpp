#include "veyra/widgets/data/menubar.hpp"

#include <cmath>

namespace veyra {
namespace {

constexpr float kSeparatorThickness = 1.0F;

bool valid_strip(LayoutRect strip) noexcept {
    if (!std::isfinite(strip.x) || !std::isfinite(strip.y)) {
        return false;
    }

    if (!std::isfinite(strip.width) || !std::isfinite(strip.height)) {
        return false;
    }

    if (strip.width <= 0.0F || strip.height <= 0.0F) {
        return false;
    }

    return true;
}

bool valid_widths(std::span<const float> widths) noexcept {
    for (float width : widths) {
        if (!std::isfinite(width) || width < 0.0F) {
            return false;
        }
    }

    return true;
}

bool needs_fill(MenubarLabelState state) noexcept {
    return state.active && !state.disabled;
}

std::size_t fill_count(std::span<const MenubarLabelState> labels) noexcept {
    auto count = std::size_t{};

    for (const auto& label : labels) {
        if (needs_fill(label)) {
            ++count;
        }
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

MenubarAppearance menubar_appearance(const Theme& theme, MenubarLabelState state) noexcept {
    Srgba8 text = theme.color(tokens::color_text_primary);

    if (state.disabled) {
        text = theme.color(tokens::color_text_disabled);
    }

    Srgba8 active_background = theme.color(tokens::color_surface);

    if (needs_fill(state)) {
        active_background = theme.color(tokens::color_action_hovered);
    }

    return {
        .background = theme.color(tokens::color_surface),
        .item_text = text,
        .active_background = active_background,
        .separator = theme.color(tokens::color_text_disabled),
    };
}

std::size_t menubar_required_commands(std::span<const MenubarLabelState> labels) noexcept {
    return 2 + fill_count(labels);
}

MenubarOutput emit_menubar(
    const Theme& theme,
    std::span<const MenubarLabelState> labels,
    const MenubarGeometry& geometry,
    std::span<DisplayCommand> commands_out
) noexcept {
    if (!valid_strip(geometry.strip_bounds) || !valid_widths(geometry.label_widths)) {
        return {
            .placed = 0,
            .required = 0,
            .result = MenubarEmitResult::invalid_input,
        };
    }

    if (labels.size() != geometry.label_widths.size()) {
        return {
            .placed = 0,
            .required = 0,
            .result = MenubarEmitResult::invalid_input,
        };
    }

    const MenubarAppearance appearance = menubar_appearance(theme, {});
    const std::size_t required = menubar_required_commands(labels);

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

    push(
        FillRect{
            .bounds = geometry.strip_bounds,
            .color = appearance.background,
        }
    );

    auto cursor = geometry.strip_bounds.x;

    for (auto index = std::size_t{}; index < labels.size(); ++index) {
        const float width = geometry.label_widths[index];

        if (needs_fill(labels[index])) {
            push(
                FillRect{
                    .bounds =
                        {.x = cursor,
                         .y = geometry.strip_bounds.y,
                         .width = width,
                         .height = geometry.strip_bounds.height},
                    .color = menubar_appearance(theme, labels[index]).active_background,
                }
            );
        }

        cursor += width;
    }

    push(
        FillRect{
            .bounds =
                {.x = geometry.strip_bounds.x,
                 .y = geometry.strip_bounds.y + geometry.strip_bounds.height - kSeparatorThickness,
                 .width = geometry.strip_bounds.width,
                 .height = kSeparatorThickness},
            .color = appearance.separator,
        }
    );

    if (exhausted) {
        return {
            .placed = placed,
            .required = required,
            .result = MenubarEmitResult::insufficient_output,
        };
    }

    return {
        .placed = placed,
        .required = required,
        .result = MenubarEmitResult::complete,
    };
}

}
