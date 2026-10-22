#include "veyra/widgets/basic/slider.hpp"

#include <cmath>

namespace veyra {
namespace {

bool valid_fraction(double fraction) noexcept {
    if (!std::isfinite(fraction)) {
        return false;
    }

    if (fraction < 0.0 || fraction > 1.0) {
        return false;
    }

    return true;
}

bool valid_state(SliderEmitState state) noexcept {
    if (!valid_fraction(state.fraction_0)) {
        return false;
    }

    if (!state.has_second) {
        return true;
    }

    if (!valid_fraction(state.fraction_1)) {
        return false;
    }

    return state.fraction_1 >= state.fraction_0;
}

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

bool valid_thickness(float thickness) noexcept {
    if (!std::isfinite(thickness)) {
        return false;
    }

    return thickness > 0.0F;
}

bool needs_fill(SliderEmitState state) noexcept {
    if (state.has_second) {
        return state.fraction_1 > state.fraction_0;
    }

    return state.fraction_0 > 0.0;
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

LayoutRect thumb_bounds(LayoutRect bar, float thickness, double fraction) noexcept {
    const float center = bar.x + bar.width * static_cast<float>(fraction);
    const float top = bar.y + (bar.height - thickness) * 0.5F;

    return {
        .x = center - thickness * 0.5F,
        .y = top,
        .width = thickness,
        .height = thickness,
    };
}

}

SliderAppearance slider_appearance(const Theme& theme, SliderState state) noexcept {
    Srgba8 fill = theme.color(tokens::color_action_primary);

    if (state.disabled) {
        fill = theme.color(tokens::color_text_disabled);
    }

    return {
        .track = theme.color(tokens::color_action_disabled),
        .fill = fill,
        .thumb = theme.color(tokens::color_text_primary),
        .disabled_track = theme.color(tokens::color_action_disabled),
        .disabled_fill = theme.color(tokens::color_text_disabled),
    };
}

std::size_t slider_required_commands(SliderEmitState state) noexcept {
    auto required = std::size_t{2};

    if (needs_fill(state)) {
        ++required;
    }

    if (state.has_second) {
        ++required;
    }

    return required;
}

SliderOutput emit_slider(
    const Theme& theme,
    SliderEmitState state,
    SliderGeometry geometry,
    std::span<DisplayCommand> commands_out
) noexcept {
    if (!valid_bar(geometry.bar) || !valid_thickness(geometry.thickness) || !valid_state(state)) {
        return {
            .placed = 0,
            .required = 0,
            .result = SliderEmitResult::invalid_input,
        };
    }

    const std::size_t required = slider_required_commands(state);
    const SliderAppearance appearance =
        slider_appearance(theme, {.fraction = state.fraction_0, .disabled = state.disabled});

    const Srgba8 track = state.disabled ? appearance.disabled_track : appearance.track;
    const Srgba8 fill = state.disabled ? appearance.disabled_fill : appearance.fill;

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
            .color = track,
        }
    );

    if (state.has_second) {
        const float low = static_cast<float>(state.fraction_0);
        const float high = static_cast<float>(state.fraction_1);

        if (high > low) {
            push(
                FillRect{
                    .bounds =
                        {.x = geometry.bar.x + geometry.bar.width * low,
                         .y = geometry.bar.y,
                         .width = geometry.bar.width * (high - low),
                         .height = geometry.bar.height},
                    .color = fill,
                }
            );
        }

        push(
            FillRoundedRect{
                .bounds = thumb_bounds(geometry.bar, geometry.thickness, state.fraction_0),
                .radius = geometry.thickness * 0.5F,
                .color = appearance.thumb,
            }
        );

        push(
            FillRoundedRect{
                .bounds = thumb_bounds(geometry.bar, geometry.thickness, state.fraction_1),
                .radius = geometry.thickness * 0.5F,
                .color = appearance.thumb,
            }
        );
    } else {
        if (state.fraction_0 > 0.0) {
            push(
                FillRect{
                    .bounds =
                        {.x = geometry.bar.x,
                         .y = geometry.bar.y,
                         .width = geometry.bar.width * static_cast<float>(state.fraction_0),
                         .height = geometry.bar.height},
                    .color = fill,
                }
            );
        }

        push(
            FillRoundedRect{
                .bounds = thumb_bounds(geometry.bar, geometry.thickness, state.fraction_0),
                .radius = geometry.thickness * 0.5F,
                .color = appearance.thumb,
            }
        );
    }

    if (exhausted) {
        return {
            .placed = placed,
            .required = required,
            .result = SliderEmitResult::insufficient_output,
        };
    }

    return {
        .placed = placed,
        .required = required,
        .result = SliderEmitResult::complete,
    };
}

}
