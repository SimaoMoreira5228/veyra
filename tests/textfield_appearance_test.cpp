#include "veyra/widgets/basic/textfield.hpp"

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

void check_disabled_precedence() {
    const auto theme = veyra::Theme{};
    const auto state = veyra::EditableTextState{
        .focused = true,
        .disabled = true,
        .invalid = true,
    };
    const auto appearance = veyra::textfield_appearance(theme, state);

    require(
        appearance.box_fill == theme.color(veyra::tokens::color_action_disabled) &&
            appearance.border == theme.color(veyra::tokens::color_action_disabled) &&
            appearance.text == theme.color(veyra::tokens::color_text_disabled),
        "disabled styling wins over focus and invalid"
    );
}

void check_invalid_distinct_from_focus() {
    const auto theme = veyra::Theme{};

    const auto invalid = veyra::textfield_appearance(theme, {.focused = true, .invalid = true});
    require(
        invalid.border == theme.color(veyra::tokens::color_error) &&
            invalid.caret == theme.color(veyra::tokens::color_error),
        "invalid beats focus on the border and caret"
    );
    require(
        invalid.box_fill == theme.color(veyra::tokens::color_surface) &&
            invalid.text == theme.color(veyra::tokens::color_text_primary),
        "invalid keeps the idle fill and text, only the outline turns"
    );

    const auto focused = veyra::textfield_appearance(theme, {.focused = true});
    require(
        focused.border == theme.color(veyra::tokens::color_action_primary) &&
            focused.caret == theme.color(veyra::tokens::color_action_primary),
        "focus resolves the action border and caret"
    );

    const auto idle = veyra::textfield_appearance(theme, {});
    require(
        idle.border == theme.color(veyra::tokens::color_text_disabled) &&
            idle.placeholder == theme.color(veyra::tokens::color_text_disabled) &&
            idle.text == theme.color(veyra::tokens::color_text_primary),
        "an idle field uses the muted border with primary text"
    );
    require(
        idle.corner_radius == theme.radius(veyra::tokens::radius_small) &&
            idle.horizontal_padding == theme.space(veyra::tokens::space_small),
        "field geometry resolves through the theme"
    );
}

void check_error_token_overrides_bump_version() {
    auto theme = veyra::Theme{};
    const auto initial_version = theme.version();
    const auto error = veyra::Srgba8{.red = 200, .green = 20, .blue = 40};

    require(
        theme.color(veyra::tokens::color_error) ==
            veyra::Srgba8{.red = 176, .green = 0, .blue = 32},
        "the error token ships a built-in default"
    );
    require(
        theme.set(veyra::tokens::color_error, error) == veyra::ThemeUpdateResult::changed,
        "a new error override reports a change"
    );
    require(theme.version() != initial_version, "a changed theme advances its version");
    require(
        veyra::textfield_appearance(theme, {.invalid = true}).border == error,
        "the error override resolves into the recipe"
    );
    require(
        theme.clear(veyra::tokens::color_error) == veyra::ThemeUpdateResult::changed &&
            veyra::textfield_appearance(theme, {.invalid = true}).border ==
                veyra::Srgba8{.red = 176, .green = 0, .blue = 32},
        "clearing restores the built-in error color"
    );
}

}

int main() {
    try {
        check_disabled_precedence();
        check_invalid_distinct_from_focus();
        check_error_token_overrides_bump_version();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
