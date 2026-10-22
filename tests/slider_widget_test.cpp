#include "veyra/behaviors/range.hpp"
#include "veyra/widgets/basic/slider.hpp"

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

const veyra::FillRoundedRect&
as_thumb(const veyra::DisplayCommand& command, std::string_view message) {
    const auto* thumb = std::get_if<veyra::FillRoundedRect>(&command);

    require(thumb != nullptr, message);

    return *thumb;
}

void check_appearance_tracks_state() {
    const auto theme = veyra::Theme{};

    const auto idle = veyra::slider_appearance(theme, {});
    require(
        idle.track == theme.color(veyra::tokens::color_action_disabled) &&
            idle.fill == theme.color(veyra::tokens::color_action_primary) &&
            idle.thumb == theme.color(veyra::tokens::color_text_primary),
        "an idle slider rests a primary fill on a muted groove with an ink thumb"
    );
    require(
        idle.track == veyra::Srgba8{.red = 208, .green = 211, .blue = 218} &&
            idle.fill == veyra::Srgba8{.red = 57, .green = 120, .blue = 216} &&
            idle.thumb == veyra::Srgba8{.red = 23, .green = 25, .blue = 31},
        "the defaults pin the muted groove, the primary fill, and the ink thumb"
    );
    require(
        idle.disabled_track == idle.track &&
            idle.disabled_fill == theme.color(veyra::tokens::color_text_disabled),
        "the disabled slots carry the same groove and the muted fill"
    );

    const auto stopped = veyra::slider_appearance(theme, {.fraction = 0.5, .disabled = true});
    require(
        stopped.fill == stopped.disabled_fill && stopped.track == stopped.disabled_track,
        "a disabled slider resolves the muted slots, position changes nothing"
    );
    require(
        stopped.thumb == idle.thumb,
        "the thumb keeps its ink on the muted fill so it never melts into the band"
    );
}

void check_presets_keep_chrome_legible() {
    const veyra::Theme presets[] = {
        veyra::Theme::light(),
        veyra::Theme::dark(),
        veyra::Theme::high_contrast(),
    };

    for (const auto& theme : presets) {
        const auto idle = veyra::slider_appearance(theme, {.fraction = 0.5});
        require(
            idle.fill != idle.track && idle.thumb != idle.track && idle.thumb != idle.fill,
            "every preset keeps fill, groove, and thumb three distinct colors"
        );

        const auto stopped = veyra::slider_appearance(theme, {.fraction = 0.5, .disabled = true});
        require(
            stopped.fill == theme.color(veyra::tokens::color_text_disabled) &&
                stopped.fill != stopped.track && stopped.thumb != stopped.fill,
            "every preset mutes the disabled fill without collapsing the thumb into it"
        );
    }
}

void check_single_emit_geometry() {
    const auto theme = veyra::Theme{};
    constexpr veyra::LayoutRect bar{.x = 10.0F, .y = 20.0F, .width = 200.0F, .height = 8.0F};
    constexpr float thickness = 16.0F;

    std::array<veyra::DisplayCommand, 3> storage{};
    const auto half = veyra::emit_slider(
        theme,
        {.fraction_0 = 0.5},
        {.bar = bar, .thickness = thickness},
        storage
    );
    require(
        half.result == veyra::SliderEmitResult::complete && half.placed == 3 && half.required == 3,
        "a half slider paints track, fill, and thumb"
    );

    const auto track = as_fill(storage[0], "the track leads");
    require(
        track.bounds.x == 10.0F && track.bounds.width == 200.0F && track.bounds.height == 8.0F &&
            track.color == theme.color(veyra::tokens::color_action_disabled),
        "the track keeps the bar rect in the muted color"
    );

    const auto fill = as_fill(storage[1], "the fill trails the track");
    require(
        fill.bounds.x == 10.0F && fill.bounds.width == 100.0F && fill.bounds.height == 8.0F &&
            fill.color == theme.color(veyra::tokens::color_action_primary),
        "the fill covers exactly half the bar in the primary color"
    );

    const auto thumb = as_thumb(storage[2], "the thumb trails the fill");
    require(
        thumb.bounds.x == 102.0F && thumb.bounds.y == 16.0F && thumb.bounds.width == 16.0F &&
            thumb.bounds.height == 16.0F && thumb.radius == 8.0F,
        "the thumb centers on the half mark as a thickness-wide circle"
    );
    require(
        thumb.color == theme.color(veyra::tokens::color_text_primary),
        "the thumb paints in the ink color"
    );

    const auto empty = veyra::emit_slider(
        theme,
        {.fraction_0 = 0.0},
        {.bar = bar, .thickness = thickness},
        storage
    );
    require(
        empty.result == veyra::SliderEmitResult::complete && empty.placed == 2 &&
            empty.required == 2,
        "a zero slider skips its fill and keeps track plus thumb"
    );
    require(
        veyra::slider_required_commands({.fraction_0 = 0.5}) == 3 &&
            veyra::slider_required_commands({.fraction_0 = 0.0}) == 2 &&
            veyra::slider_required_commands(
                {.fraction_0 = 0.25, .fraction_1 = 0.75, .has_second = true}
            ) == 4,
        "the sizing helper matches the half, zero, and dual cases"
    );
}

void check_dual_emit_reuses_single_chrome() {
    const auto theme = veyra::Theme{};
    constexpr veyra::LayoutRect bar{.x = 0.0F, .y = 0.0F, .width = 200.0F, .height = 8.0F};
    constexpr float thickness = 12.0F;

    std::array<veyra::DisplayCommand, 4> storage{};
    const auto band = veyra::emit_slider(
        theme,
        {.fraction_0 = 0.25, .fraction_1 = 0.75, .has_second = true},
        {.bar = bar, .thickness = thickness},
        storage
    );
    require(
        band.result == veyra::SliderEmitResult::complete && band.placed == 4 && band.required == 4,
        "a ranged band paints track, band fill, and both thumbs"
    );

    const auto fill = as_fill(storage[1], "the band fill trails the track");
    require(
        fill.bounds.x == 50.0F && fill.bounds.width == 100.0F &&
            fill.color == theme.color(veyra::tokens::color_action_primary),
        "the band fill spans thumb to thumb in the primary color"
    );

    const auto low = as_thumb(storage[2], "the low thumb trails the fill");
    const auto high = as_thumb(storage[3], "the high thumb trails the low thumb");
    require(
        low.bounds.x == 50.0F - 6.0F && high.bounds.x == 150.0F - 6.0F && low.radius == 6.0F &&
            high.radius == 6.0F,
        "both thumbs center on their fractions as thickness-wide circles"
    );

    std::array<veyra::DisplayCommand, 3> single_storage{};
    const auto single = veyra::emit_slider(
        theme,
        {.fraction_0 = 0.25},
        {.bar = bar, .thickness = thickness},
        single_storage
    );
    require(
        single.result == veyra::SliderEmitResult::complete &&
            as_thumb(single_storage[2], "the single thumb trails").color == low.color &&
            low.color == high.color,
        "range thumbs reuse the single thumb color, no second paint recipe"
    );

    const auto pinched = veyra::emit_slider(
        theme,
        {.fraction_0 = 0.5, .fraction_1 = 0.5, .has_second = true},
        {.bar = bar, .thickness = thickness},
        storage
    );
    require(
        pinched.result == veyra::SliderEmitResult::complete && pinched.placed == 3 &&
            pinched.required == 3,
        "thumbs sharing a value skip the zero-width band fill"
    );
}

void check_disabled_emit_mutes_the_fill() {
    const auto theme = veyra::Theme{};
    constexpr veyra::LayoutRect bar{.x = 0.0F, .y = 0.0F, .width = 120.0F, .height = 8.0F};

    std::array<veyra::DisplayCommand, 3> storage{};
    const auto emitted = veyra::emit_slider(
        theme,
        {.fraction_0 = 0.75, .disabled = true},
        {.bar = bar, .thickness = 12.0F},
        storage
    );
    require(
        emitted.result == veyra::SliderEmitResult::complete && emitted.placed == 3,
        "a disabled slider still paints track, fill, and thumb"
    );
    require(
        as_fill(storage[1], "the muted fill trails").color ==
                theme.color(veyra::tokens::color_text_disabled) &&
            as_fill(storage[1], "the muted fill trails").bounds.width == 90.0F,
        "the disabled fill keeps its width but reads muted"
    );
    require(
        as_thumb(storage[2], "the thumb trails the muted fill").color ==
            theme.color(veyra::tokens::color_text_primary),
        "the disabled thumb keeps its ink instead of melting into the muted fill"
    );
}

void check_keyboard_map_drives_the_controllers() {
    auto volume = veyra::RangeController{};
    require(
        volume.configure({.minimum = 0.0, .maximum = 100.0, .step = 1.0, .value = 50.0}),
        "the single slider domain configures cleanly"
    );
    require(
        volume.increment().result == veyra::RangeResult::changed && volume.value() == 51.0,
        "Right or Up steps the value up"
    );
    require(
        volume.decrement().result == veyra::RangeResult::changed && volume.value() == 50.0,
        "Left or Down steps the value back down"
    );
    require(
        volume.page_increment().result == veyra::RangeResult::changed && volume.value() == 60.0,
        "PageUp jumps a full page step"
    );
    require(
        volume.page_decrement().result == veyra::RangeResult::changed && volume.value() == 50.0,
        "PageDown drops a full page step"
    );
    require(
        volume.set_to_max().result == veyra::RangeResult::changed && volume.fraction() == 1.0,
        "End parks on the maximum"
    );
    require(
        volume.set_to_min().result == veyra::RangeResult::changed && volume.fraction() == 0.0,
        "Home parks on the minimum"
    );

    auto band = veyra::RangeSliderController{};
    require(
        band.configure({.minimum = 0.0, .maximum = 100.0, .step = 1.0, .low = 25.0, .high = 75.0}),
        "the dual slider domain configures cleanly"
    );
    require(
        band.increment(veyra::RangeThumb::low).result == veyra::RangeResult::changed &&
            band.low() == 26.0,
        "arrows move the focused thumb, here the low one"
    );
    require(
        band.decrement(veyra::RangeThumb::high).result == veyra::RangeResult::changed &&
            band.high() == 74.0,
        "the same keys move the high thumb when it holds focus"
    );
    require(
        band.set_to_min(veyra::RangeThumb::low).result == veyra::RangeResult::changed &&
            band.fraction(veyra::RangeThumb::low) == 0.0,
        "Home parks the focused thumb on the minimum"
    );
    require(
        band.set_to_max(veyra::RangeThumb::high).result == veyra::RangeResult::changed &&
            band.fraction(veyra::RangeThumb::high) == 1.0,
        "End parks the focused thumb on the maximum"
    );
}

void check_invalid_input_rejects_loudly() {
    const auto theme = veyra::Theme{};
    constexpr veyra::LayoutRect bar{.x = 0.0F, .y = 0.0F, .width = 100.0F, .height = 8.0F};

    std::array<veyra::DisplayCommand, 4> storage{};

    for (const auto geometry :
         {veyra::SliderGeometry{
              .bar = {.x = 0.0F, .y = 0.0F, .width = 0.0F, .height = 8.0F},
              .thickness = 12.0F,
          },
          veyra::SliderGeometry{
              .bar = {.x = 0.0F, .y = 0.0F, .width = -10.0F, .height = 8.0F},
              .thickness = 12.0F,
          },
          veyra::SliderGeometry{
              .bar = {.x = 0.0F, .y = 0.0F, .width = 100.0F, .height = 0.0F},
              .thickness = 12.0F,
          },
          veyra::SliderGeometry{.bar = bar, .thickness = 0.0F},
          veyra::SliderGeometry{.bar = bar, .thickness = -4.0F}}) {
        const auto rejected = veyra::emit_slider(theme, {.fraction_0 = 0.5}, geometry, storage);
        require(
            rejected.result == veyra::SliderEmitResult::invalid_input && rejected.placed == 0 &&
                rejected.required == 0,
            "a degenerate bar or thumb size rejects with nothing placed"
        );
    }

    const auto drifting = veyra::emit_slider(
        theme,
        {.fraction_0 = 0.5},
        {.bar =
             {.x = std::numeric_limits<float>::quiet_NaN(),
              .y = 0.0F,
              .width = 100.0F,
              .height = 8.0F},
         .thickness = 12.0F},
        storage
    );
    require(
        drifting.result == veyra::SliderEmitResult::invalid_input,
        "a non-finite bar origin rejects the emit"
    );

    const auto melting = veyra::emit_slider(
        theme,
        {.fraction_0 = 0.5},
        {.bar = bar, .thickness = std::numeric_limits<float>::infinity()},
        storage
    );
    require(
        melting.result == veyra::SliderEmitResult::invalid_input,
        "a non-finite thumb size rejects the emit"
    );

    for (const double fraction : {std::numeric_limits<double>::quiet_NaN(), -0.1, 1.1}) {
        const auto rejected = veyra::emit_slider(
            theme,
            {.fraction_0 = fraction},
            {.bar = bar, .thickness = 12.0F},
            storage
        );
        require(
            rejected.result == veyra::SliderEmitResult::invalid_input && rejected.placed == 0,
            "an out-of-domain fraction rejects instead of painting a lie"
        );
    }

    const auto crossed = veyra::emit_slider(
        theme,
        {.fraction_0 = 0.75, .fraction_1 = 0.25, .has_second = true},
        {.bar = bar, .thickness = 12.0F},
        storage
    );
    require(
        crossed.result == veyra::SliderEmitResult::invalid_input && crossed.placed == 0,
        "crossed thumbs reject, the block policy never lets low pass high"
    );
}

void check_overflow_keeps_a_whole_prefix() {
    const auto theme = veyra::Theme{};
    constexpr veyra::LayoutRect bar{.x = 0.0F, .y = 0.0F, .width = 100.0F, .height = 8.0F};

    std::array<veyra::DisplayCommand, 4> full{};
    const auto complete = veyra::emit_slider(
        theme,
        {.fraction_0 = 0.25, .fraction_1 = 0.75, .has_second = true},
        {.bar = bar, .thickness = 12.0F},
        full
    );
    require(
        complete.result == veyra::SliderEmitResult::complete && complete.placed == 4,
        "the reference dual emit fits its span"
    );

    std::array<veyra::DisplayCommand, 2> tight{};
    const auto output = veyra::emit_slider(
        theme,
        {.fraction_0 = 0.25, .fraction_1 = 0.75, .has_second = true},
        {.bar = bar, .thickness = 12.0F},
        tight
    );
    require(
        output.result == veyra::SliderEmitResult::insufficient_output,
        "a short span reports its overflow"
    );
    require(output.placed == 2 && output.required == 4, "the report keeps placed and required");
    require(
        as_fill(tight[0], "the short span keeps the track").color ==
                as_fill(full[0], "the reference span opens with the track").color &&
            as_fill(tight[1], "the short span keeps the fill").bounds.width ==
                as_fill(full[1], "the reference fill lands").bounds.width,
        "the short span keeps a whole-command prefix of the full emit"
    );

    std::array<veyra::DisplayCommand, 0> starved{};
    const auto empty =
        veyra::emit_slider(theme, {.fraction_0 = 0.5}, {.bar = bar, .thickness = 12.0F}, starved);
    require(
        empty.result == veyra::SliderEmitResult::insufficient_output && empty.placed == 0 &&
            empty.required == 3,
        "a slider with no room reports all three commands missing"
    );
}

void check_theme_override_repaints_chrome() {
    auto theme = veyra::Theme{};
    theme.set(veyra::tokens::color_action_disabled, veyra::Srgba8{30, 32, 40});
    theme.set(veyra::tokens::color_action_primary, veyra::Srgba8{90, 160, 255});
    theme.set(veyra::tokens::color_text_disabled, veyra::Srgba8{110, 115, 130});
    theme.set(veyra::tokens::color_text_primary, veyra::Srgba8{240, 242, 248});

    const auto look = veyra::slider_appearance(theme, {.fraction = 0.5});
    require(
        look.track == veyra::Srgba8{30, 32, 40} && look.fill == veyra::Srgba8{90, 160, 255} &&
            look.thumb == veyra::Srgba8{240, 242, 248},
        "the slider follows the overridden groove, fill, and ink"
    );

    const auto stopped = veyra::slider_appearance(theme, {.fraction = 0.5, .disabled = true});
    require(
        stopped.fill == veyra::Srgba8{110, 115, 130} &&
            stopped.disabled_fill == veyra::Srgba8{110, 115, 130},
        "the disabled slider follows the overridden muted token"
    );

    auto scoped = veyra::Theme::dark();
    auto tweaks = veyra::Theme{};
    tweaks.set(veyra::tokens::color_action_primary, veyra::Srgba8{10, 120, 200});
    require(
        scoped.apply(tweaks) == veyra::ThemeUpdateResult::changed,
        "the scoped layer applies cleanly"
    );

    constexpr veyra::LayoutRect bar{.x = 0.0F, .y = 0.0F, .width = 100.0F, .height = 8.0F};
    std::array<veyra::DisplayCommand, 3> storage{};
    const auto emitted =
        veyra::emit_slider(theme, {.fraction_0 = 0.5}, {.bar = bar, .thickness = 12.0F}, storage);
    require(
        emitted.result == veyra::SliderEmitResult::complete &&
            as_fill(storage[0], "the dark groove leads").color == veyra::Srgba8{30, 32, 40} &&
            as_fill(storage[1], "the bright fill trails").color == veyra::Srgba8{90, 160, 255},
        "the emitted paints track the same overrides"
    );
    require(
        as_thumb(storage[2], "the ink thumb trails").color == veyra::Srgba8{240, 242, 248},
        "the emitted thumb tracks the overridden ink"
    );
}

}

int main() {
    try {
        check_appearance_tracks_state();
        check_presets_keep_chrome_legible();
        check_single_emit_geometry();
        check_dual_emit_reuses_single_chrome();
        check_disabled_emit_mutes_the_fill();
        check_keyboard_map_drives_the_controllers();
        check_invalid_input_rejects_loudly();
        check_overflow_keeps_a_whole_prefix();
        check_theme_override_repaints_chrome();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
