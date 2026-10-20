#include "veyra/widgets/overlay/toast_card.hpp"

#include <cmath>

namespace veyra {
namespace {

constexpr float kBorderThickness = 1.0F;
constexpr float kAccentWidth = 4.0F;

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

Srgba8 accent_for(const Theme& theme, ToastKind kind) noexcept {
    switch (kind) {
    case ToastKind::error:
        return theme.color(tokens::color_error);
    case ToastKind::warning:
        return theme.color(tokens::color_action_hovered);
    case ToastKind::info:
        break;
    }

    return theme.color(tokens::color_action_primary);
}

}

ToastCardAppearance toast_card_appearance(const Theme& theme, ToastCardState state) noexcept {
    return {
        .background = theme.color(tokens::color_surface),
        .border = theme.color(tokens::color_text_disabled),
        .accent = accent_for(theme, state.kind),
        .text = theme.color(tokens::color_text_primary),
    };
}

ToastCardOutput emit_toast_card(
    const Theme& theme,
    ToastCardState state,
    const ToastCardGeometry& geometry,
    std::span<DisplayCommand> commands_out
) noexcept {
    if (!valid_rect(geometry.card)) {
        return {
            .placed = 0,
            .required = 0,
            .result = ToastCardEmitResult::invalid_input,
        };
    }

    const ToastCardAppearance appearance = toast_card_appearance(theme, state);

    auto accent_width = kAccentWidth;

    if (geometry.card.width < accent_width) {
        accent_width = geometry.card.width;
    }

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
            .bounds = geometry.card,
            .color = appearance.background,
        }
    );

    push(
        FillRect{
            .bounds =
                {.x = geometry.card.x,
                 .y = geometry.card.y,
                 .width = accent_width,
                 .height = geometry.card.height},
            .color = appearance.accent,
        }
    );

    push(
        StrokeRect{
            .bounds = geometry.card,
            .thickness = kBorderThickness,
            .color = appearance.border,
        }
    );

    if (exhausted) {
        return {
            .placed = placed,
            .required = kToastCardRequiredCommands,
            .result = ToastCardEmitResult::insufficient_output,
        };
    }

    return {
        .placed = placed,
        .required = kToastCardRequiredCommands,
        .result = ToastCardEmitResult::complete,
    };
}

}
