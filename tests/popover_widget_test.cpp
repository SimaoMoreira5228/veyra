#include "veyra/behaviors/overlay/overlay.hpp"
#include "veyra/layout/panels/menu_placement.hpp"
#include "veyra/widgets/overlay/popover.hpp"

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

void check_appearance_maps_tokens() {
    const auto theme = veyra::Theme{};

    const auto popup = veyra::popover_appearance(theme, false);
    require(
        popup.panel == theme.color(veyra::tokens::color_surface),
        "a popover panel rests on the surface"
    );
    require(
        popup.border == theme.color(veyra::tokens::color_text_disabled) &&
            popup.separator == theme.color(veyra::tokens::color_text_disabled),
        "border and separator share the muted token"
    );
    require(popup.scrim.alpha == 0, "a popover arms no scrim, transparent by construction");

    const auto dialog = veyra::popover_appearance(theme, true);
    require(
        dialog.panel == popup.panel && dialog.border == popup.border &&
            dialog.separator == popup.separator,
        "a dialog is the same chrome, not a second struct"
    );
    require(dialog.scrim.alpha == 128, "the modal flag only arms a translucent scrim");
    require(
        dialog.scrim.red == theme.color(veyra::tokens::color_text_primary).red &&
            dialog.scrim.green == theme.color(veyra::tokens::color_text_primary).green &&
            dialog.scrim.blue == theme.color(veyra::tokens::color_text_primary).blue,
        "the modal scrim tints from the text primary token"
    );
}

void check_placement_resolves_before_emit() {
    const veyra::MenuPlacementRequest request{
        .anchor = {.x = 180.0F, .y = 170.0F},
        .menu_width = 120.0F,
        .menu_height = 100.0F,
        .viewport = {.x = 0.0F, .y = 0.0F, .width = 200.0F, .height = 200.0F},
        .gap = 4.0F,
    };
    veyra::MenuPlacement placement{};
    const auto resolved = veyra::resolve_menu_placement(request, placement);

    require(resolved == veyra::MenuPlacementResult::clamped, "the corner anchor cannot fit below");
    require(
        placement.flipped_horizontal && placement.flipped_vertical,
        "placement flips above and left of the anchor"
    );
    require(
        same_rect(placement.rect, 56.0F, 66.0F, 120.0F, 100.0F),
        "the resolved rect pins the flipped panel"
    );

    const auto theme = veyra::Theme{};
    const veyra::PopoverGeometry geometry{
        .panel = placement.rect,
        .modal = false,
    };
    std::array<veyra::DisplayCommand, 4> commands{};

    const auto output = veyra::emit_popover(theme, geometry, commands);

    require(output.result == veyra::PopoverEmitResult::complete, "the resolved rect emits");
    require(
        same_rect(as_fill(commands[0], "the panel leads").bounds, 56.0F, 66.0F, 120.0F, 100.0F),
        "the emitted panel is the resolved rect, not the anchor"
    );
}

void check_popover_emit_pins_panel_and_border() {
    const auto theme = veyra::Theme{};
    const veyra::PopoverGeometry geometry{
        .panel = {.x = 10.0F, .y = 20.0F, .width = 160.0F, .height = 96.0F},
        .modal = false,
    };
    std::array<veyra::DisplayCommand, 4> commands{};

    const auto output = veyra::emit_popover(theme, geometry, commands);

    require(output.result == veyra::PopoverEmitResult::complete, "a sized span completes");
    require(output.placed == 2 && output.required == 2, "a popover is panel plus border");

    const auto& panel = as_fill(commands[0], "the panel fill leads");
    require(
        same_rect(panel.bounds, 10.0F, 20.0F, 160.0F, 96.0F) &&
            panel.color == theme.color(veyra::tokens::color_surface),
        "the panel pins its rect and the surface color"
    );

    const auto& border = as_outline(commands[1], "the border trails the panel");
    require(
        same_rect(border.bounds, 10.0F, 20.0F, 160.0F, 96.0F) && border.thickness == 1.0F &&
            border.color == theme.color(veyra::tokens::color_text_disabled),
        "the border pins the panel rect at one pixel in the muted color"
    );
}

void check_modal_emit_layers_scrim_under_panel() {
    const auto theme = veyra::Theme{};
    const veyra::PopoverGeometry geometry{
        .panel = {.x = 40.0F, .y = 60.0F, .width = 120.0F, .height = 80.0F},
        .viewport = {.x = 0.0F, .y = 0.0F, .width = 200.0F, .height = 200.0F},
        .modal = true,
    };
    std::array<veyra::DisplayCommand, 4> commands{};

    const auto output = veyra::emit_popover(theme, geometry, commands);

    require(output.result == veyra::PopoverEmitResult::complete, "a sized span completes");
    require(output.placed == 3 && output.required == 3, "a dialog adds the scrim");

    const auto& scrim = as_fill(commands[0], "the scrim leads under the panel");
    require(
        same_rect(scrim.bounds, 0.0F, 0.0F, 200.0F, 200.0F) && scrim.color.alpha == 128 &&
            scrim.color.red == theme.color(veyra::tokens::color_text_primary).red &&
            scrim.color.green == theme.color(veyra::tokens::color_text_primary).green &&
            scrim.color.blue == theme.color(veyra::tokens::color_text_primary).blue,
        "the scrim pins the full viewport, translucent"
    );

    const auto& panel = as_fill(commands[1], "the panel sits above the scrim");
    require(
        same_rect(panel.bounds, 40.0F, 60.0F, 120.0F, 80.0F) &&
            panel.color == theme.color(veyra::tokens::color_surface),
        "the dialog panel pins its centered rect on the surface"
    );

    const auto& border = as_outline(commands[2], "the border trails the dialog panel");
    require(
        same_rect(border.bounds, 40.0F, 60.0F, 120.0F, 80.0F) && border.thickness == 1.0F,
        "the dialog border hugs the same rect at one pixel"
    );
}

void check_emit_reports_overflow_without_silent_truncation() {
    const auto theme = veyra::Theme{};
    const veyra::PopoverGeometry geometry{
        .panel = {.x = 40.0F, .y = 60.0F, .width = 120.0F, .height = 80.0F},
        .viewport = {.x = 0.0F, .y = 0.0F, .width = 200.0F, .height = 200.0F},
        .modal = true,
    };
    std::array<veyra::DisplayCommand, 4> full{};
    const auto complete = veyra::emit_popover(theme, geometry, full);
    require(
        complete.result == veyra::PopoverEmitResult::complete && complete.placed == 3,
        "the reference emit fits its span"
    );

    std::array<veyra::DisplayCommand, 1> tight{};
    const auto output = veyra::emit_popover(theme, geometry, tight);

    require(
        output.result == veyra::PopoverEmitResult::insufficient_output,
        "a short span reports its overflow"
    );
    require(output.placed == 1 && output.required == 3, "the report keeps placed and required");

    const auto& kept = as_fill(tight[0], "the short span keeps the scrim");
    const auto& reference = as_fill(full[0], "the reference span opens with the scrim");
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
    const veyra::PopoverGeometry popup{
        .panel = {.x = 10.0F, .y = 20.0F, .width = 160.0F, .height = 96.0F},
        .modal = false,
    };
    const auto empty = veyra::emit_popover(theme, popup, starved);
    require(
        empty.result == veyra::PopoverEmitResult::insufficient_output && empty.placed == 0 &&
            empty.required == 2,
        "a popover with no room reports both commands missing"
    );
}

void check_theme_override_flows_through() {
    auto theme = veyra::Theme::dark();

    auto tweaks = veyra::Theme{};
    const auto panel_tweak = veyra::Srgba8{.red = 16, .green = 18, .blue = 24};
    const auto veil_tweak = veyra::Srgba8{.red = 200, .green = 210, .blue = 225};
    require(
        tweaks.set(veyra::tokens::color_surface, panel_tweak) == veyra::ThemeUpdateResult::changed,
        "the overlay takes the panel tweak"
    );
    require(
        tweaks.set(veyra::tokens::color_text_primary, veil_tweak) ==
            veyra::ThemeUpdateResult::changed,
        "the overlay takes the veil tweak"
    );
    require(
        theme.apply(tweaks) == veyra::ThemeUpdateResult::changed,
        "layering the overlay reports a change"
    );

    const auto dialog = veyra::popover_appearance(theme, true);
    require(dialog.panel == panel_tweak, "the panel follows the scoped theme override");
    require(
        dialog.scrim ==
            veyra::Srgba8{
                .red = 200,
                .green = 210,
                .blue = 225,
                .alpha = 128,
            },
        "the scrim follows the veil override and stays translucent"
    );
    require(
        dialog.separator == theme.color(veyra::tokens::color_text_disabled),
        "untweaked tokens keep resolving through the derived theme"
    );

    const veyra::PopoverGeometry geometry{
        .panel = {.x = 0.0F, .y = 0.0F, .width = 120.0F, .height = 80.0F},
        .viewport = {.x = 0.0F, .y = 0.0F, .width = 200.0F, .height = 200.0F},
        .modal = true,
    };
    std::array<veyra::DisplayCommand, 4> commands{};
    const auto output = veyra::emit_popover(theme, geometry, commands);
    require(output.result == veyra::PopoverEmitResult::complete, "the themed emit completes");
    require(
        as_fill(commands[1], "the themed panel trails the scrim").color == panel_tweak,
        "the emitted panel tracks the overridden surface"
    );
}

void check_invalid_geometry_rejected() {
    const auto theme = veyra::Theme{};
    std::array<veyra::DisplayCommand, 4> commands{};

    const veyra::PopoverGeometry flat{
        .panel = {.x = 0.0F, .y = 0.0F, .width = 0.0F, .height = 80.0F},
        .modal = false,
    };
    const auto flat_output = veyra::emit_popover(theme, flat, commands);
    require(
        flat_output.result == veyra::PopoverEmitResult::invalid_input && flat_output.placed == 0 &&
            flat_output.required == 0,
        "a zero-width panel rejects the emit without touching the span"
    );

    const veyra::PopoverGeometry inverted{
        .panel = {.x = 0.0F, .y = 0.0F, .width = 120.0F, .height = -8.0F},
        .modal = false,
    };
    const auto inverted_output = veyra::emit_popover(theme, inverted, commands);
    require(
        inverted_output.result == veyra::PopoverEmitResult::invalid_input &&
            inverted_output.placed == 0,
        "a negative panel height rejects the emit"
    );

    const veyra::PopoverGeometry drifting{
        .panel =
            {
                .x = std::numeric_limits<float>::quiet_NaN(),
                .y = 0.0F,
                .width = 120.0F,
                .height = 80.0F,
            },
        .modal = false,
    };
    const auto drifting_output = veyra::emit_popover(theme, drifting, commands);
    require(
        drifting_output.result == veyra::PopoverEmitResult::invalid_input,
        "a non-finite panel origin rejects the emit"
    );

    const veyra::PopoverGeometry blind{
        .panel = {.x = 40.0F, .y = 60.0F, .width = 120.0F, .height = 80.0F},
        .modal = true,
    };
    const auto blind_output = veyra::emit_popover(theme, blind, commands);
    require(
        blind_output.result == veyra::PopoverEmitResult::invalid_input && blind_output.placed == 0,
        "a modal with no viewport rejects, the scrim has nowhere to go"
    );

    const veyra::PopoverGeometry viewport_free{
        .panel = {.x = 10.0F, .y = 20.0F, .width = 160.0F, .height = 96.0F},
        .modal = false,
    };
    const auto viewport_free_output = veyra::emit_popover(theme, viewport_free, commands);
    require(
        viewport_free_output.result == veyra::PopoverEmitResult::complete &&
            viewport_free_output.placed == 2,
        "a popover ignores the viewport it never paints"
    );
}

void check_intents_stay_in_controller() {
    veyra::NodeTree tree;
    tree.reserve(3);

    const auto root = tree.create();
    const auto dialog = tree.create();
    const auto restore = tree.create();
    require(tree.append_child(root, dialog), "the dialog attaches");
    require(tree.append_child(root, restore), "the restore node attaches");

    veyra::OverlayController overlays;
    require(
        overlays.open({.dialog = dialog, .restore_focus = restore}) == veyra::OverlayResult::opened,
        "the dialog opens modal"
    );
    require(overlays.trap_root() == dialog, "the trap root is the open dialog");

    veyra::SemanticNodeData modal_data{};
    require(overlays.write_dialog_semantics("Save changes", modal_data), "semantics fills");
    require(
        modal_data.role == veyra::SemanticRole::dialog &&
            veyra::has_state(modal_data.states, veyra::SemanticState::modal) &&
            veyra::has_action(modal_data.actions, veyra::SemanticAction::dismiss),
        "a modal dialog reads modal with a dismiss action"
    );

    const auto nested = tree.create();
    require(tree.append_child(root, nested), "the popover node attaches");
    require(
        overlays.open(
            {.dialog = nested, .restore_focus = restore, .kind = veyra::OverlayKind::popover}
        ) == veyra::OverlayResult::opened,
        "the popover opens above the dialog"
    );
    require(overlays.trap_root() == nested, "the trap root follows the top layer");

    veyra::SemanticNodeData popup_data{};
    require(overlays.write_dialog_semantics("Align", popup_data), "popover semantics fills");
    require(
        !veyra::has_state(popup_data.states, veyra::SemanticState::modal) &&
            veyra::has_action(popup_data.actions, veyra::SemanticAction::dismiss),
        "a popover drops the modal state and keeps dismiss"
    );
    require(
        overlays.outside_press().result == veyra::OverlayResult::dismiss,
        "outside-press dismisses the popover layer"
    );
    require(
        overlays.escape().result == veyra::OverlayResult::cancel,
        "escape cancels through the controller, never through chrome"
    );
}

}

int main() {
    try {
        check_appearance_maps_tokens();
        check_placement_resolves_before_emit();
        check_popover_emit_pins_panel_and_border();
        check_modal_emit_layers_scrim_under_panel();
        check_emit_reports_overflow_without_silent_truncation();
        check_theme_override_flows_through();
        check_invalid_geometry_rejected();
        check_intents_stay_in_controller();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
