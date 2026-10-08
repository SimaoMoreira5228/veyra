#include "veyra/widgets/basic/button.hpp"

#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

void check_interaction_state_precedence() {
    const auto theme = veyra::Theme{};

    const auto idle = veyra::button_appearance(theme, {});
    require(
        idle.background == theme.color(veyra::tokens::color_action_primary) &&
            idle.foreground == theme.color(veyra::tokens::color_action_on_primary),
        "idle appearance uses the primary action colors"
    );

    const auto hovered_state = veyra::PressableState{
        .hovered = true,
        .focused = true,
    };
    const auto hovered = veyra::button_appearance(theme, hovered_state);
    require(
        hovered.background == theme.color(veyra::tokens::color_action_hovered),
        "hover changes the button background"
    );

    const auto pressed_state = veyra::PressableState{
        .hovered = true,
        .pressed = true,
    };
    const auto pressed = veyra::button_appearance(theme, pressed_state);
    require(
        pressed.background == theme.color(veyra::tokens::color_action_pressed),
        "pressed styling takes priority over hover"
    );

    const auto disabled_state = veyra::PressableState{
        .hovered = true,
        .pressed = true,
        .disabled = true,
    };
    const auto disabled = veyra::button_appearance(theme, disabled_state);
    require(
        disabled.background == theme.color(veyra::tokens::color_action_disabled) &&
            disabled.foreground == theme.color(veyra::tokens::color_text_disabled),
        "disabled styling takes priority over active pointer state"
    );
}

void check_layout_values_follow_theme() {
    auto theme = veyra::Theme{};
    theme.set(veyra::tokens::space_large, veyra::LogicalPx{18.0F});
    theme.set(veyra::tokens::space_medium, veyra::LogicalPx{6.0F});
    theme.set(veyra::tokens::radius_medium, veyra::LogicalPx{9.0F});

    const auto appearance = veyra::button_appearance(theme, {});
    require(
        appearance.horizontal_padding == veyra::LogicalPx{18.0F} &&
            appearance.vertical_padding == veyra::LogicalPx{6.0F} &&
            appearance.corner_radius == veyra::LogicalPx{9.0F},
        "layout values resolve through the theme"
    );
}

}

int main() {
    try {
        check_interaction_state_precedence();
        check_layout_values_follow_theme();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
