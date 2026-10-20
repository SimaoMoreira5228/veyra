#include "veyra/widgets/overlay/toast_card.hpp"

#include <array>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

const veyra::FillRect& as_fill(const veyra::DisplayCommand& command, std::string_view message) {
    const auto* fill = std::get_if<veyra::FillRect>(&command);

    require(fill != nullptr, message);

    return *fill;
}

const veyra::StrokeRect&
as_outline(const veyra::DisplayCommand& command, std::string_view message) {
    const auto* outline = std::get_if<veyra::StrokeRect>(&command);

    require(outline != nullptr, message);

    return *outline;
}

bool same_rect(veyra::LayoutRect rect, float x, float y, float width, float height) {
    return rect.x == x && rect.y == y && rect.width == width && rect.height == height;
}

bool same_command(const veyra::DisplayCommand& left, const veyra::DisplayCommand& right) {
    if (left.index() != right.index()) {
        return false;
    }

    if (const auto* fill = std::get_if<veyra::FillRect>(&left)) {
        const auto& other = std::get<veyra::FillRect>(right);

        return fill->bounds.x == other.bounds.x && fill->bounds.y == other.bounds.y &&
            fill->bounds.width == other.bounds.width &&
            fill->bounds.height == other.bounds.height && fill->color == other.color;
    }

    const auto& edge = std::get<veyra::StrokeRect>(left);
    const auto& other = std::get<veyra::StrokeRect>(right);

    return edge.bounds.x == other.bounds.x && edge.bounds.y == other.bounds.y &&
        edge.bounds.width == other.bounds.width && edge.bounds.height == other.bounds.height &&
        edge.thickness == other.thickness && edge.color == other.color;
}

void check_appearance_maps_kind_to_accent() {
    const auto theme = veyra::Theme{};

    const auto info = veyra::toast_card_appearance(theme, {.kind = veyra::ToastKind::info});
    require(
        info.accent == theme.color(veyra::tokens::color_action_primary),
        "an info card accents with the primary token"
    );
    require(
        info.accent == veyra::Srgba8{.red = 57, .green = 120, .blue = 216},
        "the info accent pins the default primary"
    );

    const auto warning = veyra::toast_card_appearance(theme, {.kind = veyra::ToastKind::warning});
    require(
        warning.accent == theme.color(veyra::tokens::color_action_hovered),
        "a warning card accents with the hovered token, the set has no amber"
    );
    require(
        warning.accent == veyra::Srgba8{.red = 48, .green = 105, .blue = 190},
        "the warning accent pins the default hovered blue"
    );

    const auto error =
        veyra::toast_card_appearance(theme, {.kind = veyra::ToastKind::error, .assertive = true});
    require(
        error.accent == theme.color(veyra::tokens::color_error),
        "an error card accents with the error token"
    );
    require(
        error.accent == veyra::Srgba8{.red = 176, .green = 0, .blue = 32},
        "the error accent pins the default error red"
    );

    require(
        info.background == theme.color(veyra::tokens::color_surface) &&
            info.border == theme.color(veyra::tokens::color_text_disabled) &&
            info.text == theme.color(veyra::tokens::color_text_primary),
        "every card shares the surface panel, the muted border, and the primary text"
    );
    require(
        warning.background == info.background && warning.border == info.border &&
            warning.text == info.text && error.background == info.background,
        "only the accent strip moves with the kind"
    );
}

void check_assertive_leaves_chrome_alone() {
    const auto theme = veyra::Theme{};

    const auto polite = veyra::toast_card_appearance(theme, {.kind = veyra::ToastKind::error});
    const auto loud =
        veyra::toast_card_appearance(theme, {.kind = veyra::ToastKind::error, .assertive = true});
    require(
        loud.accent == polite.accent && loud.background == polite.background &&
            loud.border == polite.border && loud.text == polite.text,
        "assertive is a live-region hint, the chrome stays identical"
    );

    constexpr veyra::LayoutRect card{.x = 8.0F, .y = 8.0F, .width = 240.0F, .height = 56.0F};
    std::array<veyra::DisplayCommand, 3> first{};
    std::array<veyra::DisplayCommand, 3> second{};

    const auto quiet =
        veyra::emit_toast_card(theme, {.kind = veyra::ToastKind::info}, {card}, first);
    const auto urgent = veyra::emit_toast_card(
        theme,
        {.kind = veyra::ToastKind::info, .assertive = true},
        {card},
        second
    );
    require(
        quiet.result == veyra::ToastCardEmitResult::complete &&
            urgent.result == veyra::ToastCardEmitResult::complete,
        "both politeness flavors emit"
    );
    require(
        same_command(first[0], second[0]) && same_command(first[1], second[1]) &&
            same_command(first[2], second[2]),
        "the assertive flag paints nothing extra"
    );
}

void check_emit_pins_panel_accent_and_border() {
    const auto theme = veyra::Theme{};
    constexpr veyra::LayoutRect card{.x = 10.0F, .y = 20.0F, .width = 240.0F, .height = 56.0F};
    std::array<veyra::DisplayCommand, 3> storage{};

    const auto output =
        veyra::emit_toast_card(theme, {.kind = veyra::ToastKind::warning}, {.card = card}, storage);
    require(
        output.result == veyra::ToastCardEmitResult::complete && output.placed == 3 &&
            output.required == 3,
        "a sized span fits panel, accent, and border"
    );

    const auto& panel = as_fill(storage[0], "the panel fill leads");
    require(
        same_rect(panel.bounds, 10.0F, 20.0F, 240.0F, 56.0F) &&
            panel.color == theme.color(veyra::tokens::color_surface),
        "the panel pins the card rect in the surface color"
    );

    const auto& accent = as_fill(storage[1], "the accent strip trails the panel");
    require(
        same_rect(accent.bounds, 10.0F, 20.0F, 4.0F, 56.0F) &&
            accent.color == theme.color(veyra::tokens::color_action_hovered),
        "the accent hugs the left edge, four wide and full height"
    );

    const auto& border = as_outline(storage[2], "the border trails the accent");
    require(
        same_rect(border.bounds, 10.0F, 20.0F, 240.0F, 56.0F) && border.thickness == 1.0F &&
            border.color == theme.color(veyra::tokens::color_text_disabled),
        "the border pins the card rect at one pixel in the muted color"
    );
}

void check_narrow_card_clamps_the_accent() {
    const auto theme = veyra::Theme{};
    constexpr veyra::LayoutRect card{.x = 0.0F, .y = 0.0F, .width = 2.0F, .height = 24.0F};
    std::array<veyra::DisplayCommand, 3> storage{};

    const auto output = veyra::emit_toast_card(theme, {}, {.card = card}, storage);
    require(
        output.result == veyra::ToastCardEmitResult::complete,
        "a card narrower than the strip still emits"
    );
    require(
        same_rect(as_fill(storage[1], "the clamped strip trails").bounds, 0.0F, 0.0F, 2.0F, 24.0F),
        "the accent never overflows a narrow card"
    );
}

void check_stacking_stays_independent() {
    const auto theme = veyra::Theme{};
    constexpr veyra::LayoutRect top{.x = 10.0F, .y = 10.0F, .width = 240.0F, .height = 56.0F};
    constexpr veyra::LayoutRect bottom{.x = 10.0F, .y = 74.0F, .width = 240.0F, .height = 56.0F};
    std::array<veyra::DisplayCommand, 3> first{};
    std::array<veyra::DisplayCommand, 3> second{};

    const auto info_out =
        veyra::emit_toast_card(theme, {.kind = veyra::ToastKind::info}, {.card = top}, first);
    const auto error_out =
        veyra::emit_toast_card(theme, {.kind = veyra::ToastKind::error}, {.card = bottom}, second);
    require(
        info_out.result == veyra::ToastCardEmitResult::complete &&
            error_out.result == veyra::ToastCardEmitResult::complete,
        "both stacked cards emit"
    );
    require(
        same_rect(as_fill(first[0], "the top panel leads").bounds, 10.0F, 10.0F, 240.0F, 56.0F) &&
            same_rect(
                as_fill(second[0], "the bottom panel leads").bounds,
                10.0F,
                74.0F,
                240.0F,
                56.0F
            ),
        "each emit pins its own rect, the caller stacks by offsetting"
    );
    require(
        as_fill(first[1], "the top accent trails").bounds.y == 10.0F &&
            as_fill(second[1], "the bottom accent trails").bounds.y == 74.0F &&
            as_fill(first[1], "the top accent trails").color !=
                as_fill(second[1], "the bottom accent trails").color,
        "each emit keeps its own strip color and row"
    );

    std::array<veyra::DisplayCommand, 3> repeat{};
    const auto rerun =
        veyra::emit_toast_card(theme, {.kind = veyra::ToastKind::info}, {.card = top}, repeat);
    require(
        rerun.result == veyra::ToastCardEmitResult::complete && same_command(first[0], repeat[0]) &&
            same_command(first[1], repeat[1]) && same_command(first[2], repeat[2]),
        "re-emitting a card replays identical commands, no hidden state leaks"
    );
}

void check_overflow_keeps_a_whole_prefix() {
    const auto theme = veyra::Theme{};
    constexpr veyra::LayoutRect card{.x = 0.0F, .y = 0.0F, .width = 240.0F, .height = 56.0F};

    std::array<veyra::DisplayCommand, 3> full{};
    const auto complete = veyra::emit_toast_card(theme, {}, {.card = card}, full);
    require(
        complete.result == veyra::ToastCardEmitResult::complete && complete.placed == 3,
        "the reference emit fits its span"
    );

    std::array<veyra::DisplayCommand, 2> tight{};
    const auto starved = veyra::emit_toast_card(theme, {}, {.card = card}, tight);
    require(
        starved.result == veyra::ToastCardEmitResult::insufficient_output && starved.placed == 2 &&
            starved.required == 3,
        "a short span keeps panel plus accent and reports the missing border"
    );
    require(
        same_command(tight[0], full[0]) && same_command(tight[1], full[1]),
        "the short span keeps a whole-command prefix of the full emit"
    );

    std::array<veyra::DisplayCommand, 0> empty{};
    const auto bare = veyra::emit_toast_card(theme, {}, {.card = card}, empty);
    require(
        bare.result == veyra::ToastCardEmitResult::insufficient_output && bare.placed == 0 &&
            bare.required == 3,
        "a card with no room still reports all three commands missing"
    );
}

void check_invalid_geometry_rejects_loudly() {
    const auto theme = veyra::Theme{};
    std::array<veyra::DisplayCommand, 3> storage{};

    for (const auto card :
         {veyra::LayoutRect{.x = 0.0F, .y = 0.0F, .width = 0.0F, .height = 56.0F},
          veyra::LayoutRect{.x = 0.0F, .y = 0.0F, .width = 240.0F, .height = 0.0F},
          veyra::LayoutRect{
              .x = 0.0F,
              .y = 0.0F,
              .width = -10.0F,
              .height = 56.0F,
          },
          veyra::LayoutRect{
              .x = std::numeric_limits<float>::quiet_NaN(),
              .y = 0.0F,
              .width = 240.0F,
              .height = 56.0F,
          }}) {
        const auto rejected = veyra::emit_toast_card(theme, {}, {.card = card}, storage);
        require(
            rejected.result == veyra::ToastCardEmitResult::invalid_input && rejected.placed == 0 &&
                rejected.required == 0,
            "a degenerate card rect rejects with nothing placed"
        );
    }
}

void check_theme_override_repaints_chrome() {
    auto theme = veyra::Theme::dark();

    auto tweaks = veyra::Theme{};
    require(
        tweaks.set(veyra::tokens::color_surface, veyra::Srgba8{16, 18, 24}) ==
            veyra::ThemeUpdateResult::changed,
        "the overlay takes the panel tweak"
    );
    require(
        tweaks.set(veyra::tokens::color_error, veyra::Srgba8{255, 90, 90}) ==
            veyra::ThemeUpdateResult::changed,
        "the overlay takes the error tweak"
    );
    require(
        tweaks.set(veyra::tokens::color_action_primary, veyra::Srgba8{90, 160, 255}) ==
            veyra::ThemeUpdateResult::changed,
        "the overlay takes the primary tweak"
    );
    require(
        theme.apply(tweaks) == veyra::ThemeUpdateResult::changed,
        "layering the overlay reports a change"
    );

    const auto error = veyra::toast_card_appearance(theme, {.kind = veyra::ToastKind::error});
    require(
        error.background == veyra::Srgba8{16, 18, 24} && error.accent == veyra::Srgba8{255, 90, 90},
        "the error card follows the overridden surface and error tokens"
    );
    require(
        error.border == theme.color(veyra::tokens::color_text_disabled) &&
            error.text == theme.color(veyra::tokens::color_text_primary),
        "untweaked tokens keep resolving through the derived theme"
    );

    constexpr veyra::LayoutRect card{.x = 0.0F, .y = 0.0F, .width = 240.0F, .height = 56.0F};
    std::array<veyra::DisplayCommand, 3> storage{};
    const auto output =
        veyra::emit_toast_card(theme, {.kind = veyra::ToastKind::info}, {.card = card}, storage);
    require(
        output.result == veyra::ToastCardEmitResult::complete &&
            as_fill(storage[0], "the themed panel leads").color == veyra::Srgba8{16, 18, 24} &&
            as_fill(storage[1], "the themed accent trails").color == veyra::Srgba8{90, 160, 255},
        "the emitted panel and accent track the overridden tokens"
    );
}

}

int main() {
    try {
        check_appearance_maps_kind_to_accent();
        check_assertive_leaves_chrome_alone();
        check_emit_pins_panel_accent_and_border();
        check_narrow_card_clamps_the_accent();
        check_stacking_stays_independent();
        check_overflow_keeps_a_whole_prefix();
        check_invalid_geometry_rejects_loudly();
        check_theme_override_repaints_chrome();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
