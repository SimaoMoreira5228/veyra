#include "veyra/widgets/basic/radio.hpp"

namespace veyra {
namespace {

Srgba8 ring_color(const Theme& theme, RadioGroupState state, bool selected) noexcept {
    if (state.disabled) {
        return theme.color(tokens::color_action_disabled);
    }

    if (selected) {
        return theme.color(tokens::color_toggle_checked);
    }

    return theme.color(tokens::color_text_disabled);
}

Srgba8 dot_color(const Theme& theme, RadioGroupState state, bool selected) noexcept {
    if (state.disabled) {
        return theme.color(tokens::color_text_disabled);
    }

    if (selected) {
        return theme.color(tokens::color_toggle_on_checked);
    }

    return theme.color(tokens::color_surface);
}

}

RadioAppearance
radio_appearance(const Theme& theme, RadioGroupState state, bool selected) noexcept {
    const auto diameter = theme.space(tokens::space_large);

    return {
        .ring = ring_color(theme, state, selected),
        .dot = dot_color(theme, state, selected),
        .ring_diameter = diameter,
        .dot_diameter = {diameter.value / 2.0F},
    };
}

}
