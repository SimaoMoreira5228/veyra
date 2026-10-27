#include "veyra/behaviors/navigation/menu.hpp"
#include "veyra/behaviors/navigation/menubar.hpp"
#include "veyra/widgets/data/menu_list.hpp"
#include "veyra/widgets/data/menubar.hpp"

#include <array>
#include <iostream>
#include <limits>
#include <span>
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

bool same_rect(veyra::LayoutRect rect, float x, float y, float width, float height) {
    return rect.x == x && rect.y == y && rect.width == width && rect.height == height;
}

void check_appearance_maps_label_states() {
    const auto theme = veyra::Theme{};

    const auto plain = veyra::menubar_appearance(theme, {});
    require(
        plain.background == theme.color(veyra::tokens::color_surface) &&
            plain.item_text == theme.color(veyra::tokens::color_text_primary) &&
            plain.active_background == theme.color(veyra::tokens::color_surface) &&
            plain.separator == theme.color(veyra::tokens::color_text_disabled),
        "a quiet label rests on the surface with primary text and a muted rule"
    );

    const auto active = veyra::menubar_appearance(theme, {.active = true});
    require(
        active.active_background == theme.color(veyra::tokens::color_action_hovered) &&
            active.item_text == theme.color(veyra::tokens::color_text_primary),
        "an active label fills with the hovered action color"
    );

    const auto gated = veyra::menubar_appearance(theme, {.disabled = true});
    require(
        gated.item_text == theme.color(veyra::tokens::color_text_disabled) &&
            gated.active_background == theme.color(veyra::tokens::color_surface),
        "a disabled label mutes its text and takes no fill"
    );

    const auto held = veyra::menubar_appearance(theme, {.active = true, .disabled = true});
    require(
        held.active_background == theme.color(veyra::tokens::color_surface),
        "a disabled label takes no fill even while flagged active"
    );
}

void check_controller_state_drives_labels() {
    veyra::MenubarController bar;
    constexpr std::array<veyra::MenubarTitle, 3> titles{{
        {.id = 1},
        {.id = 2},
        {.id = 3},
    }};
    require(
        bar.enter_strip(titles).result == veyra::MenubarResult::changed,
        "enter arms the first title"
    );
    require(
        bar.move(veyra::MenubarMove::next, titles).result == veyra::MenubarResult::changed,
        "roving steps onto the second title"
    );
    require(
        bar.open_current(titles).result == veyra::MenubarResult::opened,
        "opening arms the dropdown on the focused title"
    );

    std::array<veyra::MenubarLabelState, 3> labels{};
    for (auto index = std::size_t{}; index < titles.size(); ++index) {
        labels[index].active = bar.has_focus() && bar.focus() == titles[index].id;
        labels[index].disabled = titles[index].disabled;
    }
    require(
        !labels[0].active && labels[1].active && !labels[2].active,
        "only the open title glows"
    );

    const auto theme = veyra::Theme{};
    constexpr std::array<float, 3> widths{{60.0F, 60.0F, 60.0F}};
    const veyra::MenubarGeometry geometry{
        .strip_bounds = {.x = 0.0F, .y = 0.0F, .width = 180.0F, .height = 24.0F},
        .label_widths = std::span<const float>{widths},
    };
    std::array<veyra::DisplayCommand, 8> commands{};

    const auto output = veyra::emit_menubar(theme, labels, geometry, commands);

    require(output.result == veyra::MenubarEmitResult::complete, "the composed emit completes");
    require(output.placed == 3 && output.required == 3, "band plus one pick plus the rule");

    const auto& band = as_fill(commands[0], "the strip paints its band first");
    require(
        same_rect(band.bounds, 0.0F, 0.0F, 180.0F, 24.0F) &&
            band.color == theme.color(veyra::tokens::color_surface),
        "the composed band pins the strip rect and the surface color"
    );

    const auto& picked = as_fill(commands[1], "the open title paints its pick next");
    require(
        same_rect(picked.bounds, 60.0F, 0.0F, 60.0F, 24.0F) &&
            picked.color == theme.color(veyra::tokens::color_action_hovered),
        "the composed pick pins the second cell and the hovered color"
    );
}

void check_emit_pins_geometry_and_colors() {
    const auto theme = veyra::Theme{};
    constexpr std::array<veyra::MenubarLabelState, 3> labels{{
        {.active = true},
        {.disabled = true},
        {.active = true, .disabled = true},
    }};
    constexpr std::array<float, 3> widths{{50.0F, 80.0F, 70.0F}};
    const veyra::MenubarGeometry geometry{
        .strip_bounds = {.x = 10.0F, .y = 20.0F, .width = 200.0F, .height = 28.0F},
        .label_widths = std::span<const float>{widths},
    };
    std::array<veyra::DisplayCommand, 8> commands{};

    const auto output = veyra::emit_menubar(theme, labels, geometry, commands);

    require(output.result == veyra::MenubarEmitResult::complete, "a sized span completes");
    require(
        output.placed == 3 && output.required == 3,
        "one band, one pick, one rule: the gated cell paints nothing"
    );
    require(
        veyra::menubar_required_commands(labels) == 3,
        "the capacity helper matches the full emit"
    );

    const auto& band = as_fill(commands[0], "the band opens the emit");
    require(
        same_rect(band.bounds, 10.0F, 20.0F, 200.0F, 28.0F) &&
            band.color == theme.color(veyra::tokens::color_surface),
        "the band pins the strip rect and the surface color"
    );

    const auto& picked = as_fill(commands[1], "the active cell fills next");
    require(
        same_rect(picked.bounds, 10.0F, 20.0F, 50.0F, 28.0F) &&
            picked.color == theme.color(veyra::tokens::color_action_hovered),
        "the pick pins the first cell and the hovered color"
    );

    const auto& rule = as_fill(commands[2], "the rule closes the emit");
    require(
        same_rect(rule.bounds, 10.0F, 47.0F, 200.0F, 1.0F) &&
            rule.color == theme.color(veyra::tokens::color_text_disabled),
        "the rule pins the strip foot and the muted color"
    );
}

void check_emit_carries_no_text() {
    const auto theme = veyra::Theme{};
    constexpr std::array<veyra::MenubarLabelState, 2> labels{{
        {.active = true},
        {.disabled = true},
    }};
    constexpr std::array<float, 2> widths{{90.0F, 70.0F}};
    const veyra::MenubarGeometry geometry{
        .strip_bounds = {.x = 0.0F, .y = 0.0F, .width = 160.0F, .height = 24.0F},
        .label_widths = std::span<const float>{widths},
    };
    std::array<veyra::DisplayCommand, 8> commands{};

    const auto output = veyra::emit_menubar(theme, labels, geometry, commands);

    require(output.result == veyra::MenubarEmitResult::complete, "the text-free emit completes");

    for (auto index = std::size_t{}; index < output.placed; ++index) {
        require(
            std::get_if<veyra::FillRect>(&commands[index]) != nullptr,
            "every command paints chrome, never a text or mask run"
        );
    }
}

void check_emit_reports_overflow_without_silent_truncation() {
    const auto theme = veyra::Theme{};
    constexpr std::array<veyra::MenubarLabelState, 3> labels{{
        {.active = true},
        {},
        {.active = true},
    }};
    constexpr std::array<float, 3> widths{{50.0F, 80.0F, 70.0F}};
    const veyra::MenubarGeometry geometry{
        .strip_bounds = {.x = 10.0F, .y = 20.0F, .width = 200.0F, .height = 28.0F},
        .label_widths = std::span<const float>{widths},
    };
    std::array<veyra::DisplayCommand, 8> full{};
    const auto complete = veyra::emit_menubar(theme, labels, geometry, full);
    require(
        complete.result == veyra::MenubarEmitResult::complete && complete.placed == 4 &&
            complete.required == 4,
        "the reference emit fits its span"
    );

    std::array<veyra::DisplayCommand, 1> tight{};
    const auto output = veyra::emit_menubar(theme, labels, geometry, tight);

    require(
        output.result == veyra::MenubarEmitResult::insufficient_output,
        "a short span reports its overflow"
    );
    require(output.placed == 1 && output.required == 4, "the report keeps placed and required");

    const auto& tight_band = as_fill(tight[0], "the short span keeps the band");
    const auto& full_band = as_fill(full[0], "the reference span opens with the band");
    require(
        tight_band.color == full_band.color &&
            same_rect(
                tight_band.bounds,
                full_band.bounds.x,
                full_band.bounds.y,
                full_band.bounds.width,
                full_band.bounds.height
            ),
        "the short span keeps a whole-command prefix of the full emit"
    );
}

void check_theme_overlay_flows_through() {
    auto theme = veyra::Theme::dark();

    auto tweaks = veyra::Theme{};
    const auto accent = veyra::Srgba8{.red = 10, .green = 200, .blue = 120};
    require(
        tweaks.set(veyra::tokens::color_action_hovered, accent) ==
            veyra::ThemeUpdateResult::changed,
        "the overlay takes the scoped tweak"
    );
    require(
        theme.apply(tweaks) == veyra::ThemeUpdateResult::changed,
        "layering the overlay reports a change"
    );

    const auto active = veyra::menubar_appearance(theme, {.active = true});
    require(
        active.active_background == accent,
        "the active fill follows the scoped theme override"
    );
    require(
        active.separator == theme.color(veyra::tokens::color_text_disabled),
        "untweaked tokens keep resolving through the derived theme"
    );

    constexpr std::array<veyra::MenubarLabelState, 1> labels{{{.active = true}}};
    constexpr std::array<float, 1> widths{{100.0F}};
    const veyra::MenubarGeometry geometry{
        .strip_bounds = {.x = 0.0F, .y = 0.0F, .width = 100.0F, .height = 24.0F},
        .label_widths = std::span<const float>{widths},
    };
    std::array<veyra::DisplayCommand, 4> commands{};

    const auto output = veyra::emit_menubar(theme, labels, geometry, commands);

    require(output.result == veyra::MenubarEmitResult::complete, "the dark emit completes");

    const auto& picked = as_fill(commands[1], "the dark pick trails the band");
    require(picked.color == accent, "the emitted pick tracks the overridden hover");
    const auto& rule = as_fill(commands[2], "the dark rule trails the pick");
    require(
        rule.color == theme.color(veyra::tokens::color_text_disabled),
        "the emitted rule tracks the derived muted color"
    );
}

void check_open_menu_composition() {
    veyra::MenubarController bar;
    constexpr std::array<veyra::MenubarTitle, 3> titles{{
        {.id = 1},
        {.id = 2},
        {.id = 3},
    }};
    require(
        bar.enter_strip(titles).result == veyra::MenubarResult::changed,
        "the strip takes focus"
    );
    require(
        bar.open_current(titles).result == veyra::MenubarResult::opened && bar.open() == 1,
        "the first title opens its dropdown"
    );

    veyra::MenuController menu;
    constexpr std::array<veyra::MenuItem, 3> items{{
        {.id = 10},
        {.id = 11, .kind = veyra::MenuItemKind::separator},
        {.id = 12},
    }};
    require(menu.open_root(1, items) == veyra::MenuResult::opened, "the dropdown opens its root");

    std::array<veyra::MenubarLabelState, 3> labels{};
    for (auto index = std::size_t{}; index < titles.size(); ++index) {
        labels[index].active = bar.has_focus() && bar.focus() == titles[index].id;
        labels[index].disabled = titles[index].disabled;
    }

    const auto cursor = menu.active();
    std::array<veyra::MenuListRowState, 3> rows{};
    for (auto index = std::size_t{}; index < items.size(); ++index) {
        rows[index].active = cursor.has && cursor.id == items[index].id;
        rows[index].disabled = items[index].disabled;
        rows[index].is_separator = items[index].kind == veyra::MenuItemKind::separator;
    }

    const auto theme = veyra::Theme{};
    constexpr std::array<float, 3> widths{{60.0F, 60.0F, 60.0F}};
    const veyra::MenubarGeometry strip{
        .strip_bounds = {.x = 0.0F, .y = 0.0F, .width = 180.0F, .height = 24.0F},
        .label_widths = std::span<const float>{widths},
    };
    std::array<veyra::DisplayCommand, 8> strip_commands{};
    const auto strip_output = veyra::emit_menubar(theme, labels, strip, strip_commands);
    require(strip_output.result == veyra::MenubarEmitResult::complete, "the strip emit completes");
    require(strip_output.placed == 3, "band plus the open pick plus the rule");

    const veyra::MenuListGeometry drop{
        .list_bounds = {.x = 0.0F, .y = 24.0F, .width = 160.0F, .height = 72.0F},
        .row_height = 24.0F,
    };
    std::array<veyra::DisplayCommand, 8> drop_commands{};
    const auto drop_output = veyra::emit_menu_list(theme, rows, drop, drop_commands);
    require(drop_output.result == veyra::MenuListEmitResult::complete, "the drop emit completes");
    require(drop_output.placed == 2, "one pick plus the separator rule");

    const auto& strip_pick = as_fill(strip_commands[1], "the open title paints its pick");
    const auto& drop_pick = as_fill(drop_commands[0], "the open row paints its pick");
    require(
        strip_pick.color == drop_pick.color &&
            strip_pick.color == theme.color(veyra::tokens::color_action_hovered),
        "strip and dropdown share one highlight color without sharing chrome code"
    );
    require(
        same_rect(strip_pick.bounds, 0.0F, 0.0F, 60.0F, 24.0F),
        "the open title owns the first strip cell"
    );
    require(
        same_rect(drop_pick.bounds, 0.0F, 24.0F, 160.0F, 24.0F),
        "the dropdown opens flush under the strip"
    );
}

void check_invalid_geometry_rejected() {
    const auto theme = veyra::Theme{};
    constexpr std::array<veyra::MenubarLabelState, 2> labels{{{.active = true}, {}}};
    constexpr std::array<float, 2> widths{{50.0F, 80.0F}};
    std::array<veyra::DisplayCommand, 4> commands{};

    const veyra::MenubarGeometry flat{
        .strip_bounds = {.x = 0.0F, .y = 0.0F, .width = 130.0F, .height = 0.0F},
        .label_widths = std::span<const float>{widths},
    };
    const auto flat_output = veyra::emit_menubar(theme, labels, flat, commands);
    require(
        flat_output.result == veyra::MenubarEmitResult::invalid_input && flat_output.placed == 0 &&
            flat_output.required == 0,
        "a zero-height strip rejects the emit without touching the span"
    );

    const veyra::MenubarGeometry fluid{
        .strip_bounds =
            {.x = std::numeric_limits<float>::quiet_NaN(),
             .y = 0.0F,
             .width = 130.0F,
             .height = 24.0F},
        .label_widths = std::span<const float>{widths},
    };
    const auto fluid_output = veyra::emit_menubar(theme, labels, fluid, commands);
    require(
        fluid_output.result == veyra::MenubarEmitResult::invalid_input &&
            fluid_output.placed == 0 && fluid_output.required == 0,
        "a fluid strip origin rejects the emit"
    );

    constexpr std::array<float, 2> owing{{50.0F, -4.0F}};
    const veyra::MenubarGeometry negative{
        .strip_bounds = {.x = 0.0F, .y = 0.0F, .width = 130.0F, .height = 24.0F},
        .label_widths = std::span<const float>{owing},
    };
    const auto negative_output = veyra::emit_menubar(theme, labels, negative, commands);
    require(
        negative_output.result == veyra::MenubarEmitResult::invalid_input &&
            negative_output.placed == 0 && negative_output.required == 0,
        "a negative label width rejects the emit"
    );

    constexpr std::array<float, 2> stray{{
        50.0F,
        std::numeric_limits<float>::infinity(),
    }};
    const veyra::MenubarGeometry wild{
        .strip_bounds = {.x = 0.0F, .y = 0.0F, .width = 130.0F, .height = 24.0F},
        .label_widths = std::span<const float>{stray},
    };
    const auto wild_output = veyra::emit_menubar(theme, labels, wild, commands);
    require(
        wild_output.result == veyra::MenubarEmitResult::invalid_input && wild_output.placed == 0 &&
            wild_output.required == 0,
        "a fluid label width rejects the emit even though chrome never measures text"
    );

    constexpr std::array<float, 1> short_widths{{50.0F}};
    const veyra::MenubarGeometry ragged{
        .strip_bounds = {.x = 0.0F, .y = 0.0F, .width = 130.0F, .height = 24.0F},
        .label_widths = std::span<const float>{short_widths},
    };
    const auto ragged_output = veyra::emit_menubar(theme, labels, ragged, commands);
    require(
        ragged_output.result == veyra::MenubarEmitResult::invalid_input &&
            ragged_output.placed == 0 && ragged_output.required == 0,
        "a width count that misses the label count rejects the emit"
    );
}

void check_empty_emit_completes() {
    const auto theme = veyra::Theme{};
    const veyra::MenubarGeometry geometry{
        .strip_bounds = {.x = 0.0F, .y = 0.0F, .width = 100.0F, .height = 24.0F},
        .label_widths = std::span<const float>{},
    };
    std::array<veyra::DisplayCommand, 4> commands{};

    const auto output =
        veyra::emit_menubar(theme, std::span<const veyra::MenubarLabelState>{}, geometry, commands);

    require(
        output.result == veyra::MenubarEmitResult::complete && output.placed == 2 &&
            output.required == 2,
        "an empty label set still paints the band and its rule"
    );
    require(
        veyra::menubar_required_commands(std::span<const veyra::MenubarLabelState>{}) == 2,
        "the capacity helper keeps the band-plus-rule floor"
    );

    const auto& band = as_fill(commands[0], "the empty strip keeps its band");
    require(
        same_rect(band.bounds, 0.0F, 0.0F, 100.0F, 24.0F),
        "the empty band pins the strip rect"
    );
    const auto& rule = as_fill(commands[1], "the empty strip keeps its rule");
    require(
        same_rect(rule.bounds, 0.0F, 23.0F, 100.0F, 1.0F),
        "the empty rule pins the strip foot"
    );
}

}

int main() {
    try {
        check_appearance_maps_label_states();
        check_controller_state_drives_labels();
        check_emit_pins_geometry_and_colors();
        check_emit_carries_no_text();
        check_emit_reports_overflow_without_silent_truncation();
        check_theme_overlay_flows_through();
        check_open_menu_composition();
        check_invalid_geometry_rejected();
        check_empty_emit_completes();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
