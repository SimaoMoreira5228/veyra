#include "veyra/widgets/overlay/popover.hpp"

#include <cmath>

namespace veyra {
namespace {

constexpr float kBorderThickness = 1.0F;
constexpr std::uint8_t kScrimAlpha = 128;

bool valid_rect(LayoutRect rect) noexcept {
    if (!std::isfinite(rect.x) || !std::isfinite(rect.y)) {
        return false;
    }

    if (!std::isfinite(rect.width) || !std::isfinite(rect.height)) {
        return false;
    }

    return rect.width > 0.0F && rect.height > 0.0F;
}

bool push_command(
    std::span<DisplayCommand> output,
    std::size_t& placed,
    DisplayCommand command
) noexcept {
    if (placed >= output.size()) {
        return false;
    }

    output[placed] = command;
    ++placed;

    return true;
}

}

PopoverAppearance popover_appearance(const Theme& theme, bool modal) noexcept {
    auto scrim = theme.color(tokens::color_text_primary);
    scrim.alpha = modal ? kScrimAlpha : 0;

    return {
        .panel = theme.color(tokens::color_surface),
        .border = theme.color(tokens::color_text_disabled),
        .separator = theme.color(tokens::color_text_disabled),
        .scrim = scrim,
    };
}

PopoverOutput emit_popover(
    const Theme& theme,
    const PopoverGeometry& geometry,
    std::span<DisplayCommand> commands_out
) noexcept {
    if (!valid_rect(geometry.panel)) {
        return {
            .placed = 0,
            .required = 0,
            .result = PopoverEmitResult::invalid_input,
        };
    }

    if (geometry.modal && !valid_rect(geometry.viewport)) {
        return {
            .placed = 0,
            .required = 0,
            .result = PopoverEmitResult::invalid_input,
        };
    }

    const auto appearance = popover_appearance(theme, geometry.modal);
    const std::size_t required = geometry.modal ? 3 : 2;

    auto placed = std::size_t{};
    auto exhausted = false;

    auto push = [&](DisplayCommand command) noexcept {
        if (exhausted) {
            return;
        }

        if (!push_command(commands_out, placed, command)) {
            exhausted = true;
        }
    };

    if (geometry.modal) {
        push(
            FillRect{
                .bounds = geometry.viewport,
                .color = appearance.scrim,
            }
        );
    }

    push(
        FillRect{
            .bounds = geometry.panel,
            .color = appearance.panel,
        }
    );

    push(
        StrokeRect{
            .bounds = geometry.panel,
            .thickness = kBorderThickness,
            .color = appearance.border,
        }
    );

    if (exhausted) {
        return {
            .placed = placed,
            .required = required,
            .result = PopoverEmitResult::insufficient_output,
        };
    }

    return {
        .placed = placed,
        .required = required,
        .result = PopoverEmitResult::complete,
    };
}

}
