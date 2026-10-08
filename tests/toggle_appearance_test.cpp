#include "veyra/widgets/basic/toggle.hpp"

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

veyra::ToggleState toggled_on() {
    return {.value = veyra::ToggleValue::checked};
}

void check_value_and_interaction_precedence() {
    const auto theme = veyra::Theme{};

    const auto off = veyra::checkbox_appearance(theme, {});
    require(
        off.box == theme.color(veyra::tokens::color_surface) &&
            off.mark == theme.color(veyra::tokens::color_text_primary),
        "an unchecked box uses the surface and primary text colors"
    );

    const auto on = veyra::checkbox_appearance(theme, toggled_on());
    require(
        on.box == theme.color(veyra::tokens::color_toggle_checked) &&
            on.mark == theme.color(veyra::tokens::color_toggle_on_checked),
        "a checked box resolves the checked tokens"
    );
    require(
        on.box_size == theme.space(veyra::tokens::space_large) &&
            on.corner_radius == theme.radius(veyra::tokens::radius_small),
        "checkbox geometry resolves through the theme"
    );

    const auto track = veyra::switch_appearance(theme, toggled_on());
    require(
        track.track == theme.color(veyra::tokens::color_toggle_checked) &&
            track.thumb == theme.color(veyra::tokens::color_toggle_on_checked),
        "a switched-on track resolves the checked tokens"
    );
    require(
        track.track_width == theme.space(veyra::tokens::space_extra_large) &&
            track.track_height == theme.space(veyra::tokens::space_large) &&
            track.corner_radius == theme.radius(veyra::tokens::radius_large),
        "switch geometry resolves through the theme"
    );

    const auto pressed = veyra::ToggleState{
        .value = veyra::ToggleValue::checked,
        .hovered = true,
        .pressed = true,
    };
    const auto active = veyra::checkbox_appearance(theme, pressed);
    require(
        active.box == theme.color(veyra::tokens::color_action_pressed),
        "pressed styling takes priority over the checked fill"
    );

    const auto disabled = veyra::ToggleState{
        .value = veyra::ToggleValue::checked,
        .hovered = true,
        .pressed = true,
        .disabled = true,
    };
    const auto unavailable = veyra::switch_appearance(theme, disabled);
    require(
        unavailable.track == theme.color(veyra::tokens::color_action_disabled) &&
            unavailable.thumb == theme.color(veyra::tokens::color_text_disabled),
        "disabled styling takes priority over active checked state"
    );
}

void check_checked_token_overrides_bump_version() {
    auto theme = veyra::Theme{};
    const auto initial_version = theme.version();
    const auto fill = veyra::Srgba8{.red = 12, .green = 34, .blue = 56, .alpha = 78};

    require(
        theme.set(veyra::tokens::color_toggle_checked, fill) == veyra::ThemeUpdateResult::changed,
        "a new toggle override reports a change"
    );
    require(theme.version() != initial_version, "a changed theme advances its version");
    require(
        veyra::checkbox_appearance(theme, toggled_on()).box == fill,
        "the checked override resolves into the recipe"
    );
    require(
        theme.clear(veyra::tokens::color_toggle_checked) == veyra::ThemeUpdateResult::changed &&
            veyra::checkbox_appearance(theme, toggled_on()).box ==
                veyra::Srgba8{.red = 57, .green = 120, .blue = 216},
        "clearing restores the built-in checked fill"
    );
}

}

int main() {
    try {
        check_value_and_interaction_precedence();
        check_checked_token_overrides_bump_version();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
