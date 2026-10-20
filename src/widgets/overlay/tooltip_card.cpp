#include "veyra/widgets/overlay/tooltip_card.hpp"

#include <cmath>

namespace veyra {
namespace {

constexpr float kBorderThickness = 1.0F;

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

TooltipAppearance tooltip_appearance(const Theme& theme) noexcept {
    return {
        .background = theme.color(tokens::color_text_primary),
        .border = theme.color(tokens::color_text_disabled),
        .text = theme.color(tokens::color_surface),
    };
}

TooltipCardOutput emit_tooltip_card(
    const Theme& theme,
    const TooltipCardGeometry& geometry,
    std::span<DisplayCommand> commands_out
) noexcept {
    if (!valid_rect(geometry.rect)) {
        return {
            .placed = 0,
            .required = 0,
            .result = TooltipCardEmitResult::invalid_input,
        };
    }

    const auto appearance = tooltip_appearance(theme);
    constexpr auto required = std::size_t{2};

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

    push(
        FillRect{
            .bounds = geometry.rect,
            .color = appearance.background,
        }
    );

    push(
        StrokeRect{
            .bounds = geometry.rect,
            .thickness = kBorderThickness,
            .color = appearance.border,
        }
    );

    if (exhausted) {
        return {
            .placed = placed,
            .required = required,
            .result = TooltipCardEmitResult::insufficient_output,
        };
    }

    return {
        .placed = placed,
        .required = required,
        .result = TooltipCardEmitResult::complete,
    };
}

}
