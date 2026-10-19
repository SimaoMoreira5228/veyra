#include "veyra/behaviors/selection.hpp"
#include "veyra/data/list_pipeline.hpp"
#include "veyra/widgets/data/virtual_list.hpp"

#include <array>
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

void check_appearance_maps_row_states() {
    const auto theme = veyra::Theme{};

    const auto plain = veyra::virtual_list_row_appearance(theme, {});
    require(
        plain.background == theme.color(veyra::tokens::color_surface) &&
            plain.separator == theme.color(veyra::tokens::color_text_disabled) &&
            plain.current_outline == theme.color(veyra::tokens::color_action_primary),
        "a plain row resolves the surface, muted separator, and primary outline"
    );

    const auto selected = veyra::virtual_list_row_appearance(theme, {.selected = true});
    require(
        selected.background == theme.color(veyra::tokens::color_action_hovered),
        "a selected row fills with the hovered action color"
    );
    require(
        selected.background == veyra::Srgba8{.red = 48, .green = 105, .blue = 190},
        "the selected fill pins the default hovered channels"
    );

    const auto cursor = veyra::virtual_list_row_appearance(theme, {.current = true});
    require(
        cursor.background == theme.color(veyra::tokens::color_surface),
        "the cursor alone paints no fill, the outline carries it"
    );
    require(
        cursor.current_outline == veyra::Srgba8{.red = 57, .green = 120, .blue = 216},
        "the cursor outline pins the default primary channels"
    );

    const auto placeholder = veyra::virtual_list_row_appearance(theme, {.placeholder = true});
    require(
        placeholder.background == theme.color(veyra::tokens::color_action_disabled),
        "a placeholder row fills with the disabled color"
    );

    const auto both =
        veyra::virtual_list_row_appearance(theme, {.selected = true, .placeholder = true});
    require(
        both.background == theme.color(veyra::tokens::color_action_disabled),
        "a placeholder wins over selection while its data is missing"
    );
}

void check_selection_drives_row_states() {
    auto selection = veyra::SelectionController{};
    require(
        selection.select(7) == veyra::SelectionResult::changed,
        "selecting a row id reports a change"
    );
    require(selection.set_current(9), "parking the cursor reports a change");

    constexpr std::array<std::uint64_t, 3> ids{7, 8, 9};
    std::array<veyra::VirtualListRowState, 3> rows{};

    for (auto index = std::size_t{}; index < ids.size(); ++index) {
        rows[index].selected = selection.contains(ids[index]);
        rows[index].current = selection.has_current() && selection.current() == ids[index];
    }

    require(rows[0].selected && !rows[0].current, "the selected id marks its row");
    require(!rows[1].selected && !rows[1].current, "an untouched id leaves its row plain");
    require(!rows[2].selected && rows[2].current, "the cursor id marks its row current");
}

void check_emit_pins_geometry_and_colors() {
    const auto theme = veyra::Theme{};
    constexpr std::array<veyra::VirtualListRowState, 3> rows{{
        {.selected = true},
        {},
        {.current = true},
    }};
    const veyra::VirtualListRowsGeometry geometry{
        .list_bounds = {.x = 10.0F, .y = 20.0F, .width = 200.0F, .height = 120.0F},
        .row_height = 24.0F,
    };
    std::array<veyra::DisplayCommand, 8> commands{};

    const auto output = veyra::emit_virtual_list_rows(theme, rows, geometry, commands);

    require(output.result == veyra::VirtualListEmitResult::complete, "a sized span completes");
    require(output.placed == 5 && output.required == 5, "plain rows skip their background fill");

    const auto& selected_fill = as_fill(commands[0], "the first command fills the selected row");
    require(
        same_rect(selected_fill.bounds, 10.0F, 20.0F, 200.0F, 24.0F) &&
            selected_fill.color == veyra::Srgba8{.red = 48, .green = 105, .blue = 190},
        "the selected fill pins its row rect and channels"
    );

    const auto& first_separator = as_fill(commands[1], "the second command rules the first row");
    require(
        same_rect(first_separator.bounds, 10.0F, 43.0F, 200.0F, 1.0F) &&
            first_separator.color == veyra::Srgba8{.red = 99, .green = 104, .blue = 116},
        "the separator pins the row bottom and the muted channels"
    );

    const auto& plain_separator = as_fill(commands[2], "a plain row emits only its separator");
    require(
        same_rect(plain_separator.bounds, 10.0F, 67.0F, 200.0F, 1.0F),
        "the plain separator pins the second row bottom"
    );

    const auto& cursor_separator = as_fill(commands[3], "the cursor row still rules itself off");
    require(
        same_rect(cursor_separator.bounds, 10.0F, 91.0F, 200.0F, 1.0F),
        "the cursor separator pins the third row bottom"
    );

    const auto& outline = as_outline(commands[4], "the last command outlines the cursor row");
    require(
        same_rect(outline.bounds, 10.0F, 68.0F, 200.0F, 24.0F) && outline.thickness == 2.0F &&
            outline.color == veyra::Srgba8{.red = 57, .green = 120, .blue = 216},
        "the cursor outline pins its row rect, thickness, and channels"
    );
}

void check_emit_reports_overflow_without_silent_truncation() {
    const auto theme = veyra::Theme{};
    constexpr std::array<veyra::VirtualListRowState, 3> rows{{
        {.selected = true},
        {},
        {.current = true},
    }};
    const veyra::VirtualListRowsGeometry geometry{
        .list_bounds = {.x = 10.0F, .y = 20.0F, .width = 200.0F, .height = 120.0F},
        .row_height = 24.0F,
    };
    std::array<veyra::DisplayCommand, 8> full{};
    const auto complete = veyra::emit_virtual_list_rows(theme, rows, geometry, full);
    require(
        complete.result == veyra::VirtualListEmitResult::complete && complete.placed == 5,
        "the reference emit fits its span"
    );

    std::array<veyra::DisplayCommand, 2> tight{};
    const auto output = veyra::emit_virtual_list_rows(theme, rows, geometry, tight);

    require(
        output.result == veyra::VirtualListEmitResult::insufficient_output,
        "a short span reports its overflow"
    );
    require(output.placed == 2 && output.required == 5, "the report keeps placed and required");

    const auto& tight_fill = as_fill(tight[0], "the short span keeps the selected fill");
    const auto& full_fill = as_fill(full[0], "the reference span opens with the selected fill");
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
    const auto& tight_rule = as_fill(tight[1], "the short span keeps the first separator");
    const auto& full_rule = as_fill(full[1], "the reference span rules the first row second");
    require(
        tight_rule.color == full_rule.color &&
            same_rect(
                tight_rule.bounds,
                full_rule.bounds.x,
                full_rule.bounds.y,
                full_rule.bounds.width,
                full_rule.bounds.height
            ),
        "the short prefix covers the separator too"
    );
}

void check_pipeline_placeholders_emit_shimmer_fills() {
    auto pipeline = veyra::ListPipeline{};
    require(!pipeline.should_show_placeholders(), "a fresh pipeline shows no placeholders");
    require(pipeline.set_placeholders(0, 2), "recording trailing placeholders reports a change");
    require(pipeline.should_show_placeholders(), "trailing placeholders show shimmer rows");

    const auto theme = veyra::Theme{};
    const std::array<veyra::VirtualListRowState, 2> rows{{
        {},
        {.placeholder = pipeline.should_show_placeholders()},
    }};
    const veyra::VirtualListRowsGeometry geometry{
        .list_bounds = {.x = 0.0F, .y = 0.0F, .width = 160.0F, .height = 64.0F},
        .row_height = 24.0F,
    };
    std::array<veyra::DisplayCommand, 8> commands{};

    const auto output = veyra::emit_virtual_list_rows(theme, rows, geometry, commands);

    require(output.result == veyra::VirtualListEmitResult::complete, "placeholder rows complete");
    require(output.placed == 3 && output.required == 3, "the shimmer row adds one fill");

    const auto& shimmer = as_fill(commands[1], "the placeholder emits its fill before its rule");
    require(
        same_rect(shimmer.bounds, 0.0F, 24.0F, 160.0F, 24.0F) &&
            shimmer.color == veyra::Srgba8{.red = 208, .green = 211, .blue = 218},
        "the shimmer fill pins its row rect and the disabled channels"
    );
}

void check_sticky_header_band_shifts_rows() {
    const auto theme = veyra::Theme{};
    constexpr std::array<veyra::VirtualListRowState, 1> rows{{}};
    const veyra::VirtualListRowsGeometry geometry{
        .list_bounds = {.x = 10.0F, .y = 20.0F, .width = 200.0F, .height = 120.0F},
        .row_height = 24.0F,
        .sticky_header_height = 24.0F,
    };
    std::array<veyra::DisplayCommand, 8> commands{};

    const auto output = veyra::emit_virtual_list_rows(theme, rows, geometry, commands);

    require(output.result == veyra::VirtualListEmitResult::complete, "the sticky emit completes");
    require(output.placed == 3 && output.required == 3, "the band adds two commands");

    const auto& band = as_fill(commands[0], "the band opens the emit");
    require(
        same_rect(band.bounds, 10.0F, 20.0F, 200.0F, 24.0F) &&
            band.color == veyra::Srgba8{.red = 255, .green = 255, .blue = 255},
        "the sticky band pins the list top and the surface channels"
    );

    const auto& band_rule = as_fill(commands[1], "the band rules itself off");
    require(
        same_rect(band_rule.bounds, 10.0F, 43.0F, 200.0F, 1.0F),
        "the band separator pins the header bottom"
    );

    const auto& shifted = as_fill(commands[2], "the first row slides under the band");
    require(
        same_rect(shifted.bounds, 10.0F, 67.0F, 200.0F, 1.0F),
        "the row separator pins the shifted row bottom"
    );
}

void check_theme_overlay_flows_through() {
    auto theme = veyra::Theme::dark();
    require(
        theme.color(veyra::tokens::color_surface) ==
            veyra::Srgba8{.red = 28, .green = 30, .blue = 38},
        "the dark preset pins its surface"
    );

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

    const auto selected = veyra::virtual_list_row_appearance(theme, {.selected = true});
    require(selected.background == accent, "the selected fill follows the scoped theme override");
    require(
        selected.separator == theme.color(veyra::tokens::color_text_disabled),
        "untweaked tokens keep resolving through the derived theme"
    );
}

void check_invalid_geometry_rejected() {
    const auto theme = veyra::Theme{};
    constexpr std::array<veyra::VirtualListRowState, 1> rows{{}};
    std::array<veyra::DisplayCommand, 4> commands{};

    const veyra::VirtualListRowsGeometry flat{
        .list_bounds = {.x = 0.0F, .y = 0.0F, .width = 100.0F, .height = 40.0F},
        .row_height = 0.0F,
    };
    const auto flat_output = veyra::emit_virtual_list_rows(theme, rows, flat, commands);
    require(
        flat_output.result == veyra::VirtualListEmitResult::invalid_input &&
            flat_output.placed == 0 && flat_output.required == 0,
        "a zero row height rejects the emit without touching the span"
    );

    const veyra::VirtualListRowsGeometry negative{
        .list_bounds = {.x = 0.0F, .y = 0.0F, .width = 100.0F, .height = 40.0F},
        .row_height = 24.0F,
        .sticky_header_height = -4.0F,
    };
    const auto negative_output = veyra::emit_virtual_list_rows(theme, rows, negative, commands);
    require(
        negative_output.result == veyra::VirtualListEmitResult::invalid_input &&
            negative_output.placed == 0,
        "a negative header height rejects the emit"
    );
}

void check_empty_emit_completes() {
    const auto theme = veyra::Theme{};
    const veyra::VirtualListRowsGeometry geometry{
        .list_bounds = {.x = 0.0F, .y = 0.0F, .width = 100.0F, .height = 40.0F},
        .row_height = 24.0F,
    };
    std::array<veyra::DisplayCommand, 4> commands{};

    const auto output = veyra::emit_virtual_list_rows(theme, {}, geometry, commands);

    require(
        output.result == veyra::VirtualListEmitResult::complete && output.placed == 0 &&
            output.required == 0,
        "an empty row set completes with no commands"
    );
}

}

int main() {
    try {
        check_appearance_maps_row_states();
        check_selection_drives_row_states();
        check_emit_pins_geometry_and_colors();
        check_emit_reports_overflow_without_silent_truncation();
        check_pipeline_placeholders_emit_shimmer_fills();
        check_sticky_header_band_shifts_rows();
        check_theme_overlay_flows_through();
        check_invalid_geometry_rejected();
        check_empty_emit_completes();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
