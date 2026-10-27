#include "veyra/behaviors/overlay/tooltip.hpp"
#include "veyra/widgets/overlay/tooltip_card.hpp"

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

const veyra::StrokeRect&
as_outline(const veyra::DisplayCommand& command, std::string_view message) {
    const auto* outline = std::get_if<veyra::StrokeRect>(&command);

    require(outline != nullptr, message);

    return *outline;
}

bool same_rect(veyra::LayoutRect rect, float x, float y, float width, float height) {
    return rect.x == x && rect.y == y && rect.width == width && rect.height == height;
}

double linear_channel(std::uint8_t channel) {
    const double scaled = static_cast<double>(channel) / 255.0;

    if (scaled <= 0.04045) {
        return scaled / 12.92;
    }

    return std::pow((scaled + 0.055) / 1.055, 2.4);
}

double contrast_ratio(veyra::Srgba8 first, veyra::Srgba8 second) {
    const double first_luminance = 0.2126 * linear_channel(first.red) +
        0.7152 * linear_channel(first.green) + 0.0722 * linear_channel(first.blue);
    const double second_luminance = 0.2126 * linear_channel(second.red) +
        0.7152 * linear_channel(second.green) + 0.0722 * linear_channel(second.blue);
    const double lighter = first_luminance > second_luminance ? first_luminance : second_luminance;
    const double darker = first_luminance > second_luminance ? second_luminance : first_luminance;

    return (lighter + 0.05) / (darker + 0.05);
}

void check_appearance_inverts_surface_pair() {
    const auto theme = veyra::Theme{};
    const auto appearance = veyra::tooltip_appearance(theme);

    require(
        appearance.background == theme.color(veyra::tokens::color_text_primary),
        "the tooltip panel borrows the ink, not the paper"
    );
    require(
        appearance.text == theme.color(veyra::tokens::color_surface),
        "the tooltip text borrows the paper, so it reads on the ink"
    );
    require(
        appearance.border == theme.color(veyra::tokens::color_text_disabled),
        "the tooltip border speaks the same muted token as popover chrome"
    );
}

void check_contrast_canary_across_presets() {
    const veyra::Theme presets[] = {
        veyra::Theme::light(),
        veyra::Theme::dark(),
        veyra::Theme::high_contrast(),
    };

    for (const auto& theme : presets) {
        const auto appearance = veyra::tooltip_appearance(theme);

        require(
            appearance.background == theme.color(veyra::tokens::color_text_primary) &&
                appearance.text == theme.color(veyra::tokens::color_surface),
            "every preset inverts the same token pair, no per-theme special case"
        );
        require(
            appearance.background != appearance.text,
            "panel and text never collapse onto one color"
        );
        require(
            contrast_ratio(appearance.background, appearance.text) >= 7.0,
            "the inverted pair keeps headroom past AAA on every preset"
        );
        require(
            appearance.border != appearance.background,
            "the muted border stays a visible edge on the inverted panel"
        );
    }
}

void check_theme_override_flows_through() {
    auto theme = veyra::Theme::dark();

    auto tweaks = veyra::Theme{};
    const auto ink = veyra::Srgba8{.red = 10, .green = 12, .blue = 16};
    const auto paper = veyra::Srgba8{.red = 240, .green = 242, .blue = 248};
    const auto edge = veyra::Srgba8{.red = 150, .green = 156, .blue = 168};
    require(
        tweaks.set(veyra::tokens::color_text_primary, ink) == veyra::ThemeUpdateResult::changed,
        "the overlay takes the ink tweak"
    );
    require(
        tweaks.set(veyra::tokens::color_surface, paper) == veyra::ThemeUpdateResult::changed,
        "the overlay takes the paper tweak"
    );
    require(
        tweaks.set(veyra::tokens::color_text_disabled, edge) == veyra::ThemeUpdateResult::changed,
        "the overlay takes the edge tweak"
    );
    require(
        theme.apply(tweaks) == veyra::ThemeUpdateResult::changed,
        "layering the overlay reports a change"
    );

    const auto appearance = veyra::tooltip_appearance(theme);
    require(
        appearance.background == ink && appearance.text == paper && appearance.border == edge,
        "the tooltip follows the scoped override on all three slots"
    );

    const veyra::TooltipCardGeometry geometry{
        .rect = {.x = 8.0F, .y = 12.0F, .width = 120.0F, .height = 28.0F},
    };
    std::array<veyra::DisplayCommand, 4> commands{};
    const auto output = veyra::emit_tooltip_card(theme, geometry, commands);

    require(output.result == veyra::TooltipCardEmitResult::complete, "the themed emit completes");
    require(
        as_fill(commands[0], "the themed panel leads").color == ink,
        "the emitted panel tracks the overridden ink"
    );
    require(
        as_outline(commands[1], "the themed border trails").color == edge,
        "the emitted border tracks the overridden edge"
    );
}

void check_emit_pins_panel_and_border() {
    const auto theme = veyra::Theme{};
    const veyra::TooltipCardGeometry geometry{
        .rect = {.x = 8.0F, .y = 12.0F, .width = 120.0F, .height = 28.0F},
    };
    std::array<veyra::DisplayCommand, 4> commands{};

    const auto output = veyra::emit_tooltip_card(theme, geometry, commands);

    require(output.result == veyra::TooltipCardEmitResult::complete, "a sized span completes");
    require(output.placed == 2 && output.required == 2, "a tooltip is panel plus border");

    const auto& panel = as_fill(commands[0], "the panel fill leads");
    require(
        same_rect(panel.bounds, 8.0F, 12.0F, 120.0F, 28.0F) &&
            panel.color == theme.color(veyra::tokens::color_text_primary),
        "the panel pins its caller-resolved rect in the inverted ink"
    );

    const auto& border = as_outline(commands[1], "the border trails the panel");
    require(
        same_rect(border.bounds, 8.0F, 12.0F, 120.0F, 28.0F) && border.thickness == 1.0F &&
            border.color == theme.color(veyra::tokens::color_text_disabled),
        "the border pins the panel rect at one pixel in the muted color"
    );
}

void check_emit_reports_overflow_without_silent_truncation() {
    const auto theme = veyra::Theme{};
    const veyra::TooltipCardGeometry geometry{
        .rect = {.x = 8.0F, .y = 12.0F, .width = 120.0F, .height = 28.0F},
    };

    std::array<veyra::DisplayCommand, 4> full{};
    const auto complete = veyra::emit_tooltip_card(theme, geometry, full);
    require(
        complete.result == veyra::TooltipCardEmitResult::complete && complete.placed == 2,
        "the reference emit fits its span"
    );

    std::array<veyra::DisplayCommand, 1> tight{};
    const auto output = veyra::emit_tooltip_card(theme, geometry, tight);

    require(
        output.result == veyra::TooltipCardEmitResult::insufficient_output,
        "a short span reports its overflow"
    );
    require(output.placed == 1 && output.required == 2, "the report keeps placed and required");

    const auto& kept = as_fill(tight[0], "the short span keeps the panel");
    const auto& reference = as_fill(full[0], "the reference span opens with the panel");
    require(
        kept.color == reference.color &&
            same_rect(
                kept.bounds,
                reference.bounds.x,
                reference.bounds.y,
                reference.bounds.width,
                reference.bounds.height
            ),
        "the short span keeps a whole-command prefix of the full emit"
    );

    std::array<veyra::DisplayCommand, 0> starved{};
    const auto empty = veyra::emit_tooltip_card(theme, geometry, starved);
    require(
        empty.result == veyra::TooltipCardEmitResult::insufficient_output && empty.placed == 0 &&
            empty.required == 2,
        "a tooltip with no room reports both commands missing"
    );
}

void check_invalid_geometry_rejected() {
    const auto theme = veyra::Theme{};
    std::array<veyra::DisplayCommand, 4> commands{};

    const veyra::TooltipCardGeometry flat{
        .rect = {.x = 0.0F, .y = 0.0F, .width = 0.0F, .height = 28.0F},
    };
    const auto flat_output = veyra::emit_tooltip_card(theme, flat, commands);
    require(
        flat_output.result == veyra::TooltipCardEmitResult::invalid_input &&
            flat_output.placed == 0 && flat_output.required == 0,
        "a zero-width card rejects the emit without touching the span"
    );

    const veyra::TooltipCardGeometry inverted{
        .rect = {.x = 0.0F, .y = 0.0F, .width = 120.0F, .height = -8.0F},
    };
    const auto inverted_output = veyra::emit_tooltip_card(theme, inverted, commands);
    require(
        inverted_output.result == veyra::TooltipCardEmitResult::invalid_input &&
            inverted_output.placed == 0,
        "a negative card height rejects the emit"
    );

    const veyra::TooltipCardGeometry drifting{
        .rect = {
            .x = std::numeric_limits<float>::quiet_NaN(),
            .y = 0.0F,
            .width = 120.0F,
            .height = 28.0F,
        },
    };
    const auto drifting_output = veyra::emit_tooltip_card(theme, drifting, commands);
    require(
        drifting_output.result == veyra::TooltipCardEmitResult::invalid_input,
        "a non-finite card origin rejects the emit"
    );

    const veyra::TooltipCardGeometry endless{
        .rect = {
            .x = 0.0F,
            .y = 0.0F,
            .width = std::numeric_limits<float>::infinity(),
            .height = 28.0F,
        },
    };
    const auto endless_output = veyra::emit_tooltip_card(theme, endless, commands);
    require(
        endless_output.result == veyra::TooltipCardEmitResult::invalid_input,
        "an infinite card width rejects the emit"
    );
}

void check_timing_ownership_stays_in_controller() {
    veyra::TooltipController tip;
    require(
        tip.hover_start(7, 400, 1000) == veyra::TooltipResult::changed,
        "arming parks the tooltip pending on its anchor"
    );

    veyra::SemanticNodeData pending_data{};
    require(
        !tip.write_tooltip_semantics("Save", pending_data),
        "a pending tooltip describes nothing yet"
    );

    require(
        tip.advance(1400) == veyra::TooltipResult::shown,
        "the dwell expiry shows the tooltip exactly once"
    );

    veyra::SemanticNodeData shown_data{};
    require(tip.write_tooltip_semantics("Save", shown_data), "a shown tooltip writes semantics");
    require(
        shown_data.label == "Save" && shown_data.actions == std::uint16_t{},
        "the record carries the caller text with no actions, so it can never take focus"
    );

    const auto theme = veyra::Theme{};
    const veyra::TooltipCardGeometry geometry{
        .rect = {.x = 8.0F, .y = 12.0F, .width = 120.0F, .height = 28.0F},
    };
    std::array<veyra::DisplayCommand, 4> before{};
    std::array<veyra::DisplayCommand, 4> after{};
    const auto shown_emit = veyra::emit_tooltip_card(theme, geometry, before);
    require(tip.hide() == veyra::TooltipResult::changed, "hiding clears the shown tooltip");
    const auto hidden_emit = veyra::emit_tooltip_card(theme, geometry, after);

    require(
        shown_emit.result == veyra::TooltipCardEmitResult::complete &&
            hidden_emit.result == veyra::TooltipCardEmitResult::complete,
        "chrome emits the same rect either way, it takes no visibility input"
    );
    require(
        as_fill(before[0], "the shown panel leads").color ==
            as_fill(after[0], "the hidden panel leads").color,
        "show and hide never recolor chrome, the caller decides when to paint"
    );

    veyra::SemanticNodeData hidden_data{};
    require(
        !tip.write_tooltip_semantics("Save", hidden_data),
        "a hidden tooltip describes nothing again"
    );
}

}

int main() {
    try {
        check_appearance_inverts_surface_pair();
        check_contrast_canary_across_presets();
        check_theme_override_flows_through();
        check_emit_pins_panel_and_border();
        check_emit_reports_overflow_without_silent_truncation();
        check_invalid_geometry_rejected();
        check_timing_ownership_stays_in_controller();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
