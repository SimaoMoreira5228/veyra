#include "veyra/widgets/basic/toggle.hpp"

namespace veyra {
namespace {

bool is_on(ToggleValue value) noexcept {
    return value == ToggleValue::checked || value == ToggleValue::mixed;
}

Srgba8 fill_color(const Theme& theme, ToggleState state) noexcept {
    if (state.disabled) {
        return theme.color(tokens::color_action_disabled);
    }

    if (state.pressed) {
        return theme.color(tokens::color_action_pressed);
    }

    if (state.hovered) {
        return theme.color(tokens::color_action_hovered);
    }

    if (is_on(state.value)) {
        return theme.color(tokens::color_toggle_checked);
    }

    return theme.color(tokens::color_surface);
}

Srgba8 accent_color(const Theme& theme, ToggleState state) noexcept {
    if (state.disabled) {
        return theme.color(tokens::color_text_disabled);
    }

    if (is_on(state.value)) {
        return theme.color(tokens::color_toggle_on_checked);
    }

    return theme.color(tokens::color_text_primary);
}

}

CheckboxAppearance checkbox_appearance(const Theme& theme, ToggleState state) noexcept {
    return {
        .box = fill_color(theme, state),
        .mark = accent_color(theme, state),
        .box_size = theme.space(tokens::space_large),
        .corner_radius = theme.radius(tokens::radius_small),
    };
}

SwitchAppearance switch_appearance(const Theme& theme, ToggleState state) noexcept {
    return {
        .track = fill_color(theme, state),
        .thumb = accent_color(theme, state),
        .track_width = theme.space(tokens::space_extra_large),
        .track_height = theme.space(tokens::space_large),
        .corner_radius = theme.radius(tokens::radius_large),
    };
}

}
