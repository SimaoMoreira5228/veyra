#include "veyra/widgets/basic/text_area.hpp"

#include <cmath>

namespace veyra {
namespace {

constexpr float kBorderThickness = 1.0F;

bool valid_rect(LayoutRect rect) noexcept {
    if (!std::isfinite(rect.x) || !std::isfinite(rect.y)) {
        return false;
    }

    if (!std::isfinite(rect.width) || !std::isfinite(rect.height)) {
        return false;
    }

    return rect.width > 0.0F && rect.height > 0.0F;
}

Srgba8 box_color(const Theme& theme, TextAreaState state) noexcept {
    if (state.disabled) {
        return theme.color(tokens::color_action_disabled);
    }

    return theme.color(tokens::color_surface);
}

Srgba8 border_color(const Theme& theme, TextAreaState state) noexcept {
    if (state.disabled) {
        return theme.color(tokens::color_action_disabled);
    }

    if (state.focused) {
        return theme.color(tokens::color_action_primary);
    }

    return theme.color(tokens::color_text_disabled);
}

Srgba8 caret_color(const Theme& theme, TextAreaState state) noexcept {
    if (state.disabled) {
        return theme.color(tokens::color_text_disabled);
    }

    return theme.color(tokens::color_action_primary);
}

Srgba8 placeholder_color(const Theme& theme, TextAreaState state) noexcept {
    if (state.disabled) {
        return theme.color(tokens::color_action_disabled);
    }

    return theme.color(tokens::color_text_disabled);
}

Srgba8 selection_color(const Theme& theme, TextAreaState state) noexcept {
    if (state.disabled) {
        return theme.color(tokens::color_action_disabled);
    }

    return theme.color(tokens::color_action_hovered);
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

TextAreaAppearance text_area_appearance(const Theme& theme, TextAreaState state) noexcept {
    return {
        .box_fill = box_color(theme, state),
        .border = border_color(theme, state),
        .caret = caret_color(theme, state),
        .placeholder = placeholder_color(theme, state),
        .selection = selection_color(theme, state),
        .corner_radius = theme.radius(tokens::radius_small),
        .horizontal_padding = theme.space(tokens::space_small),
        .vertical_padding = theme.space(tokens::space_small),
    };
}

std::size_t text_area_required_commands(const TextAreaGeometry& geometry) noexcept {
    auto required = std::size_t{2} + geometry.selection_rects.size();

    if (geometry.show_caret) {
        ++required;
    }

    if (geometry.has_composition) {
        ++required;
    }

    if (geometry.clip_to_viewport) {
        required += 2;
    }

    return required;
}

TextAreaOutput emit_text_area(
    const Theme& theme,
    TextAreaState state,
    const TextAreaGeometry& geometry,
    std::span<DisplayCommand> commands_out
) noexcept {
    if (!valid_rect(geometry.bounds)) {
        return {
            .placed = 0,
            .required = 0,
            .result = TextAreaEmitResult::invalid_input,
        };
    }

    if (geometry.show_caret && !valid_rect(geometry.caret)) {
        return {
            .placed = 0,
            .required = 0,
            .result = TextAreaEmitResult::invalid_input,
        };
    }

    if (geometry.has_composition && !valid_rect(geometry.composition_underline)) {
        return {
            .placed = 0,
            .required = 0,
            .result = TextAreaEmitResult::invalid_input,
        };
    }

    for (const auto& rect : geometry.selection_rects) {
        if (!valid_rect(rect)) {
            return {
                .placed = 0,
                .required = 0,
                .result = TextAreaEmitResult::invalid_input,
            };
        }
    }

    const TextAreaAppearance appearance = text_area_appearance(theme, state);
    const std::size_t required = text_area_required_commands(geometry);

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

    if (geometry.clip_to_viewport) {
        push(
            PushClip{
                .bounds = geometry.bounds,
                .clip_id = 0,
            }
        );
    }

    push(
        FillRoundedRect{
            .bounds = geometry.bounds,
            .radius = appearance.corner_radius.value,
            .color = appearance.box_fill,
        }
    );

    for (const auto& rect : geometry.selection_rects) {
        push(
            FillRect{
                .bounds = rect,
                .color = appearance.selection,
            }
        );
    }

    if (geometry.has_composition) {
        push(
            FillRect{
                .bounds = geometry.composition_underline,
                .color = theme.color(tokens::color_action_primary),
            }
        );
    }

    if (geometry.show_caret) {
        push(
            FillRect{
                .bounds = geometry.caret,
                .color = appearance.caret,
            }
        );
    }

    push(
        StrokeRoundedRect{
            .bounds = geometry.bounds,
            .radius = appearance.corner_radius.value,
            .thickness = kBorderThickness,
            .color = appearance.border,
        }
    );

    if (geometry.clip_to_viewport) {
        push(PopClip{});
    }

    if (exhausted) {
        return {
            .placed = placed,
            .required = required,
            .result = TextAreaEmitResult::insufficient_output,
        };
    }

    return {
        .placed = placed,
        .required = required,
        .result = TextAreaEmitResult::complete,
    };
}

}
