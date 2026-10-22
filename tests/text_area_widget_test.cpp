#include "veyra/text/text_editor_buffer.hpp"
#include "veyra/widgets/basic/text_area.hpp"

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

const veyra::FillRoundedRect&
as_rounded_fill(const veyra::DisplayCommand& command, std::string_view message) {
    const auto* fill = std::get_if<veyra::FillRoundedRect>(&command);

    require(fill != nullptr, message);

    return *fill;
}

const veyra::StrokeRoundedRect&
as_rounded_outline(const veyra::DisplayCommand& command, std::string_view message) {
    const auto* outline = std::get_if<veyra::StrokeRoundedRect>(&command);

    require(outline != nullptr, message);

    return *outline;
}

bool same_rect(veyra::LayoutRect rect, float x, float y, float width, float height) {
    return rect.x == x && rect.y == y && rect.width == width && rect.height == height;
}

void check_appearance_mirrors_textfield_tokens() {
    const auto theme = veyra::Theme{};

    const auto disabled = veyra::text_area_appearance(theme, {.disabled = true});
    require(
        disabled.box_fill == theme.color(veyra::tokens::color_action_disabled) &&
            disabled.border == theme.color(veyra::tokens::color_action_disabled) &&
            disabled.placeholder == theme.color(veyra::tokens::color_action_disabled) &&
            disabled.selection == theme.color(veyra::tokens::color_action_disabled) &&
            disabled.caret == theme.color(veyra::tokens::color_text_disabled),
        "disabled styling wins across the whole frame including the selection"
    );

    const auto focused = veyra::text_area_appearance(theme, {.focused = true});
    require(
        focused.border == theme.color(veyra::tokens::color_action_primary) &&
            focused.caret == theme.color(veyra::tokens::color_action_primary),
        "focus resolves the action border and caret"
    );
    require(
        focused.box_fill == theme.color(veyra::tokens::color_surface) &&
            focused.placeholder == theme.color(veyra::tokens::color_text_disabled) &&
            focused.selection == theme.color(veyra::tokens::color_action_hovered),
        "focus keeps the surface fill, the muted placeholder, and the hovered selection"
    );

    const auto idle = veyra::text_area_appearance(theme, {});
    require(
        idle.border == theme.color(veyra::tokens::color_text_disabled) &&
            idle.placeholder == theme.color(veyra::tokens::color_text_disabled) &&
            idle.caret == theme.color(veyra::tokens::color_action_primary),
        "an idle frame uses the muted border with a live caret"
    );
    require(
        idle.corner_radius == theme.radius(veyra::tokens::radius_small) &&
            idle.horizontal_padding == theme.space(veyra::tokens::space_small) &&
            idle.vertical_padding == theme.space(veyra::tokens::space_small),
        "frame geometry resolves through the theme"
    );
}

void check_emit_pins_box_and_border() {
    const auto theme = veyra::Theme{};
    constexpr veyra::LayoutRect bounds{.x = 8.0F, .y = 12.0F, .width = 240.0F, .height = 120.0F};
    const veyra::TextAreaGeometry geometry{.bounds = bounds};

    std::array<veyra::DisplayCommand, 4> commands{};
    const auto output = veyra::emit_text_area(theme, {.focused = true}, geometry, commands);

    require(
        output.result == veyra::TextAreaEmitResult::complete && output.placed == 2 &&
            output.required == 2,
        "a bare frame paints box plus border"
    );

    const auto& box = as_rounded_fill(commands[0], "the box opens the emit");
    require(
        same_rect(box.bounds, 8.0F, 12.0F, 240.0F, 120.0F) &&
            box.color == theme.color(veyra::tokens::color_surface) &&
            box.radius == theme.radius(veyra::tokens::radius_small).value,
        "the box pins the frame rect, the surface color, and the theme radius"
    );

    const auto& border = as_rounded_outline(commands[1], "the border closes the emit");
    require(
        same_rect(border.bounds, 8.0F, 12.0F, 240.0F, 120.0F) && border.thickness == 1.0F &&
            border.color == theme.color(veyra::tokens::color_action_primary),
        "the border pins the frame rect, its thickness, and the focused color"
    );
    require(
        veyra::text_area_required_commands(geometry) == 2,
        "the sizing helper matches the bare frame"
    );
}

void check_caller_maps_selection_bytes_to_rects() {
    std::array<char, 256> storage{};
    veyra::TextEditorBuffer buffer(std::span<char>{storage}, "one\ntwo\nthree");

    std::array<char, 256> scratch{};
    auto length = std::size_t{};
    require(
        buffer.copy_text(scratch, length) == veyra::TextEditResult::complete,
        "the document flattens into caller scratch"
    );
    const std::string_view text(scratch.data(), length);
    require(text == "one\ntwo\nthree", "the buffer holds three hard lines");

    constexpr float advance = 8.0F;
    constexpr float line_height = 16.0F;
    constexpr float origin_x = 8.0F;
    constexpr float origin_y = 12.0F;

    auto line_index = std::size_t{};
    auto line_start = std::size_t{};
    auto selection_line = std::size_t{};
    auto selection_offset = std::size_t{};
    const auto selection_start = std::size_t{4};
    const auto selection_end = std::size_t{7};

    for (auto index = std::size_t{}; index <= text.size(); ++index) {
        if (index == text.size() || text[index] == '\n') {
            if (selection_start >= line_start && selection_start < index) {
                selection_line = line_index;
                selection_offset = selection_start - line_start;
            }
            ++line_index;
            line_start = index + 1;
        }
    }
    require(selection_line == 1 && selection_offset == 0, "the caller finds two on its line");

    const std::array<veyra::LayoutRect, 1> selection{{
        {.x = origin_x + static_cast<float>(selection_offset) * advance,
         .y = origin_y + static_cast<float>(selection_line) * line_height,
         .width = static_cast<float>(selection_end - selection_start) * advance,
         .height = line_height},
    }};

    const auto theme = veyra::Theme{};
    const veyra::TextAreaGeometry geometry{
        .bounds = {.x = 8.0F, .y = 12.0F, .width = 240.0F, .height = 120.0F},
        .selection_rects = selection,
    };

    std::array<veyra::DisplayCommand, 4> commands{};
    const auto output = veyra::emit_text_area(theme, {.has_selection = true}, geometry, commands);

    require(
        output.result == veyra::TextAreaEmitResult::complete && output.placed == 3 &&
            output.required == 3,
        "one caller rect adds one selection fill"
    );

    const auto& highlight = as_fill(commands[1], "the selection trails the box");
    require(
        same_rect(highlight.bounds, 8.0F, 28.0F, 24.0F, 16.0F) &&
            highlight.color == theme.color(veyra::tokens::color_action_hovered),
        "the highlight pins the caller-mapped rect and the hovered color"
    );
}

void check_caret_bar_follows_caller_rect() {
    const auto theme = veyra::Theme{};
    constexpr veyra::LayoutRect caret{.x = 32.0F, .y = 28.0F, .width = 2.0F, .height = 16.0F};
    const veyra::TextAreaGeometry geometry{
        .bounds = {.x = 8.0F, .y = 12.0F, .width = 240.0F, .height = 120.0F},
        .caret = caret,
        .show_caret = true,
    };

    std::array<veyra::DisplayCommand, 4> commands{};
    const auto output = veyra::emit_text_area(theme, {.focused = true}, geometry, commands);

    require(
        output.result == veyra::TextAreaEmitResult::complete && output.placed == 3,
        "a visible caret adds one bar"
    );

    const auto& bar = as_fill(commands[1], "the caret trails the box");
    require(
        same_rect(bar.bounds, 32.0F, 28.0F, 2.0F, 16.0F) &&
            bar.color == theme.color(veyra::tokens::color_action_primary),
        "the bar pins the caller caret rect and the primary color"
    );

    const veyra::TextAreaGeometry hidden{
        .bounds = geometry.bounds,
        .caret = caret,
    };
    std::array<veyra::DisplayCommand, 4> quiet{};
    const auto silent = veyra::emit_text_area(theme, {.focused = true}, hidden, quiet);
    require(
        silent.result == veyra::TextAreaEmitResult::complete && silent.placed == 2,
        "a hidden caret paints box plus border only"
    );
}

void check_composition_underline_uses_primary() {
    const auto theme = veyra::Theme{};
    constexpr veyra::LayoutRect underline{.x = 8.0F, .y = 42.0F, .width = 24.0F, .height = 1.0F};
    const veyra::TextAreaGeometry geometry{
        .bounds = {.x = 8.0F, .y = 12.0F, .width = 240.0F, .height = 120.0F},
        .composition_underline = underline,
        .has_composition = true,
    };

    std::array<veyra::DisplayCommand, 4> commands{};
    const auto output = veyra::emit_text_area(theme, {.focused = true}, geometry, commands);

    require(
        output.result == veyra::TextAreaEmitResult::complete && output.placed == 3,
        "a live preedit adds one underline rule"
    );

    const auto& rule = as_fill(commands[1], "the underline trails the box");
    require(
        same_rect(rule.bounds, 8.0F, 42.0F, 24.0F, 1.0F) &&
            rule.color == theme.color(veyra::tokens::color_action_primary),
        "the rule pins the caller composition rect and the primary color"
    );
}

void check_clip_wraps_when_requested() {
    const auto theme = veyra::Theme{};
    const veyra::TextAreaGeometry geometry{
        .bounds = {.x = 8.0F, .y = 12.0F, .width = 240.0F, .height = 120.0F},
        .clip_to_viewport = true,
    };

    std::array<veyra::DisplayCommand, 6> commands{};
    const auto output = veyra::emit_text_area(theme, {}, geometry, commands);

    require(
        output.result == veyra::TextAreaEmitResult::complete && output.placed == 4 &&
            output.required == 4,
        "clipping adds a push/pop pair around the frame"
    );

    const auto* push = std::get_if<veyra::PushClip>(&commands[0]);
    require(
        push != nullptr && same_rect(push->bounds, 8.0F, 12.0F, 240.0F, 120.0F),
        "the clip opens the emit on the viewport bounds"
    );
    require(std::get_if<veyra::PopClip>(&commands[3]) != nullptr, "the clip closes the emit");
    require(
        veyra::text_area_required_commands(geometry) == 4,
        "the sizing helper counts the clip pair"
    );

    const veyra::TextAreaGeometry unclipped{.bounds = geometry.bounds};
    std::array<veyra::DisplayCommand, 6> plain{};
    require(
        veyra::emit_text_area(theme, {}, unclipped, plain).placed == 2 &&
            std::get_if<veyra::PushClip>(&plain[0]) == nullptr,
        "the default emit leaves clipping to the caller"
    );
}

void check_invalid_input_and_overflow() {
    const auto theme = veyra::Theme{};
    std::array<veyra::DisplayCommand, 4> commands{};

    const veyra::TextAreaGeometry flat{
        .bounds = {.x = 8.0F, .y = 12.0F, .width = 0.0F, .height = 120.0F},
    };
    const auto rejected = veyra::emit_text_area(theme, {}, flat, commands);
    require(
        rejected.result == veyra::TextAreaEmitResult::invalid_input && rejected.placed == 0 &&
            rejected.required == 0,
        "a degenerate frame rejects with nothing placed"
    );

    const veyra::TextAreaGeometry bad_caret{
        .bounds = {.x = 8.0F, .y = 12.0F, .width = 240.0F, .height = 120.0F},
        .caret = {.x = 32.0F, .y = 28.0F, .width = 0.0F, .height = 16.0F},
        .show_caret = true,
    };
    require(
        veyra::emit_text_area(theme, {}, bad_caret, commands).result ==
            veyra::TextAreaEmitResult::invalid_input,
        "a degenerate caret rect rejects instead of painting a lie"
    );

    const std::array<veyra::LayoutRect, 1> selection{{
        {.x = 8.0F, .y = 28.0F, .width = 24.0F, .height = 16.0F},
    }};
    const veyra::TextAreaGeometry full{
        .bounds = {.x = 8.0F, .y = 12.0F, .width = 240.0F, .height = 120.0F},
        .selection_rects = selection,
    };

    std::array<veyra::DisplayCommand, 8> reference{};
    const auto complete = veyra::emit_text_area(theme, {.has_selection = true}, full, reference);
    require(
        complete.result == veyra::TextAreaEmitResult::complete && complete.placed == 3,
        "the reference emit fits its span"
    );

    std::array<veyra::DisplayCommand, 1> tight{};
    const auto starved = veyra::emit_text_area(theme, {.has_selection = true}, full, tight);
    require(
        starved.result == veyra::TextAreaEmitResult::insufficient_output && starved.placed == 1 &&
            starved.required == 3,
        "a short span keeps the box and reports the missing paints"
    );
    require(
        as_rounded_fill(tight[0], "the short span keeps the box").bounds.width == 240.0F,
        "the short prefix keeps a whole-command prefix of the full emit"
    );
}

void check_theme_override_flows_through() {
    auto theme = veyra::Theme{};
    const auto hovered = veyra::Srgba8{.red = 10, .green = 200, .blue = 120};
    require(
        theme.set(veyra::tokens::color_action_hovered, hovered) ==
            veyra::ThemeUpdateResult::changed,
        "the overlay takes the scoped tweak"
    );

    const auto look = veyra::text_area_appearance(theme, {.has_selection = true});
    require(look.selection == hovered, "the selection follows the scoped theme override");

    const std::array<veyra::LayoutRect, 1> selection{{
        {.x = 8.0F, .y = 28.0F, .width = 24.0F, .height = 16.0F},
    }};
    const veyra::TextAreaGeometry geometry{
        .bounds = {.x = 8.0F, .y = 12.0F, .width = 240.0F, .height = 120.0F},
        .selection_rects = selection,
    };
    std::array<veyra::DisplayCommand, 4> commands{};
    const auto output = veyra::emit_text_area(theme, {.has_selection = true}, geometry, commands);
    require(
        output.result == veyra::TextAreaEmitResult::complete &&
            as_fill(commands[1], "the overridden highlight trails").color == hovered,
        "the emitted highlight paints the same override"
    );
}

}

int main() {
    try {
        check_appearance_mirrors_textfield_tokens();
        check_emit_pins_box_and_border();
        check_caller_maps_selection_bytes_to_rects();
        check_caret_bar_follows_caller_rect();
        check_composition_underline_uses_primary();
        check_clip_wraps_when_requested();
        check_invalid_input_and_overflow();
        check_theme_override_flows_through();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
