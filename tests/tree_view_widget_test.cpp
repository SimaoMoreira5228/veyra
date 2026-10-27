#include "veyra/behaviors/selection/selection.hpp"
#include "veyra/behaviors/selection/tree_cursor.hpp"
#include "veyra/widgets/data/tree_view.hpp"

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

const veyra::StrokeLine& as_line(const veyra::DisplayCommand& command, std::string_view message) {
    const auto* line = std::get_if<veyra::StrokeLine>(&command);

    require(line != nullptr, message);

    return *line;
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

bool same_point(veyra::LayoutPoint point, float x, float y) {
    return point.x == x && point.y == y;
}

constexpr veyra::TreeCursorRow kLeaf{
    .id = 7,
    .parent = 0,
    .depth = 0,
    .has_children = false,
    .expanded = false,
    .children_known = true,
};

void check_appearance_maps_row_states() {
    const auto theme = veyra::Theme{};

    const auto plain = veyra::tree_view_row_appearance(theme, {}, kLeaf);
    require(
        plain.background == theme.color(veyra::tokens::color_surface) &&
            plain.separator == theme.color(veyra::tokens::color_text_disabled) &&
            plain.current_outline == theme.color(veyra::tokens::color_action_primary) &&
            plain.chevron == theme.color(veyra::tokens::color_action_primary) &&
            plain.guide == theme.color(veyra::tokens::color_text_disabled),
        "a plain leaf rests on the surface with primary chevron and muted guides"
    );

    const auto selected = veyra::tree_view_row_appearance(theme, {.selected = true}, kLeaf);
    require(
        selected.background == theme.color(veyra::tokens::color_action_hovered),
        "a selected row fills with the hovered action color like the flat list"
    );

    const auto placeholder =
        veyra::tree_view_row_appearance(theme, {.selected = true, .placeholder = true}, kLeaf);
    require(
        placeholder.background == theme.color(veyra::tokens::color_action_disabled),
        "a placeholder wins over selection while its data is missing"
    );

    constexpr veyra::TreeCursorRow unknown{
        .id = 3,
        .parent = 0,
        .depth = 0,
        .has_children = true,
        .expanded = false,
        .children_known = false,
    };
    const auto pending = veyra::tree_view_row_appearance(theme, {}, unknown);
    require(
        pending.chevron == theme.color(veyra::tokens::color_text_disabled),
        "an unknown-children row mutes its chevron to the disabled text color"
    );

    constexpr veyra::TreeCursorRow known{
        .id = 4,
        .parent = 0,
        .depth = 0,
        .has_children = true,
        .expanded = true,
        .children_known = true,
    };
    const auto open = veyra::tree_view_row_appearance(theme, {}, known);
    require(
        open.chevron == theme.color(veyra::tokens::color_action_primary),
        "a known parent keeps the primary chevron whether open or shut"
    );
}

void check_indent_math_uses_geometry_first() {
    const auto theme = veyra::Theme{};
    constexpr std::array<veyra::TreeCursorRow, 2> rows{{
        {.id = 1, .parent = 0, .depth = 0, .has_children = true},
        {.id = 2, .parent = 1, .depth = 1, .has_children = true},
    }};
    constexpr std::array<veyra::TreeViewRowState, 2> states{{{}, {}}};
    const veyra::TreeViewRowsGeometry geometry{
        .list_bounds = {.x = 10.0F, .y = 20.0F, .width = 200.0F, .height = 120.0F},
        .row_height = 24.0F,
        .indent_width = 16.0F,
    };
    std::array<veyra::DisplayCommand, 16> commands{};

    const auto output = veyra::emit_tree_view_rows(theme, rows, states, geometry, commands);

    require(output.result == veyra::TreeViewEmitResult::complete, "the indent emit completes");
    require(output.placed == 7 && output.required == 7, "two parents take seven paints");

    const auto& first = as_line(commands[1], "the root chevron opens the emit after its rule");
    require(
        same_point(first.start, 10.0F, 28.0F) && same_point(first.end, 15.0F, 32.0F),
        "depth zero starts its chevron at the list edge"
    );

    const auto& nested = as_line(commands[5], "the nested chevron follows its guide");
    require(
        same_point(nested.start, 26.0F, 52.0F) && same_point(nested.end, 31.0F, 56.0F),
        "depth one starts its chevron one indent width in"
    );
}

void check_indent_falls_back_to_theme_space() {
    const auto theme = veyra::Theme{};
    constexpr std::array<veyra::TreeCursorRow, 1> rows{{
        {.id = 1, .parent = 0, .depth = 1, .has_children = true},
    }};
    constexpr std::array<veyra::TreeViewRowState, 1> states{{}};
    const veyra::TreeViewRowsGeometry geometry{
        .list_bounds = {.x = 10.0F, .y = 0.0F, .width = 200.0F, .height = 40.0F},
        .row_height = 24.0F,
    };
    std::array<veyra::DisplayCommand, 8> commands{};

    const auto output = veyra::emit_tree_view_rows(theme, rows, states, geometry, commands);

    require(output.result == veyra::TreeViewEmitResult::complete, "the fallback emit completes");

    const auto& chevron = as_line(commands[2], "the chevron trails the guide and the rule");
    require(
        same_point(chevron.start, 18.0F, 8.0F),
        "a zero indent width falls back to the medium space token"
    );
}

void check_chevron_tracks_parent_state() {
    const auto theme = veyra::Theme{};
    constexpr std::array<veyra::TreeCursorRow, 4> rows{{
        {.id = 1, .parent = 0, .depth = 0, .has_children = false},
        {.id = 2, .parent = 0, .depth = 0, .has_children = true, .children_known = true},
        {.id = 3, .parent = 0, .depth = 0, .has_children = true, .expanded = true},
        {.id = 4, .parent = 0, .depth = 0, .has_children = true, .children_known = false},
    }};
    constexpr std::array<veyra::TreeViewRowState, 4> states{{{}, {}, {}, {}}};
    const veyra::TreeViewRowsGeometry geometry{
        .list_bounds = {.x = 0.0F, .y = 0.0F, .width = 160.0F, .height = 96.0F},
        .row_height = 24.0F,
    };
    std::array<veyra::DisplayCommand, 16> commands{};

    const auto output = veyra::emit_tree_view_rows(theme, rows, states, geometry, commands);

    require(output.result == veyra::TreeViewEmitResult::complete, "the chevron emit completes");
    require(output.placed == 10 && output.required == 10, "one leaf plus three parents take ten");

    require(
        std::get_if<veyra::StrokeLine>(&commands[1]) == nullptr,
        "a leaf draws no chevron, just its separator"
    );

    const auto& shut_first = as_line(commands[2], "a shut parent draws its chevron first leg");
    const auto& shut_second = as_line(commands[3], "a shut parent draws its chevron second leg");
    require(
        same_point(shut_first.start, 0.0F, 32.0F) && same_point(shut_first.end, 5.0F, 36.0F) &&
            same_point(shut_second.start, 5.0F, 36.0F) && same_point(shut_second.end, 0.0F, 40.0F),
        "a shut parent points right from a shared tip"
    );

    const auto& open_first = as_line(commands[5], "an open parent draws its chevron first leg");
    const auto& open_second = as_line(commands[6], "an open parent draws its chevron second leg");
    require(
        same_point(open_first.start, 0.0F, 58.0F) && same_point(open_first.end, 3.0F, 61.0F) &&
            same_point(open_second.start, 3.0F, 61.0F) && same_point(open_second.end, 6.0F, 58.0F),
        "an open parent points down from a shared valley"
    );
    require(
        open_first.thickness == 1.5F && shut_first.thickness == 1.5F,
        "both chevrons share one stroke weight"
    );

    const auto& pending_first = as_line(commands[8], "an unknown parent still draws a chevron");
    const auto& pending_second = as_line(commands[9], "the unknown chevron has both legs");
    require(
        pending_first.color == theme.color(veyra::tokens::color_text_disabled) &&
            pending_second.color == theme.color(veyra::tokens::color_text_disabled),
        "unknown children mute both chevron legs to the pending tint"
    );
    require(
        shut_first.color == theme.color(veyra::tokens::color_action_primary),
        "a known shut parent keeps the primary chevron"
    );
}

void check_guides_span_depth_transitions() {
    const auto theme = veyra::Theme{};
    constexpr std::array<veyra::TreeCursorRow, 4> rows{{
        {.id = 1, .parent = 0, .depth = 0},
        {.id = 2, .parent = 1, .depth = 1},
        {.id = 3, .parent = 1, .depth = 1},
        {.id = 4, .parent = 0, .depth = 0},
    }};
    constexpr std::array<veyra::TreeViewRowState, 4> states{{{}, {}, {}, {}}};
    const veyra::TreeViewRowsGeometry geometry{
        .list_bounds = {.x = 0.0F, .y = 0.0F, .width = 160.0F, .height = 96.0F},
        .row_height = 24.0F,
        .indent_width = 16.0F,
    };
    std::array<veyra::DisplayCommand, 16> commands{};

    const auto output = veyra::emit_tree_view_rows(theme, rows, states, geometry, commands);

    require(output.result == veyra::TreeViewEmitResult::complete, "the guide emit completes");
    require(output.placed == 6 && output.required == 6, "two nested rows add one guide each");

    const auto& upper = as_line(commands[1], "the first child carries the guide down");
    require(
        same_point(upper.start, 3.0F, 24.0F) && same_point(upper.end, 3.0F, 48.0F) &&
            upper.thickness == 1.0F &&
            upper.color == theme.color(veyra::tokens::color_text_disabled),
        "the upper guide pins its level column and the muted color"
    );

    const auto& lower = as_line(commands[3], "the last child carries the guide down");
    require(
        same_point(lower.start, 3.0F, 48.0F) && same_point(lower.end, 3.0F, 72.0F),
        "the lower guide tiles exactly under the upper one"
    );
}

void check_pipeline_placeholders_emit_shimmer_fills() {
    const auto theme = veyra::Theme{};
    constexpr std::array<veyra::TreeCursorRow, 2> rows{{
        {.id = 1, .parent = 0, .depth = 0},
        {.id = 2, .parent = 1, .depth = 1},
    }};
    const std::array<veyra::TreeViewRowState, 2> states{{
        {},
        {.placeholder = true},
    }};
    const veyra::TreeViewRowsGeometry geometry{
        .list_bounds = {.x = 0.0F, .y = 0.0F, .width = 160.0F, .height = 48.0F},
        .row_height = 24.0F,
        .indent_width = 16.0F,
    };
    std::array<veyra::DisplayCommand, 8> commands{};

    const auto output = veyra::emit_tree_view_rows(theme, rows, states, geometry, commands);

    require(output.result == veyra::TreeViewEmitResult::complete, "placeholder rows complete");
    require(output.placed == 4 && output.required == 4, "the shimmer row adds one fill");

    const auto& shimmer = as_fill(commands[1], "the placeholder emits its fill before its guide");
    require(
        same_rect(shimmer.bounds, 0.0F, 24.0F, 160.0F, 24.0F) &&
            shimmer.color == theme.color(veyra::tokens::color_action_disabled),
        "the shimmer fill pins its row rect and the disabled color"
    );
}

void check_sticky_header_band_shifts_rows() {
    const auto theme = veyra::Theme{};
    constexpr std::array<veyra::TreeCursorRow, 1> rows{{
        {.id = 1, .parent = 0, .depth = 0},
    }};
    constexpr std::array<veyra::TreeViewRowState, 1> states{{}};
    const veyra::TreeViewRowsGeometry geometry{
        .list_bounds = {.x = 10.0F, .y = 20.0F, .width = 200.0F, .height = 120.0F},
        .row_height = 24.0F,
        .sticky_header_height = 24.0F,
    };
    std::array<veyra::DisplayCommand, 8> commands{};

    const auto output = veyra::emit_tree_view_rows(theme, rows, states, geometry, commands);

    require(output.result == veyra::TreeViewEmitResult::complete, "the sticky emit completes");
    require(output.placed == 3 && output.required == 3, "the band adds two commands");

    const auto& band = as_fill(commands[0], "the band opens the emit");
    require(
        same_rect(band.bounds, 10.0F, 20.0F, 200.0F, 24.0F) &&
            band.color == theme.color(veyra::tokens::color_surface),
        "the sticky band pins the list top and the surface color"
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

void check_emit_reports_overflow_without_silent_truncation() {
    const auto theme = veyra::Theme{};
    constexpr std::array<veyra::TreeCursorRow, 2> rows{{
        {.id = 1, .parent = 0, .depth = 0, .has_children = true},
        {.id = 2, .parent = 1, .depth = 1},
    }};
    constexpr std::array<veyra::TreeViewRowState, 2> states{{
        {.selected = true},
        {.current = true},
    }};
    const veyra::TreeViewRowsGeometry geometry{
        .list_bounds = {.x = 0.0F, .y = 0.0F, .width = 160.0F, .height = 48.0F},
        .row_height = 24.0F,
        .indent_width = 16.0F,
    };

    require(
        veyra::tree_view_required_commands(rows, states, geometry) == 7,
        "the capacity helper counts fills, guides, chevrons, and outlines"
    );

    std::array<veyra::DisplayCommand, 8> full{};
    const auto complete = veyra::emit_tree_view_rows(theme, rows, states, geometry, full);
    require(
        complete.result == veyra::TreeViewEmitResult::complete && complete.placed == 7 &&
            complete.required == 7,
        "a sized span completes with the helper count"
    );

    std::array<veyra::DisplayCommand, 2> tight{};
    const auto output = veyra::emit_tree_view_rows(theme, rows, states, geometry, tight);

    require(
        output.result == veyra::TreeViewEmitResult::insufficient_output,
        "a short span reports its overflow"
    );
    require(output.placed == 2 && output.required == 7, "the report keeps placed and required");

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
}

void check_theme_overlay_flows_through() {
    auto theme = veyra::Theme::dark();

    auto tweaks = veyra::Theme{};
    const auto accent = veyra::Srgba8{.red = 10, .green = 200, .blue = 120};
    require(
        tweaks.set(veyra::tokens::color_action_primary, accent) ==
            veyra::ThemeUpdateResult::changed,
        "the overlay takes the scoped tweak"
    );
    require(
        theme.apply(tweaks) == veyra::ThemeUpdateResult::changed,
        "layering the overlay reports a change"
    );

    const auto look = veyra::tree_view_row_appearance(theme, {.selected = true}, kLeaf);
    require(look.chevron == accent, "the chevron follows the scoped primary override");
    require(
        look.guide == theme.color(veyra::tokens::color_text_disabled),
        "untweaked tokens keep resolving through the derived theme"
    );

    constexpr std::array<veyra::TreeCursorRow, 1> rows{{
        {.id = 1, .parent = 0, .depth = 0},
    }};
    constexpr std::array<veyra::TreeViewRowState, 1> states{{}};
    const veyra::TreeViewRowsGeometry geometry{
        .list_bounds = {.x = 0.0F, .y = 0.0F, .width = 100.0F, .height = 40.0F},
        .row_height = 24.0F,
        .sticky_header_height = 24.0F,
    };
    std::array<veyra::DisplayCommand, 4> commands{};

    const auto output = veyra::emit_tree_view_rows(theme, rows, states, geometry, commands);
    require(output.result == veyra::TreeViewEmitResult::complete, "the dark emit completes");

    const auto& band = as_fill(commands[0], "the dark band leads");
    require(
        band.color == theme.color(veyra::tokens::color_surface),
        "the emitted band tracks the overridden surface"
    );
}

void check_invalid_geometry_rejected() {
    const auto theme = veyra::Theme{};
    constexpr std::array<veyra::TreeCursorRow, 1> rows{{
        {.id = 1, .parent = 0, .depth = 0},
    }};
    constexpr std::array<veyra::TreeViewRowState, 1> states{{}};
    std::array<veyra::DisplayCommand, 4> commands{};

    const veyra::TreeViewRowsGeometry flat{
        .list_bounds = {.x = 0.0F, .y = 0.0F, .width = 100.0F, .height = 40.0F},
        .row_height = 0.0F,
    };
    const auto flat_output = veyra::emit_tree_view_rows(theme, rows, states, flat, commands);
    require(
        flat_output.result == veyra::TreeViewEmitResult::invalid_input && flat_output.placed == 0 &&
            flat_output.required == 0,
        "a zero row height rejects the emit without touching the span"
    );

    const veyra::TreeViewRowsGeometry negative{
        .list_bounds = {.x = 0.0F, .y = 0.0F, .width = 100.0F, .height = 40.0F},
        .row_height = 24.0F,
        .sticky_header_height = -4.0F,
    };
    const auto negative_output =
        veyra::emit_tree_view_rows(theme, rows, states, negative, commands);
    require(
        negative_output.result == veyra::TreeViewEmitResult::invalid_input &&
            negative_output.placed == 0,
        "a negative header height rejects the emit"
    );

    constexpr std::array<veyra::TreeViewRowState, 2> extra{{{}, {}}};
    const veyra::TreeViewRowsGeometry paired{
        .list_bounds = {.x = 0.0F, .y = 0.0F, .width = 100.0F, .height = 40.0F},
        .row_height = 24.0F,
    };
    const auto ragged_output = veyra::emit_tree_view_rows(theme, rows, extra, paired, commands);
    require(
        ragged_output.result == veyra::TreeViewEmitResult::invalid_input &&
            ragged_output.placed == 0 && ragged_output.required == 0,
        "a row and state count mismatch rejects the emit"
    );
    require(
        veyra::tree_view_required_commands(rows, extra, paired) == 0,
        "the capacity helper reports zero for the same mismatch"
    );
}

void check_empty_emit_completes() {
    const auto theme = veyra::Theme{};
    const veyra::TreeViewRowsGeometry geometry{
        .list_bounds = {.x = 0.0F, .y = 0.0F, .width = 100.0F, .height = 40.0F},
        .row_height = 24.0F,
    };
    std::array<veyra::DisplayCommand, 4> commands{};

    const auto output = veyra::emit_tree_view_rows(theme, {}, {}, geometry, commands);

    require(
        output.result == veyra::TreeViewEmitResult::complete && output.placed == 0 &&
            output.required == 0,
        "an empty row set completes with no commands"
    );
}

void check_controllers_compose_into_emit() {
    constexpr std::array<veyra::TreeCursorRow, 4> source{{
        {.id = 1, .parent = 0, .depth = 0, .has_children = true, .children_known = false},
        {.id = 2, .parent = 1, .depth = 1},
        {.id = 3, .parent = 1, .depth = 1},
        {.id = 4, .parent = 0, .depth = 0},
    }};

    auto expansion = veyra::TreeExpansion{};
    require(
        expansion.expand(1, source) == veyra::TreeCursorResult::needs_children,
        "an unknown parent asks for its children first"
    );
    require(
        expansion.confirm_children(1) == veyra::TreeCursorResult::expand,
        "confirmed children join the expanded set"
    );
    require(expansion.is_expanded(1), "the parent stays expanded after confirmation");

    auto selection = veyra::SelectionController{};
    require(
        selection.configure({.mode = veyra::SelectionMode::multiple}),
        "the tree takes a multi-select controller"
    );
    require(
        selection.select(2) == veyra::SelectionResult::changed,
        "selecting a child reports a change"
    );
    require(selection.set_current(2), "parking the cursor on the child reports a change");

    std::array<veyra::TreeCursorRow, 4> rows = source;

    for (auto& row : rows) {
        row.expanded = expansion.is_expanded(row.id);
        row.children_known = true;
    }

    std::array<veyra::TreeViewRowState, 4> states{};

    for (auto index = std::size_t{}; index < rows.size(); ++index) {
        states[index].selected = selection.contains(rows[index].id);
        states[index].current = selection.has_current() && selection.current() == rows[index].id;
    }

    const auto theme = veyra::Theme{};
    const veyra::TreeViewRowsGeometry geometry{
        .list_bounds = {.x = 0.0F, .y = 0.0F, .width = 200.0F, .height = 80.0F},
        .row_height = 20.0F,
        .indent_width = 16.0F,
    };
    std::array<veyra::DisplayCommand, 16> commands{};

    const auto output = veyra::emit_tree_view_rows(theme, rows, states, geometry, commands);

    require(output.result == veyra::TreeViewEmitResult::complete, "the composed emit completes");
    require(output.placed == 10 && output.required == 10, "the composed rows take ten paints");

    const auto& open_first = as_line(commands[1], "the expanded root draws its chevron first leg");
    const auto& open_second =
        as_line(commands[2], "the expanded root draws its chevron second leg");
    require(
        same_point(open_first.start, 0.0F, 8.0F) && same_point(open_first.end, 3.0F, 11.0F) &&
            same_point(open_second.start, 3.0F, 11.0F) && same_point(open_second.end, 6.0F, 8.0F),
        "the expansion set resolves into a downward chevron on the root"
    );

    const auto& picked = as_fill(commands[3], "the selected child paints its fill first");
    require(
        same_rect(picked.bounds, 0.0F, 20.0F, 200.0F, 20.0F) &&
            picked.color == veyra::Srgba8{.red = 48, .green = 105, .blue = 190},
        "the selection resolves into the hovered fill on the child row"
    );

    const auto& cursor = as_outline(commands[6], "the cursor outlines the picked child last");
    require(
        same_rect(cursor.bounds, 0.0F, 20.0F, 200.0F, 20.0F) && cursor.thickness == 2.0F &&
            cursor.color == veyra::Srgba8{.red = 57, .green = 120, .blue = 216},
        "the cursor resolves into a primary outline around the same row"
    );
}

}

int main() {
    try {
        check_appearance_maps_row_states();
        check_indent_math_uses_geometry_first();
        check_indent_falls_back_to_theme_space();
        check_chevron_tracks_parent_state();
        check_guides_span_depth_transitions();
        check_pipeline_placeholders_emit_shimmer_fills();
        check_sticky_header_band_shifts_rows();
        check_emit_reports_overflow_without_silent_truncation();
        check_theme_overlay_flows_through();
        check_invalid_geometry_rejected();
        check_empty_emit_completes();
        check_controllers_compose_into_emit();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
