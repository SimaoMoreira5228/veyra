#include "veyra/behaviors/icon_view.hpp"
#include "veyra/widgets/data/icon_view.hpp"

#include <array>
#include <cstdint>
#include <iostream>
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

constexpr std::array<veyra::SoftwarePixel, 4> kLightPixels{{
    {.red = 200, .green = 210, .blue = 220, .alpha = 255},
    {.red = 200, .green = 210, .blue = 220, .alpha = 255},
    {.red = 200, .green = 210, .blue = 220, .alpha = 255},
    {.red = 200, .green = 210, .blue = 220, .alpha = 255},
}};

constexpr std::array<veyra::SoftwarePixel, 4> kDarkPixels{{
    {.red = 20, .green = 24, .blue = 30, .alpha = 255},
    {.red = 20, .green = 24, .blue = 30, .alpha = 255},
    {.red = 20, .green = 24, .blue = 30, .alpha = 255},
    {.red = 20, .green = 24, .blue = 30, .alpha = 255},
}};

constexpr veyra::SoftwareImage kLightImage{
    .width = 2,
    .height = 2,
    .stride = 2,
    .pixels = kLightPixels,
};

constexpr veyra::SoftwareImage kDarkImage{
    .width = 2,
    .height = 2,
    .stride = 2,
    .pixels = kDarkPixels,
};

constexpr veyra::LayoutRect kBox{.x = 10.0F, .y = 20.0F, .width = 32.0F, .height = 32.0F};

const veyra::FillRect& as_fill(const veyra::DisplayCommand& command, std::string_view message) {
    const auto* fill = std::get_if<veyra::FillRect>(&command);

    require(fill != nullptr, message);

    return *fill;
}

const veyra::FillRoundedRect&
as_pill(const veyra::DisplayCommand& command, std::string_view message) {
    const auto* pill = std::get_if<veyra::FillRoundedRect>(&command);

    require(pill != nullptr, message);

    return *pill;
}

const veyra::DrawImage& as_image(const veyra::DisplayCommand& command, std::string_view message) {
    const auto* draw = std::get_if<veyra::DrawImage>(&command);

    require(draw != nullptr, message);

    return *draw;
}

const veyra::StrokeRect&
as_outline(const veyra::DisplayCommand& command, std::string_view message) {
    const auto* outline = std::get_if<veyra::StrokeRect>(&command);

    require(outline != nullptr, message);

    return *outline;
}

void check_appearance_pins_its_tokens() {
    const auto theme = veyra::Theme{};

    const auto plain = veyra::icon_appearance(theme, {});
    require(
        plain.box == theme.color(veyra::tokens::color_surface),
        "a full-color icon waits on a neutral surface box"
    );
    require(
        plain.badge == theme.color(veyra::tokens::color_surface),
        "no badge count leaves the pill color a neutral surface"
    );
    require(
        plain.outline == theme.color(veyra::tokens::color_action_primary),
        "the cursor outline pins the primary action color"
    );

    const auto tinted = veyra::icon_appearance(theme, {.tintable = true});
    require(
        tinted.box == theme.color(veyra::tokens::color_action_disabled),
        "a tintable glyph waits on the disabled shimmer like a list placeholder"
    );
    require(
        tinted.badge == plain.badge && tinted.outline == plain.outline,
        "tintable flips the box only, badge and outline stay put"
    );

    const auto badged = veyra::icon_appearance(theme, {.has_badge = true});
    require(
        badged.badge == theme.color(veyra::tokens::color_error),
        "a live badge paints the error pill like a notification count"
    );
    require(
        badged.box == plain.box && badged.outline == plain.outline,
        "a badge recolors the pill only"
    );
}

void check_theme_override_flows_through() {
    auto theme = veyra::Theme::dark();

    auto tweaks = veyra::Theme{};
    const auto accent = veyra::Srgba8{.red = 10, .green = 200, .blue = 120};
    require(
        tweaks.set(veyra::tokens::color_error, accent) == veyra::ThemeUpdateResult::changed,
        "the overlay takes the scoped tweak"
    );
    require(
        theme.apply(tweaks) == veyra::ThemeUpdateResult::changed,
        "layering the overlay reports a change"
    );

    const auto look = veyra::icon_appearance(theme, {.has_badge = true});
    require(look.badge == accent, "the badge pill follows the scoped theme override");
    require(
        look.outline == theme.color(veyra::tokens::color_action_primary),
        "untweaked tokens keep resolving through the derived theme"
    );
}

void check_pending_emits_a_surface_placeholder() {
    const auto theme = veyra::Theme{};
    const veyra::IconGeometry geometry{.box = kBox};
    std::array<veyra::DisplayCommand, 4> commands{};

    const auto output = veyra::emit_icon(theme, {}, geometry, commands);

    require(output.result == veyra::IconEmitResult::complete, "a pending icon emits");
    require(output.placed == 1 && output.required == 1, "a pending icon costs one fill");
    require(
        output.badge_rect.width == 0.0F && output.badge_rect.height == 0.0F,
        "a pending icon reports no badge rect for the caller text"
    );

    const auto& fill = as_fill(commands[0], "the placeholder is a plain box fill");
    require(
        fill.bounds.x == kBox.x && fill.bounds.y == kBox.y && fill.bounds.width == kBox.width &&
            fill.bounds.height == kBox.height,
        "the placeholder keeps the caller box"
    );
    require(
        fill.color == theme.color(veyra::tokens::color_surface),
        "the placeholder fills with the surface color"
    );

    const auto tinted = veyra::emit_icon(theme, {.tintable = true}, geometry, commands);
    require(tinted.result == veyra::IconEmitResult::complete, "a tintable pending icon emits");
    require(
        as_fill(commands[0], "the tintable placeholder is a box fill").color ==
            theme.color(veyra::tokens::color_action_disabled),
        "a tintable pending icon shimmers with the disabled color"
    );
}

void check_variant_pick_follows_dark_ui() {
    const auto theme = veyra::Theme{};
    const veyra::IconGeometry geometry{.box = kBox};
    std::array<veyra::DisplayCommand, 4> commands{};

    const auto light = veyra::emit_icon(
        theme,
        {.light = &kLightImage, .dark = &kDarkImage, .dark_ui = false},
        geometry,
        commands
    );
    require(light.result == veyra::IconEmitResult::complete, "the light emit completes");
    require(light.placed == 1 && light.required == 1, "a bare icon costs one image command");

    const auto& light_draw = as_image(commands[0], "the icon emits a draw image command");
    require(light_draw.image == &kLightImage, "light ui draws the light variant");
    require(light_draw.opacity == 255, "icons draw fully opaque");
    require(
        light_draw.destination.width == kBox.width && light_draw.destination.height == kBox.height,
        "a square icon contained in a square box fills it"
    );

    const auto dark = veyra::emit_icon(
        theme,
        {.light = &kLightImage, .dark = &kDarkImage, .dark_ui = true},
        geometry,
        commands
    );
    require(dark.result == veyra::IconEmitResult::complete, "the dark emit completes");

    const auto& dark_draw = as_image(commands[0], "the dark emit draws an image");
    require(dark_draw.image == &kDarkImage, "dark ui draws the dark variant");

    const auto fallback =
        veyra::emit_icon(theme, {.light = &kLightImage, .dark_ui = true}, geometry, commands);
    require(fallback.result == veyra::IconEmitResult::complete, "a fallback emit completes");
    require(
        as_image(commands[0], "the fallback emit draws an image").image == &kLightImage,
        "a missing dark variant falls back to light instead of the placeholder"
    );
}

void check_badge_pill_needs_a_count() {
    const auto theme = veyra::Theme{};
    const veyra::IconGeometry geometry{.box = kBox};
    std::array<veyra::DisplayCommand, 4> commands{};

    const auto quiet =
        veyra::emit_icon(theme, {.light = &kLightImage, .badge_count = 0}, geometry, commands);
    require(quiet.result == veyra::IconEmitResult::complete, "a zero badge emits");
    require(quiet.placed == 1 && quiet.required == 1, "a zero badge paints no pill");

    const auto badged =
        veyra::emit_icon(theme, {.light = &kLightImage, .badge_count = 5}, geometry, commands);
    require(badged.result == veyra::IconEmitResult::complete, "a badged icon emits");
    require(badged.placed == 2 && badged.required == 2, "a badge adds one pill command");

    const auto& pill = as_pill(commands[1], "the badge paints after the image");
    require(pill.color == theme.color(veyra::tokens::color_error), "the pill pins the error color");
    require(
        pill.radius == theme.radius(veyra::tokens::radius_small).value,
        "the pill rounds with the small theme radius"
    );
    require(
        pill.bounds.x == badged.badge_rect.x && pill.bounds.y == badged.badge_rect.y &&
            pill.bounds.width == badged.badge_rect.width &&
            pill.bounds.height == badged.badge_rect.height,
        "the reported badge rect matches the painted pill for the caller text"
    );

    const auto expected = veyra::icon_badge_rect(theme, kBox, 5);
    require(
        expected.width == 16.0F && expected.height == 16.0F,
        "a single digit pill sizes from the small space token"
    );
    require(
        expected.x == kBox.x + kBox.width - expected.width - 2.0F && expected.y == kBox.y + 2.0F,
        "the pill anchors inside the top-right corner past extra-small padding"
    );

    const auto wide = veyra::icon_badge_rect(theme, kBox, 150);
    require(wide.width == 32.0F, "an overflow count sizes for its three capped glyphs");
}

void check_badge_text_caps_at_ninety_nine_plus() {
    std::array<char, 4> text{};

    require(veyra::icon_badge_text(0, text) == 0, "a zero badge writes no text");
    require(
        veyra::icon_badge_text(7, text) == 1 && text[0] == '7',
        "a single count writes one digit"
    );
    require(
        veyra::icon_badge_text(42, text) == 2 && text[0] == '4' && text[1] == '2',
        "a double count writes both digits"
    );
    require(
        veyra::icon_badge_text(99, text) == 2 && text[0] == '9' && text[1] == '9',
        "ninety-nine still writes its digits"
    );
    require(
        veyra::icon_badge_text(100, text) == 3 && text[0] == '9' && text[1] == '9' &&
            text[2] == '+',
        "one hundred caps at ninety-nine plus"
    );
    require(
        veyra::icon_badge_text(4000, text) == 3 && text[2] == '+',
        "thousands keep the same cap"
    );

    std::array<char, 2> tight{};
    require(veyra::icon_badge_text(100, tight) == 0, "a span too small for the cap writes nothing");
    require(veyra::icon_badge_text(42, tight) == 2, "a span sized for the digits still fits");
}

void check_selected_draws_the_list_cursor_outline() {
    const auto theme = veyra::Theme{};
    const veyra::IconGeometry geometry{.box = kBox};
    std::array<veyra::DisplayCommand, 4> commands{};

    const auto output = veyra::emit_icon(
        theme,
        {.light = &kLightImage, .badge_count = 5, .selected = true},
        geometry,
        commands
    );

    require(output.result == veyra::IconEmitResult::complete, "a selected icon emits");
    require(output.placed == 3 && output.required == 3, "image, pill, and outline land in order");

    as_image(commands[0], "the icon image paints first");
    as_pill(commands[1], "the badge pill paints second");

    const auto& outline = as_outline(commands[2], "the cursor outline paints last");
    require(
        outline.bounds.x == kBox.x && outline.bounds.y == kBox.y &&
            outline.bounds.width == kBox.width && outline.bounds.height == kBox.height,
        "the outline traces the caller box"
    );
    require(outline.thickness == 2.0F, "the outline matches the list cursor weight");
    require(
        outline.color == theme.color(veyra::tokens::color_action_primary),
        "the outline pins the primary action color"
    );

    const auto pending_selected = veyra::emit_icon(theme, {.selected = true}, geometry, commands);
    require(
        pending_selected.placed == 2 && pending_selected.required == 2,
        "a pending selected icon pairs its placeholder with the outline"
    );
    as_fill(commands[0], "the placeholder still paints first");
    as_outline(commands[1], "the outline still traces the box");
}

void check_invalid_geometry_and_pixels_reject() {
    const auto theme = veyra::Theme{};
    std::array<veyra::DisplayCommand, 4> commands{};

    const veyra::IconGeometry flat{.box = {.x = 0.0F, .y = 0.0F, .width = 0.0F, .height = 32.0F}};
    const auto flat_out = veyra::emit_icon(theme, {.light = &kLightImage}, flat, commands);
    require(
        flat_out.result == veyra::IconEmitResult::invalid_input && flat_out.placed == 0 &&
            flat_out.required == 0,
        "a zero box rejects without touching the span"
    );

    const veyra::IconGeometry negative{
        .box = {.x = 0.0F, .y = 0.0F, .width = -4.0F, .height = 32.0F},
    };
    require(
        veyra::emit_icon(theme, {.light = &kLightImage}, negative, commands).result ==
            veyra::IconEmitResult::invalid_input,
        "a negative box rejects"
    );

    const veyra::IconGeometry wild{
        .box = {.x = 0.0F, .y = 0.0F, .width = 32.0F, .height = 32.0F},
        .fit = static_cast<veyra::ImageFitMode>(99),
    };
    require(
        veyra::emit_icon(theme, {.light = &kLightImage}, wild, commands).result ==
            veyra::IconEmitResult::invalid_input,
        "an unknown fit mode rejects"
    );

    const std::array<veyra::SoftwarePixel, 1> pixel{{{.red = 1}}};
    const veyra::SoftwareImage empty_image{
        .width = 0,
        .height = 0,
        .stride = 0,
        .pixels = pixel,
    };
    const veyra::IconGeometry geometry{.box = kBox};
    require(
        veyra::emit_icon(theme, {.light = &empty_image}, geometry, commands).result ==
            veyra::IconEmitResult::invalid_input,
        "a zero-size image rejects instead of fitting nothing"
    );
}

void check_overflow_reports_required_and_prefix() {
    const auto theme = veyra::Theme{};
    const veyra::IconGeometry geometry{.box = kBox};
    const veyra::IconContent content{
        .light = &kLightImage,
        .badge_count = 12,
        .selected = true,
    };

    std::array<veyra::DisplayCommand, 4> full{};
    const auto complete = veyra::emit_icon(theme, content, geometry, full);
    require(
        complete.result == veyra::IconEmitResult::complete && complete.placed == 3,
        "the reference emit fits its span"
    );

    std::array<veyra::DisplayCommand, 2> tight{};
    const auto output = veyra::emit_icon(theme, content, geometry, tight);

    require(
        output.result == veyra::IconEmitResult::insufficient_output,
        "a short span reports its overflow"
    );
    require(output.placed == 2 && output.required == 3, "the report keeps placed and required");
    require(
        tight[0].index() == full[0].index() && tight[1].index() == full[1].index(),
        "the short span keeps a whole-command prefix of the full emit"
    );
    require(
        output.badge_rect.width == complete.badge_rect.width,
        "the overflow still reports the badge rect for a retry"
    );

    std::array<veyra::DisplayCommand, 0> bare{};
    const auto starved = veyra::emit_icon(theme, content, geometry, bare);
    require(
        starved.result == veyra::IconEmitResult::insufficient_output && starved.placed == 0 &&
            starved.required == 3,
        "an empty span reports the full need with nothing placed"
    );
}

void check_registry_flows_into_emit() {
    veyra::IconRegistry registry;

    require(registry.register_icon(21, "inbox", true), "the icon registers");
    require(registry.pending(21), "the fresh icon pends its fetch");
    require(registry.needs_image(21), "the loader poll fires for the fresh icon");

    const auto theme = veyra::Theme{};
    const veyra::IconGeometry geometry{.box = kBox};
    std::array<veyra::DisplayCommand, 4> commands{};

    const veyra::SoftwareImage* missing = reinterpret_cast<const veyra::SoftwareImage*>(1);
    require(
        registry.image_for(21, false, missing) && missing == nullptr,
        "no variants resolve null before the fetch lands"
    );

    const auto waiting = veyra::emit_icon(theme, {.tintable = true}, geometry, commands);
    require(
        waiting.result == veyra::IconEmitResult::complete && waiting.placed == 1,
        "the pending icon emits its placeholder"
    );
    require(
        std::holds_alternative<veyra::FillRect>(commands[0]),
        "the pending emit holds a box fill, never an image"
    );

    require(registry.set_image(21, &kLightImage, &kDarkImage), "the fetched variants land");
    require(!registry.pending(21), "landed pixels clear the pending state");
    require(!registry.needs_image(21), "landed pixels quiet the loader poll");
    require(registry.set_badge(21, 3), "the unread count lands");

    auto record = veyra::IconRecord{};
    require(registry.record_for(21, record), "the composed icon reads back");

    const veyra::SoftwareImage* resolved = nullptr;
    require(registry.image_for(21, true, resolved), "the dark variant resolves");

    const auto output = veyra::emit_icon(
        theme,
        {
            .light = record.light,
            .dark = record.dark,
            .dark_ui = true,
            .tintable = record.tintable,
            .badge_count = record.badge_count,
            .selected = true,
        },
        geometry,
        commands
    );

    require(output.result == veyra::IconEmitResult::complete, "the composed icon emits");
    require(output.placed == 3 && output.required == 3, "image, pill, and outline land");
    require(
        as_image(commands[0], "the composed emit draws an image").image == resolved,
        "the emit draws the resolved registry variant"
    );
    require(resolved == &kDarkImage, "dark ui draws the dark bytes the loader fetched");
}

}

int main() {
    try {
        check_appearance_pins_its_tokens();
        check_theme_override_flows_through();
        check_pending_emits_a_surface_placeholder();
        check_variant_pick_follows_dark_ui();
        check_badge_pill_needs_a_count();
        check_badge_text_caps_at_ninety_nine_plus();
        check_selected_draws_the_list_cursor_outline();
        check_invalid_geometry_and_pixels_reject();
        check_overflow_reports_required_and_prefix();
        check_registry_flows_into_emit();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
