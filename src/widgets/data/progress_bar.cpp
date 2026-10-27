#include "veyra/widgets/data/progress_bar.hpp"

#include "veyra/behaviors/input/range.hpp"

#include <cmath>

namespace veyra {
namespace {

bool valid_bar(LayoutRect bar) noexcept {
    if (!std::isfinite(bar.x) || !std::isfinite(bar.y)) {
        return false;
    }

    if (!std::isfinite(bar.width) || !std::isfinite(bar.height)) {
        return false;
    }

    if (bar.width <= 0.0F || bar.height <= 0.0F) {
        return false;
    }

    return true;
}

bool valid_state(ProgressState state) noexcept {
    if (!std::isfinite(state.fraction) || state.fraction < 0.0 || state.fraction > 1.0) {
        return false;
    }

    return true;
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

float fill_width(LayoutRect bar, double fraction) noexcept {
    return bar.width * static_cast<float>(fraction);
}

float block_offset(float travel, std::uint64_t tick_ms) noexcept {
    const double position =
        indeterminate_progress_fraction(tick_ms, kProgressBarIndeterminatePeriodMs);

    return travel * static_cast<float>(position);
}

}

ProgressBarAppearance progress_bar_appearance(const Theme& theme, ProgressState state) noexcept {
    Srgba8 fill = theme.color(tokens::color_action_primary);

    if (state.canceled) {
        fill = theme.color(tokens::color_text_disabled);
    }

    return {
        .track = theme.color(tokens::color_action_disabled),
        .fill = fill,
        .indeterminate_block = fill,
    };
}

std::size_t progress_bar_required_commands(ProgressState state) noexcept {
    if (state.indeterminate) {
        return 2;
    }

    if (state.fraction > 0.0) {
        return 2;
    }

    return 1;
}

ProgressBarOutput emit_progress_bar(
    const Theme& theme,
    ProgressState state,
    ProgressBarGeometry geometry,
    std::span<DisplayCommand> commands_out
) noexcept {
    if (!valid_bar(geometry.bar) || !valid_state(state)) {
        return {
            .placed = 0,
            .required = 0,
            .result = ProgressBarEmitResult::invalid_input,
        };
    }

    const std::size_t required = progress_bar_required_commands(state);
    const ProgressBarAppearance appearance = progress_bar_appearance(theme, state);

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
            .bounds = geometry.bar,
            .color = appearance.track,
        }
    );

    if (state.indeterminate) {
        const float block_width = geometry.bar.width * kProgressBarIndeterminateShare;
        const float travel = geometry.bar.width - block_width;

        push(
            FillRect{
                .bounds =
                    {.x = geometry.bar.x + block_offset(travel, geometry.animation_tick_ms),
                     .y = geometry.bar.y,
                     .width = block_width,
                     .height = geometry.bar.height},
                .color = appearance.indeterminate_block,
            }
        );
    } else if (state.fraction > 0.0) {
        push(
            FillRect{
                .bounds =
                    {.x = geometry.bar.x,
                     .y = geometry.bar.y,
                     .width = fill_width(geometry.bar, state.fraction),
                     .height = geometry.bar.height},
                .color = appearance.fill,
            }
        );
    }

    if (exhausted) {
        return {
            .placed = placed,
            .required = required,
            .result = ProgressBarEmitResult::insufficient_output,
        };
    }

    return {
        .placed = placed,
        .required = required,
        .result = ProgressBarEmitResult::complete,
    };
}

}
