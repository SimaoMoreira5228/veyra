#include "veyra/widgets/basic/textfield.hpp"

namespace veyra {
namespace {

Srgba8 box_color(const Theme& theme, EditableTextState state) noexcept {
    if (state.disabled) {
        return theme.color(tokens::color_action_disabled);
    }

    return theme.color(tokens::color_surface);
}

Srgba8 border_color(const Theme& theme, EditableTextState state) noexcept {
    if (state.disabled) {
        return theme.color(tokens::color_action_disabled);
    }

    if (state.invalid) {
        return theme.color(tokens::color_error);
    }

    if (state.focused) {
        return theme.color(tokens::color_action_primary);
    }

    return theme.color(tokens::color_text_disabled);
}

Srgba8 caret_color(const Theme& theme, EditableTextState state) noexcept {
    if (state.disabled) {
        return theme.color(tokens::color_text_disabled);
    }

    if (state.invalid) {
        return theme.color(tokens::color_error);
    }

    return theme.color(tokens::color_action_primary);
}

Srgba8 placeholder_color(const Theme& theme, EditableTextState state) noexcept {
    if (state.disabled) {
        return theme.color(tokens::color_action_disabled);
    }

    return theme.color(tokens::color_text_disabled);
}

Srgba8 text_color(const Theme& theme, EditableTextState state) noexcept {
    if (state.disabled) {
        return theme.color(tokens::color_text_disabled);
    }

    return theme.color(tokens::color_text_primary);
}

}

TextFieldAppearance textfield_appearance(const Theme& theme, EditableTextState state) noexcept {
    return {
        .box_fill = box_color(theme, state),
        .border = border_color(theme, state),
        .caret = caret_color(theme, state),
        .placeholder = placeholder_color(theme, state),
        .text = text_color(theme, state),
        .corner_radius = theme.radius(tokens::radius_small),
        .horizontal_padding = theme.space(tokens::space_small),
    };
}

}
