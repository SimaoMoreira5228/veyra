#include "veyra/behaviors/menu.hpp"
#include "veyra/widgets/data/menu_list.hpp"

#include <array>
#include <cmath>
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

const veyra::StrokeLine& as_line(const veyra::DisplayCommand& command, std::string_view message) {
    const auto* line = std::get_if<veyra::StrokeLine>(&command);

    require(line != nullptr, message);

    return *line;
}

bool same_rect(veyra::LayoutRect rect, float x, float y, float width, float height) {
    return rect.x == x && rect.y == y && rect.width == width && rect.height == height;
}

bool same_point(veyra::LayoutPoint point, float x, float y) {
    return point.x == x && point.y == y;
}

void check_appearance_maps_row_states() {
    const auto theme = veyra::Theme{};

    const auto plain = veyra::menu_row_appearance(theme, {});
    require(
        plain.background == theme.color(veyra::tokens::color_surface) &&
            plain.text == theme.color(veyra::tokens::color_text_primary) &&
            plain.checkmark == theme.color(veyra::tokens::color_toggle_checked) &&
            plain.separator == theme.color(veyra::tokens::color_text_disabled) &&
            plain.submenu_arrow == theme.color(veyra::tokens::color_text_primary) &&
            plain.disabled_text == theme.color(veyra::tokens::color_text_disabled),
        "a plain row rests on the surface with primary text and muted rules"
    );

    const auto active = veyra::menu_row_appearance(theme, {.active = true});
    require(
        active.background == theme.color(veyra::tokens::color_action_hovered),
        "an active row fills with the hovered action color"
    );

    const auto checked = veyra::menu_row_appearance(theme, {.checked = true});
    require(
        checked.background == theme.color(veyra::tokens::color_toggle_checked),
        "a checked row fills with the toggle checked color"
    );

    const auto both = veyra::menu_row_appearance(theme, {.active = true, .checked = true});
    require(
        both.background == theme.color(veyra::tokens::color_action_hovered),
        "the active fill wins over the checked fill on the same row"
    );

    const auto gated = veyra::menu_row_appearance(theme, {.active = true, .disabled = true});
    require(
        gated.background == theme.color(veyra::tokens::color_surface),
        "a disabled row takes no active fill even while hovered"
    );

    const auto ruled = veyra::menu_row_appearance(theme, {.active = true, .is_separator = true});
    require(
        ruled.background == theme.color(veyra::tokens::color_surface),
        "a separator takes no fill even when flagged active"
    );
}

void check_controller_state_drives_rows() {
    veyra::MenuController menu;
    constexpr std::array<veyra::MenuItem, 4> items{{
        {.id = 1},
        {.id = 2, .kind = veyra::MenuItemKind::separator},
        {.id = 3, .kind = veyra::MenuItemKind::checkable},
        {.id = 4, .disabled = true},
    }};
    require(menu.open_root(40, items) == veyra::MenuResult::opened, "the root opens");
    require(
        menu.move(veyra::MenuMove::next, items) == veyra::MenuResult::changed,
        "moving off the first row reports a change"
    );

    const auto active = menu.active();
    require(active.has && active.id == 3, "the move lands on the checkable row");

    std::array<veyra::MenuListRowState, 4> rows{};
    for (auto index = std::size_t{}; index < items.size(); ++index) {
        rows[index].active = active.has && active.id == items[index].id;
        rows[index].checked = items[index].id == 3;
        rows[index].disabled = items[index].disabled;
        rows[index].is_separator = items[index].kind == veyra::MenuItemKind::separator;
    }

    require(!rows[0].active && !rows[0].checked, "the command row stays plain");
    require(rows[1].is_separator, "the separator flag follows the item kind");
    require(rows[2].active && rows[2].checked, "the checkable row carries both flags");
    require(rows[3].disabled && !rows[3].active, "the disabled row never takes the cursor");

    const auto theme = veyra::Theme{};
    const veyra::MenuListGeometry geometry{
        .list_bounds = {.x = 0.0F, .y = 0.0F, .width = 160.0F, .height = 96.0F},
        .row_height = 24.0F,
    };
    std::array<veyra::DisplayCommand, 12> commands{};

    const auto output = veyra::emit_menu_list(theme, rows, geometry, commands);

    require(output.result == veyra::MenuListEmitResult::complete, "the composed emit completes");

    const auto& ruled = as_fill(commands[0], "the separator rules itself off first");
    require(
        same_rect(ruled.bounds, 0.0F, 35.5F, 160.0F, 1.0F),
        "the composed separator pins its centered rule"
    );

    const auto& picked = as_fill(commands[1], "the active checked row paints its fill next");
    require(
        same_rect(picked.bounds, 0.0F, 48.0F, 160.0F, 24.0F) &&
            picked.color == theme.color(veyra::tokens::color_action_hovered),
        "the composed active fill pins its row rect and the hovered color"
    );
}

void check_emit_pins_geometry_and_colors() {
    const auto theme = veyra::Theme{};
    constexpr std::array<veyra::MenuListRowState, 5> rows{{
        {.active = true},
        {.checked = true},
        {.is_separator = true},
        {.has_submenu = true},
        {.checked = true, .disabled = true},
    }};
    const veyra::MenuListGeometry geometry{
        .list_bounds = {.x = 10.0F, .y = 20.0F, .width = 200.0F, .height = 120.0F},
        .row_height = 24.0F,
    };
    std::array<veyra::DisplayCommand, 12> commands{};

    const auto output = veyra::emit_menu_list(theme, rows, geometry, commands);

    require(output.result == veyra::MenuListEmitResult::complete, "a sized span completes");
    require(
        output.placed == 9 && output.required == 9,
        "one active, one checked, one rule, one arrow, one muted check take nine paints"
    );
    require(
        veyra::menu_list_required_commands(rows) == 9,
        "the capacity helper matches the full emit"
    );

    const auto& active_fill = as_fill(commands[0], "the first command fills the active row");
    require(
        same_rect(active_fill.bounds, 10.0F, 20.0F, 200.0F, 24.0F) &&
            active_fill.color == theme.color(veyra::tokens::color_action_hovered),
        "the active fill pins its row rect and the hovered color"
    );

    const auto& checked_fill = as_fill(commands[1], "the checked row fills before its mark");
    require(
        same_rect(checked_fill.bounds, 10.0F, 44.0F, 200.0F, 24.0F) &&
            checked_fill.color == theme.color(veyra::tokens::color_toggle_checked),
        "the checked fill pins its row rect and the toggle checked color"
    );

    const auto& check_first = as_line(commands[2], "the check draws its short leg first");
    const auto& check_second = as_line(commands[3], "the check draws its long leg second");
    require(
        same_point(check_first.start, 16.0F, 55.0F) && same_point(check_first.end, 19.0F, 58.0F) &&
            same_point(check_second.start, 19.0F, 58.0F) &&
            same_point(check_second.end, 25.0F, 52.0F),
        "the check pins its jointed legs in the row gutter"
    );
    require(
        check_first.thickness == 1.5F && check_second.thickness == 1.5F &&
            check_first.color == theme.color(veyra::tokens::color_toggle_checked) &&
            check_second.color == theme.color(veyra::tokens::color_toggle_checked),
        "both check legs share one stroke weight and the checked color"
    );

    const auto& rule = as_fill(commands[4], "the separator centers its rule");
    require(
        same_rect(rule.bounds, 10.0F, 79.5F, 200.0F, 1.0F) &&
            rule.color == theme.color(veyra::tokens::color_text_disabled),
        "the rule pins the centered row middle and the muted color"
    );

    const auto& arrow_first = as_line(commands[5], "the submenu arrow draws its upper leg first");
    const auto& arrow_second = as_line(commands[6], "the submenu arrow draws its lower leg second");
    require(
        same_point(arrow_first.start, 200.0F, 100.0F) &&
            same_point(arrow_first.end, 204.0F, 104.0F) &&
            same_point(arrow_second.start, 204.0F, 104.0F) &&
            same_point(arrow_second.end, 200.0F, 108.0F),
        "the arrow pins its right-pointing tip near the row edge"
    );
    require(
        arrow_first.thickness == 1.5F && arrow_second.thickness == 1.5F &&
            arrow_first.color == theme.color(veyra::tokens::color_text_primary),
        "both arrow legs share one stroke weight and the primary text color"
    );

    const auto& muted_first = as_line(commands[7], "the disabled check still draws");
    const auto& muted_second = as_line(commands[8], "the muted check keeps both legs");
    require(
        same_point(muted_first.start, 16.0F, 127.0F) &&
            same_point(muted_second.end, 25.0F, 124.0F) &&
            muted_first.color == theme.color(veyra::tokens::color_text_disabled) &&
            muted_second.color == theme.color(veyra::tokens::color_text_disabled),
        "a disabled check mutes both legs to the disabled text color"
    );
}

void check_emit_carries_no_text() {
    const auto theme = veyra::Theme{};
    constexpr std::array<veyra::MenuListRowState, 4> rows{{
        {.active = true},
        {.checked = true, .has_submenu = true},
        {.is_separator = true},
        {.checked = true, .disabled = true, .has_submenu = true},
    }};
    const veyra::MenuListGeometry geometry{
        .list_bounds = {.x = 0.0F, .y = 0.0F, .width = 160.0F, .height = 96.0F},
        .row_height = 24.0F,
        .shortcut_column_x = 120.0F,
    };
    std::array<veyra::DisplayCommand, 16> commands{};

    const auto output = veyra::emit_menu_list(theme, rows, geometry, commands);

    require(output.result == veyra::MenuListEmitResult::complete, "the text-free emit completes");

    for (auto index = std::size_t{}; index < output.placed; ++index) {
        const bool rect = std::get_if<veyra::FillRect>(&commands[index]) != nullptr;
        const bool line = std::get_if<veyra::StrokeLine>(&commands[index]) != nullptr;
        require(rect || line, "every command paints chrome, never a text or mask run");
    }
}

void check_emit_reports_overflow_without_silent_truncation() {
    const auto theme = veyra::Theme{};
    constexpr std::array<veyra::MenuListRowState, 3> rows{{
        {.active = true},
        {.checked = true, .has_submenu = true},
        {.is_separator = true},
    }};
    const veyra::MenuListGeometry geometry{
        .list_bounds = {.x = 10.0F, .y = 20.0F, .width = 200.0F, .height = 72.0F},
        .row_height = 24.0F,
    };
    std::array<veyra::DisplayCommand, 8> full{};
    const auto complete = veyra::emit_menu_list(theme, rows, geometry, full);
    require(
        complete.result == veyra::MenuListEmitResult::complete && complete.placed == 7 &&
            complete.required == 7,
        "the reference emit fits its span"
    );

    std::array<veyra::DisplayCommand, 2> tight{};
    const auto output = veyra::emit_menu_list(theme, rows, geometry, tight);

    require(
        output.result == veyra::MenuListEmitResult::insufficient_output,
        "a short span reports its overflow"
    );
    require(output.placed == 2 && output.required == 7, "the report keeps placed and required");

    const auto& tight_fill = as_fill(tight[0], "the short span keeps the active fill");
    const auto& full_fill = as_fill(full[0], "the reference span opens with the active fill");
    require(
        tight_fill.color == full_fill.color &&
            same_rect(
                tight_fill.bounds,
                full_fill.bounds.x,
                full_fill.bounds.y,
                full_fill.bounds.width,
                full_fill.bounds.height
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

    const auto active = veyra::menu_row_appearance(theme, {.active = true});
    require(active.background == accent, "the active fill follows the scoped theme override");
    require(
        active.separator == theme.color(veyra::tokens::color_text_disabled),
        "untweaked tokens keep resolving through the derived theme"
    );

    constexpr std::array<veyra::MenuListRowState, 2> rows{{
        {.active = true},
        {.is_separator = true},
    }};
    const veyra::MenuListGeometry geometry{
        .list_bounds = {.x = 0.0F, .y = 0.0F, .width = 100.0F, .height = 48.0F},
        .row_height = 24.0F,
    };
    std::array<veyra::DisplayCommand, 4> commands{};

    const auto output = veyra::emit_menu_list(theme, rows, geometry, commands);

    require(output.result == veyra::MenuListEmitResult::complete, "the dark emit completes");

    const auto& fill = as_fill(commands[0], "the dark active fill leads");
    require(fill.color == accent, "the emitted fill tracks the overridden hover");
    const auto& rule = as_fill(commands[1], "the dark rule trails the fill");
    require(
        rule.color == theme.color(veyra::tokens::color_text_disabled),
        "the emitted rule tracks the derived muted color"
    );
}

void check_row_height_falls_back_to_theme_space() {
    const auto theme = veyra::Theme{};
    constexpr std::array<veyra::MenuListRowState, 2> rows{{
        {.active = true},
        {.is_separator = true},
    }};
    const veyra::MenuListGeometry fallback{
        .list_bounds = {.x = 0.0F, .y = 0.0F, .width = 100.0F, .height = 48.0F},
        .row_height = 0.0F,
    };
    std::array<veyra::DisplayCommand, 4> commands{};

    const auto output = veyra::emit_menu_list(theme, rows, fallback, commands);

    require(
        output.result == veyra::MenuListEmitResult::complete,
        "a zero height takes the fallback"
    );
    require(output.placed == 2 && output.required == 2, "the fallback emit takes two paints");

    const auto& fill = as_fill(commands[0], "the fallback fill pins the derived pitch");
    require(
        same_rect(fill.bounds, 0.0F, 0.0F, 100.0F, 24.0F),
        "the medium space token plus the text allowance read back as 24"
    );
    const auto& rule = as_fill(commands[1], "the fallback rule slides under the first row");
    require(
        same_rect(rule.bounds, 0.0F, 35.5F, 100.0F, 1.0F),
        "the second rule pins the derived pitch of the first row"
    );

    auto wide = veyra::Theme{};
    require(
        wide.set(veyra::tokens::space_medium, veyra::LogicalPx{12.0F}) ==
            veyra::ThemeUpdateResult::changed,
        "the medium space token takes the override"
    );
    std::array<veyra::DisplayCommand, 4> resized{};

    const auto tracked = veyra::emit_menu_list(wide, rows, fallback, resized);

    require(tracked.result == veyra::MenuListEmitResult::complete, "the retuned emit completes");

    const auto& grown = as_fill(resized[0], "the retuned fill tracks the space token");
    require(
        same_rect(grown.bounds, 0.0F, 0.0F, 100.0F, 28.0F),
        "a wider medium token grows the derived pitch"
    );

    const veyra::MenuListGeometry dense{
        .list_bounds = {.x = 0.0F, .y = 0.0F, .width = 100.0F, .height = 40.0F},
        .row_height = 20.0F,
    };
    std::array<veyra::DisplayCommand, 4> caller{};

    const auto overridden = veyra::emit_menu_list(wide, rows, dense, caller);

    require(
        overridden.result == veyra::MenuListEmitResult::complete,
        "an override still completes"
    );

    const auto& kept = as_fill(caller[0], "an explicit height wins over the theme");
    require(
        same_rect(kept.bounds, 0.0F, 0.0F, 100.0F, 20.0F),
        "the caller height pins the row rect"
    );
}

void check_invalid_geometry_rejected() {
    const auto theme = veyra::Theme{};
    constexpr std::array<veyra::MenuListRowState, 1> rows{{{.active = true}}};
    std::array<veyra::DisplayCommand, 4> commands{};

    const veyra::MenuListGeometry negative{
        .list_bounds = {.x = 0.0F, .y = 0.0F, .width = 100.0F, .height = 40.0F},
        .row_height = -4.0F,
    };
    const auto negative_output = veyra::emit_menu_list(theme, rows, negative, commands);
    require(
        negative_output.result == veyra::MenuListEmitResult::invalid_input &&
            negative_output.placed == 0 && negative_output.required == 0,
        "a negative row height rejects the emit without touching the span"
    );

    const veyra::MenuListGeometry fluid{
        .list_bounds = {.x = 0.0F, .y = 0.0F, .width = 100.0F, .height = 40.0F},
        .row_height = std::numeric_limits<float>::quiet_NaN(),
    };
    const auto fluid_output = veyra::emit_menu_list(theme, rows, fluid, commands);
    require(
        fluid_output.result == veyra::MenuListEmitResult::invalid_input &&
            fluid_output.placed == 0 && fluid_output.required == 0,
        "a fluid row height rejects the emit"
    );

    const veyra::MenuListGeometry stray{
        .list_bounds = {.x = 0.0F, .y = 0.0F, .width = 100.0F, .height = 40.0F},
        .row_height = 24.0F,
        .shortcut_column_x = std::numeric_limits<float>::infinity(),
    };
    const auto stray_output = veyra::emit_menu_list(theme, rows, stray, commands);
    require(
        stray_output.result == veyra::MenuListEmitResult::invalid_input &&
            stray_output.placed == 0 && stray_output.required == 0,
        "a fluid shortcut column rejects the emit even though chrome never paints it"
    );
}

void check_empty_emit_completes() {
    const auto theme = veyra::Theme{};
    const veyra::MenuListGeometry geometry{
        .list_bounds = {.x = 0.0F, .y = 0.0F, .width = 100.0F, .height = 40.0F},
        .row_height = 24.0F,
    };
    std::array<veyra::DisplayCommand, 4> commands{};

    const auto output = veyra::emit_menu_list(theme, {}, geometry, commands);

    require(
        output.result == veyra::MenuListEmitResult::complete && output.placed == 0 &&
            output.required == 0,
        "an empty row set completes with no commands"
    );
}

}

int main() {
    try {
        check_appearance_maps_row_states();
        check_controller_state_drives_rows();
        check_emit_pins_geometry_and_colors();
        check_emit_carries_no_text();
        check_emit_reports_overflow_without_silent_truncation();
        check_theme_overlay_flows_through();
        check_row_height_falls_back_to_theme_space();
        check_invalid_geometry_rejected();
        check_empty_emit_completes();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
