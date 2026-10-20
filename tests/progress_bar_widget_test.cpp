#include "veyra/widgets/data/progress_bar.hpp"

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

void check_appearance_tracks_state() {
    const auto theme = veyra::Theme{};

    const auto idle = veyra::progress_bar_appearance(theme, {});
    require(
        idle.track == theme.color(veyra::tokens::color_action_disabled) &&
            idle.fill == theme.color(veyra::tokens::color_action_primary) &&
            idle.indeterminate_block == theme.color(veyra::tokens::color_action_primary),
        "an idle bar rests on a muted track with a primary fill"
    );
    require(
        idle.track == veyra::Srgba8{.red = 208, .green = 211, .blue = 218} &&
            idle.fill == veyra::Srgba8{.red = 57, .green = 120, .blue = 216},
        "the defaults pin the muted groove and the primary fill"
    );

    const auto busy = veyra::progress_bar_appearance(theme, {.indeterminate = true});
    require(
        busy.indeterminate_block == idle.fill && busy.track == idle.track,
        "a busy bar slides the same primary block along the same track"
    );

    const auto stopped = veyra::progress_bar_appearance(theme, {.fraction = 0.5, .canceled = true});
    require(
        stopped.fill == theme.color(veyra::tokens::color_text_disabled) &&
            stopped.indeterminate_block == theme.color(veyra::tokens::color_text_disabled) &&
            stopped.track == idle.track,
        "a canceled bar mutes both fills and keeps the track"
    );
}

void check_determinate_emit_geometry() {
    const auto theme = veyra::Theme{};
    constexpr veyra::LayoutRect bar{.x = 10.0F, .y = 20.0F, .width = 200.0F, .height = 8.0F};

    std::array<veyra::DisplayCommand, 2> storage{};
    const auto empty = veyra::emit_progress_bar(theme, {}, {.bar = bar}, storage);
    require(
        empty.result == veyra::ProgressBarEmitResult::complete && empty.placed == 1 &&
            empty.required == 1,
        "a zero bar paints only its track"
    );
    require(
        as_fill(storage[0], "the track leads").bounds.width == 200.0F,
        "the track spans the full bar"
    );

    const auto half = veyra::emit_progress_bar(theme, {.fraction = 0.5}, {.bar = bar}, storage);
    require(
        half.result == veyra::ProgressBarEmitResult::complete && half.placed == 2 &&
            half.required == 2,
        "a half bar takes track plus fill"
    );

    const auto track = as_fill(storage[0], "the track still leads");
    const auto fill = as_fill(storage[1], "the fill trails the track");
    require(
        track.bounds.x == 10.0F && track.bounds.width == 200.0F &&
            track.color == theme.color(veyra::tokens::color_action_disabled),
        "the track keeps the bar rect in the muted color"
    );
    require(
        fill.bounds.x == 10.0F && fill.bounds.width == 100.0F && fill.bounds.height == 8.0F &&
            fill.color == theme.color(veyra::tokens::color_action_primary),
        "the fill covers exactly half the bar in the primary color"
    );

    const auto full = veyra::emit_progress_bar(theme, {.fraction = 1.0}, {.bar = bar}, storage);
    require(
        full.result == veyra::ProgressBarEmitResult::complete &&
            as_fill(storage[1], "the full fill trails").bounds.width == 200.0F,
        "a complete bar fills edge to edge"
    );

    require(
        veyra::progress_bar_required_commands({.fraction = 0.5}) == 2 &&
            veyra::progress_bar_required_commands({}) == 1 &&
            veyra::progress_bar_required_commands({.indeterminate = true}) == 2,
        "the sizing helper matches the half, empty, and busy cases"
    );
}

void check_canceled_emit_mutes_the_fill() {
    const auto theme = veyra::Theme{};
    constexpr veyra::LayoutRect bar{.x = 0.0F, .y = 0.0F, .width = 120.0F, .height = 8.0F};

    std::array<veyra::DisplayCommand, 2> storage{};
    const auto emitted = veyra::emit_progress_bar(
        theme,
        {.fraction = 0.75, .canceled = true},
        {.bar = bar},
        storage
    );
    require(
        emitted.result == veyra::ProgressBarEmitResult::complete && emitted.placed == 2,
        "a canceled bar still paints track plus fill"
    );
    require(
        as_fill(storage[1], "the muted fill trails").color ==
                theme.color(veyra::tokens::color_text_disabled) &&
            as_fill(storage[1], "the muted fill trails").bounds.width == 90.0F,
        "the canceled fill keeps its width but reads muted"
    );

    const auto busy_stopped = veyra::emit_progress_bar(
        theme,
        {.indeterminate = true, .canceled = true},
        {.bar = bar, .animation_tick_ms = 300},
        storage
    );
    require(
        busy_stopped.result == veyra::ProgressBarEmitResult::complete &&
            as_fill(storage[1], "the muted block trails").color ==
                theme.color(veyra::tokens::color_text_disabled),
        "a canceled spinner mutes its sliding block too"
    );
}

void check_indeterminate_block_follows_caller_time() {
    const auto theme = veyra::Theme{};
    constexpr veyra::LayoutRect bar{.x = 4.0F, .y = 6.0F, .width = 200.0F, .height = 8.0F};
    constexpr std::uint64_t period = veyra::kProgressBarIndeterminatePeriodMs;

    std::array<veyra::DisplayCommand, 2> first{};
    std::array<veyra::DisplayCommand, 2> second{};

    const auto run_at = [&](std::uint64_t tick_ms, std::span<veyra::DisplayCommand> output) {
        return veyra::emit_progress_bar(
            theme,
            {.indeterminate = true},
            {.bar = bar, .animation_tick_ms = tick_ms},
            output
        );
    };

    require(
        run_at(300, first).result == veyra::ProgressBarEmitResult::complete &&
            run_at(300, second).result == veyra::ProgressBarEmitResult::complete,
        "both samples at the same tick complete"
    );

    const auto block_a = as_fill(first[1], "the first block trails its track");
    const auto block_b = as_fill(second[1], "the second block trails its track");
    require(
        block_a.bounds.x == block_b.bounds.x && block_a.bounds.width == 50.0F &&
            block_a.bounds.height == 8.0F,
        "frozen caller time freezes the quarter-width block"
    );
    require(
        block_a.bounds.x == 4.0F + 150.0F * 0.25F,
        "a quarter period sits a quarter down the run"
    );

    std::array<veyra::DisplayCommand, 2> wrapped{};
    require(
        run_at(300 + period, wrapped).result == veyra::ProgressBarEmitResult::complete &&
            as_fill(wrapped[1], "the wrapped block trails").bounds.x == block_a.bounds.x,
        "one period later the block wraps to the same spot"
    );

    std::array<veyra::DisplayCommand, 2> moved{};
    require(
        run_at(900, moved).result == veyra::ProgressBarEmitResult::complete &&
            as_fill(moved[1], "the moved block trails").bounds.x != block_a.bounds.x,
        "a later tick slides the block down the track"
    );
}

void check_invalid_input_rejects_loudly() {
    const auto theme = veyra::Theme{};
    constexpr veyra::LayoutRect bar{.x = 0.0F, .y = 0.0F, .width = 100.0F, .height = 8.0F};

    std::array<veyra::DisplayCommand, 2> storage{};

    for (const auto geometry :
         {veyra::ProgressBarGeometry{.bar = {.x = 0.0F, .y = 0.0F, .width = 0.0F, .height = 8.0F}},
          veyra::ProgressBarGeometry{
              .bar = {.x = 0.0F, .y = 0.0F, .width = -10.0F, .height = 8.0F}
          },
          veyra::ProgressBarGeometry{
              .bar = {.x = 0.0F, .y = 0.0F, .width = 100.0F, .height = 0.0F}
          }}) {
        const auto rejected = veyra::emit_progress_bar(theme, {.fraction = 0.5}, geometry, storage);
        require(
            rejected.result == veyra::ProgressBarEmitResult::invalid_input &&
                rejected.placed == 0 && rejected.required == 0,
            "a degenerate bar rect rejects with nothing placed"
        );
    }

    for (const double fraction : {std::numeric_limits<double>::quiet_NaN(), -0.1, 1.1}) {
        const auto rejected =
            veyra::emit_progress_bar(theme, {.fraction = fraction}, {.bar = bar}, storage);
        require(
            rejected.result == veyra::ProgressBarEmitResult::invalid_input && rejected.placed == 0,
            "an out-of-domain fraction rejects instead of painting a lie"
        );
    }
}

void check_overflow_keeps_a_whole_prefix() {
    const auto theme = veyra::Theme{};
    constexpr veyra::LayoutRect bar{.x = 0.0F, .y = 0.0F, .width = 100.0F, .height = 8.0F};

    std::array<veyra::DisplayCommand, 1> tight{};
    const auto starved = veyra::emit_progress_bar(theme, {.fraction = 0.5}, {.bar = bar}, tight);
    require(
        starved.result == veyra::ProgressBarEmitResult::insufficient_output &&
            starved.placed == 1 && starved.required == 2,
        "a short span keeps the track and reports the missing fill"
    );

    std::array<veyra::DisplayCommand, 0> empty{};
    const auto bare = veyra::emit_progress_bar(theme, {}, {.bar = bar}, empty);
    require(
        bare.result == veyra::ProgressBarEmitResult::insufficient_output && bare.required == 1,
        "even a lone track overflows loudly with no room"
    );
}

void check_theme_override_repaints_chrome() {
    auto theme = veyra::Theme{};
    theme.set(veyra::tokens::color_action_disabled, veyra::Srgba8{30, 32, 40});
    theme.set(veyra::tokens::color_action_primary, veyra::Srgba8{90, 160, 255});
    theme.set(veyra::tokens::color_text_disabled, veyra::Srgba8{110, 115, 130});

    const auto look = veyra::progress_bar_appearance(theme, {.fraction = 0.5});
    require(
        look.track == veyra::Srgba8{30, 32, 40} && look.fill == veyra::Srgba8{90, 160, 255},
        "the bar follows the overridden groove and primary"
    );

    const auto stopped = veyra::progress_bar_appearance(theme, {.fraction = 0.5, .canceled = true});
    require(
        stopped.fill == veyra::Srgba8{110, 115, 130} &&
            stopped.indeterminate_block == veyra::Srgba8{110, 115, 130},
        "the canceled bar follows the overridden muted token"
    );

    constexpr veyra::LayoutRect bar{.x = 0.0F, .y = 0.0F, .width = 100.0F, .height = 8.0F};
    std::array<veyra::DisplayCommand, 2> storage{};
    const auto emitted = veyra::emit_progress_bar(theme, {.fraction = 0.5}, {.bar = bar}, storage);
    require(
        emitted.result == veyra::ProgressBarEmitResult::complete &&
            as_fill(storage[0], "the dark groove leads").color == veyra::Srgba8{30, 32, 40} &&
            as_fill(storage[1], "the bright fill trails").color == veyra::Srgba8{90, 160, 255},
        "the emitted paints track the same overrides"
    );
}

}

int main() {
    try {
        check_appearance_tracks_state();
        check_determinate_emit_geometry();
        check_canceled_emit_mutes_the_fill();
        check_indeterminate_block_follows_caller_time();
        check_invalid_input_rejects_loudly();
        check_overflow_keeps_a_whole_prefix();
        check_theme_override_repaints_chrome();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
