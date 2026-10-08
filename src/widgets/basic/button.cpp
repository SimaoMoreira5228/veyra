#include "veyra/widgets/basic/button.hpp"

namespace veyra {

ButtonAppearance button_appearance(const Theme& theme, PressableState state) noexcept {
    auto background = theme.color(tokens::color_action_primary);
    auto foreground = theme.color(tokens::color_action_on_primary);

    if (state.disabled) {
        background = theme.color(tokens::color_action_disabled);
        foreground = theme.color(tokens::color_text_disabled);
    } else if (state.pressed) {
        background = theme.color(tokens::color_action_pressed);
    } else if (state.hovered) {
        background = theme.color(tokens::color_action_hovered);
    }

    return {
        .background = background,
        .foreground = foreground,
        .horizontal_padding = theme.space(tokens::space_large),
        .vertical_padding = theme.space(tokens::space_medium),
        .corner_radius = theme.radius(tokens::radius_medium),
    };
}

}
