#include "veyra/style/theme.hpp"

#include <cstddef>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

void check_defaults_and_context_isolation() {
    auto first = veyra::Theme{};
    auto second = veyra::Theme{};

    require(
        first.color(veyra::tokens::color_action_primary) ==
            veyra::Srgba8{.red = 57, .green = 120, .blue = 216},
        "the default action color is available"
    );
    require(
        first.space(veyra::tokens::space_medium) == veyra::LogicalPx{8.0F},
        "the default spacing scale is available"
    );
    require(
        first.radius(veyra::tokens::radius_medium) == veyra::LogicalPx{4.0F},
        "the default radius scale is available"
    );
    require(
        first.motion(veyra::tokens::motion_fast) == veyra::Milliseconds{100},
        "the default motion scale is available"
    );

    first.set(veyra::tokens::color_action_primary, veyra::Srgba8{.red = 10, .green = 20});
    require(
        second.color(veyra::tokens::color_action_primary) ==
            veyra::Srgba8{.red = 57, .green = 120, .blue = 216},
        "overrides stay within one theme"
    );
}

void check_override_lifecycle_and_versioning() {
    auto theme = veyra::Theme{};
    const auto initial_version = theme.version();
    const auto color = veyra::Srgba8{.red = 12, .green = 34, .blue = 56, .alpha = 78};

    require(
        theme.set(veyra::tokens::color_text_primary, color) == veyra::ThemeUpdateResult::changed,
        "a new override reports a change"
    );
    require(theme.version() != initial_version, "a changed theme advances its version");
    const auto changed_version = theme.version();
    require(
        theme.set(veyra::tokens::color_text_primary, color) == veyra::ThemeUpdateResult::unchanged,
        "writing the same value is a no-op"
    );
    require(theme.version() == changed_version, "a no-op leaves the version alone");
    require(theme.color(veyra::tokens::color_text_primary) == color, "the override resolves");
    require(
        theme.clear(veyra::tokens::color_text_primary) == veyra::ThemeUpdateResult::changed,
        "clearing an override reports a change"
    );
    require(
        theme.color(veyra::tokens::color_text_primary) ==
            veyra::Srgba8{.red = 23, .green = 25, .blue = 31},
        "clearing restores the built-in value"
    );
    require(
        theme.clear(veyra::tokens::color_text_primary) == veyra::ThemeUpdateResult::unchanged,
        "clearing an absent override is a no-op"
    );
}

void check_dimension_and_motion_overrides() {
    auto theme = veyra::Theme{};

    require(
        theme.set(veyra::tokens::space_large, veyra::LogicalPx{20.0F}) ==
                veyra::ThemeUpdateResult::changed &&
            theme.space(veyra::tokens::space_large) == veyra::LogicalPx{20.0F},
        "spacing overrides resolve"
    );
    require(
        theme.clear(veyra::tokens::space_large) == veyra::ThemeUpdateResult::changed &&
            theme.space(veyra::tokens::space_large) == veyra::LogicalPx{16.0F},
        "clearing spacing restores its default"
    );
    require(
        theme.set(veyra::tokens::radius_large, veyra::LogicalPx{12.0F}) ==
                veyra::ThemeUpdateResult::changed &&
            theme.radius(veyra::tokens::radius_large) == veyra::LogicalPx{12.0F},
        "radius overrides resolve"
    );
    require(
        theme.clear(veyra::tokens::radius_large) == veyra::ThemeUpdateResult::changed &&
            theme.radius(veyra::tokens::radius_large) == veyra::LogicalPx{8.0F},
        "clearing radius restores its default"
    );
    require(
        theme.set(veyra::tokens::motion_slow, veyra::Milliseconds{500}) ==
                veyra::ThemeUpdateResult::changed &&
            theme.motion(veyra::tokens::motion_slow) == veyra::Milliseconds{500},
        "motion overrides resolve"
    );
    require(
        theme.clear(veyra::tokens::motion_slow) == veyra::ThemeUpdateResult::changed &&
            theme.motion(veyra::tokens::motion_slow) == veyra::Milliseconds{300},
        "clearing motion restores its default"
    );
}

void check_invalid_values_and_tokens() {
    auto theme = veyra::Theme{};
    const auto initial_version = theme.version();

    require(
        theme.set(veyra::tokens::space_small, veyra::LogicalPx{-1.0F}) ==
            veyra::ThemeUpdateResult::invalid_value,
        "negative spacing is rejected"
    );
    require(
        theme.set(
            veyra::tokens::radius_small,
            veyra::LogicalPx{std::numeric_limits<float>::infinity()}
        ) == veyra::ThemeUpdateResult::invalid_value,
        "non-finite radii are rejected"
    );
    require(
        theme.set(veyra::SpaceToken::count, veyra::LogicalPx{4.0F}) ==
            veyra::ThemeUpdateResult::invalid_token,
        "unknown token IDs are rejected"
    );
    require(theme.version() == initial_version, "invalid changes do not advance the version");
}

void check_invalid_tokens_leave_state_untouched() {
    auto theme = veyra::Theme{};
    const auto initial_version = theme.version();

    require(
        theme.set(veyra::ColorToken::count, veyra::Srgba8{.red = 1, .green = 2, .blue = 3}) ==
            veyra::ThemeUpdateResult::invalid_token,
        "a color set past the end is rejected"
    );
    require(
        theme.set(veyra::SpaceToken::count, veyra::LogicalPx{4.0F}) ==
            veyra::ThemeUpdateResult::invalid_token,
        "a spacing set past the end is rejected"
    );
    require(
        theme.set(veyra::RadiusToken::count, veyra::LogicalPx{4.0F}) ==
            veyra::ThemeUpdateResult::invalid_token,
        "a radius set past the end is rejected"
    );
    require(
        theme.set(veyra::MotionToken::count, veyra::Milliseconds{50}) ==
            veyra::ThemeUpdateResult::invalid_token,
        "a motion set past the end is rejected"
    );
    require(
        theme.set(veyra::SpaceToken::count, veyra::LogicalPx{-5.0F}) ==
            veyra::ThemeUpdateResult::invalid_token,
        "token validation precedes value validation"
    );
    require(
        theme.set(
            static_cast<veyra::ColorToken>(250),
            veyra::Srgba8{.red = 1, .green = 2, .blue = 3}
        ) == veyra::ThemeUpdateResult::invalid_token,
        "a far out-of-range color set is rejected"
    );
    require(
        theme.clear(veyra::ColorToken::count) == veyra::ThemeUpdateResult::invalid_token,
        "a color clear past the end is rejected"
    );
    require(
        theme.clear(veyra::SpaceToken::count) == veyra::ThemeUpdateResult::invalid_token,
        "a spacing clear past the end is rejected"
    );
    require(
        theme.clear(veyra::RadiusToken::count) == veyra::ThemeUpdateResult::invalid_token,
        "a radius clear past the end is rejected"
    );
    require(
        theme.clear(veyra::MotionToken::count) == veyra::ThemeUpdateResult::invalid_token,
        "a motion clear past the end is rejected"
    );
    require(theme.version() == initial_version, "rejected tokens leave the version alone");
    require(
        theme.color(veyra::tokens::color_surface) ==
            veyra::Srgba8{.red = 255, .green = 255, .blue = 255},
        "a rejected color set leaves the stored value alone"
    );
    require(
        theme.space(veyra::tokens::space_none) == veyra::LogicalPx{0.0F},
        "a rejected spacing set leaves the stored value alone"
    );
    require(
        theme.radius(veyra::tokens::radius_none) == veyra::LogicalPx{0.0F},
        "a rejected radius set leaves the stored value alone"
    );
    require(
        theme.motion(veyra::tokens::motion_instant) == veyra::Milliseconds{0},
        "a rejected motion set leaves the stored value alone"
    );
}

void check_unchanged_reports_per_kind() {
    auto theme = veyra::Theme{};

    require(
        theme.set(veyra::tokens::space_medium, veyra::LogicalPx{10.0F}) ==
            veyra::ThemeUpdateResult::changed,
        "a new spacing override reports a change"
    );

    const auto space_version = theme.version();

    require(
        theme.set(veyra::tokens::space_medium, veyra::LogicalPx{10.0F}) ==
            veyra::ThemeUpdateResult::unchanged,
        "rewriting the same spacing is a no-op"
    );
    require(theme.version() == space_version, "a spacing no-op leaves the version alone");
    require(
        theme.set(veyra::tokens::radius_medium, veyra::LogicalPx{6.0F}) ==
            veyra::ThemeUpdateResult::changed,
        "a new radius override reports a change"
    );

    const auto radius_version = theme.version();

    require(
        theme.set(veyra::tokens::radius_medium, veyra::LogicalPx{6.0F}) ==
            veyra::ThemeUpdateResult::unchanged,
        "rewriting the same radius is a no-op"
    );
    require(theme.version() == radius_version, "a radius no-op leaves the version alone");
    require(
        theme.set(veyra::tokens::motion_fast, veyra::Milliseconds{120}) ==
            veyra::ThemeUpdateResult::changed,
        "a new motion override reports a change"
    );

    const auto motion_version = theme.version();

    require(
        theme.set(veyra::tokens::motion_fast, veyra::Milliseconds{120}) ==
            veyra::ThemeUpdateResult::unchanged,
        "rewriting the same motion is a no-op"
    );
    require(theme.version() == motion_version, "a motion no-op leaves the version alone");
    require(
        theme.clear(veyra::tokens::space_small) == veyra::ThemeUpdateResult::unchanged,
        "clearing an absent spacing override is a no-op"
    );
    require(
        theme.clear(veyra::tokens::radius_small) == veyra::ThemeUpdateResult::unchanged,
        "clearing an absent radius override is a no-op"
    );
    require(
        theme.clear(veyra::tokens::motion_normal) == veyra::ThemeUpdateResult::unchanged,
        "clearing an absent motion override is a no-op"
    );
}

void check_version_advances_once_per_change() {
    auto theme = veyra::Theme{};
    const auto initial_version = theme.version();

    require(
        theme.set(veyra::tokens::color_error, veyra::Srgba8{.red = 9, .green = 9, .blue = 9}) ==
            veyra::ThemeUpdateResult::changed,
        "a color override advances the version"
    );
    require(theme.version() == initial_version + 1, "one color change bumps the version once");
    require(
        theme.set(veyra::tokens::space_small, veyra::LogicalPx{5.0F}) ==
            veyra::ThemeUpdateResult::changed,
        "a spacing override advances the version"
    );
    require(theme.version() == initial_version + 2, "one spacing change bumps the version once");
    require(
        theme.set(veyra::tokens::radius_small, veyra::LogicalPx{3.0F}) ==
            veyra::ThemeUpdateResult::changed,
        "a radius override advances the version"
    );
    require(theme.version() == initial_version + 3, "one radius change bumps the version once");
    require(
        theme.set(veyra::tokens::motion_fast, veyra::Milliseconds{120}) ==
            veyra::ThemeUpdateResult::changed,
        "a motion override advances the version"
    );
    require(theme.version() == initial_version + 4, "one motion change bumps the version once");
    require(
        theme.clear(veyra::tokens::color_error) == veyra::ThemeUpdateResult::changed,
        "clearing an override advances the version"
    );
    require(theme.version() == initial_version + 5, "one clear bumps the version once");
    require(theme.version() != 0, "the version never reads zero after changes");
}

void check_full_default_tables_and_clear_restores() {
    const auto fresh = veyra::Theme{};

    constexpr veyra::Srgba8 expected_colors[] = {
        veyra::Srgba8{.red = 255, .green = 255, .blue = 255},
        veyra::Srgba8{.red = 23, .green = 25, .blue = 31},
        veyra::Srgba8{.red = 57, .green = 120, .blue = 216},
        veyra::Srgba8{.red = 255, .green = 255, .blue = 255},
        veyra::Srgba8{.red = 48, .green = 105, .blue = 190},
        veyra::Srgba8{.red = 40, .green = 88, .blue = 164},
        veyra::Srgba8{.red = 208, .green = 211, .blue = 218},
        veyra::Srgba8{.red = 99, .green = 104, .blue = 116},
        veyra::Srgba8{.red = 57, .green = 120, .blue = 216},
        veyra::Srgba8{.red = 255, .green = 255, .blue = 255},
        veyra::Srgba8{.red = 176, .green = 0, .blue = 32},
    };
    constexpr veyra::LogicalPx expected_spaces[] = {
        veyra::LogicalPx{0.0F},
        veyra::LogicalPx{2.0F},
        veyra::LogicalPx{4.0F},
        veyra::LogicalPx{8.0F},
        veyra::LogicalPx{16.0F},
        veyra::LogicalPx{24.0F},
    };
    constexpr veyra::LogicalPx expected_radii[] = {
        veyra::LogicalPx{0.0F},
        veyra::LogicalPx{2.0F},
        veyra::LogicalPx{4.0F},
        veyra::LogicalPx{8.0F},
    };
    constexpr veyra::Milliseconds expected_motions[] = {
        veyra::Milliseconds{0},
        veyra::Milliseconds{100},
        veyra::Milliseconds{180},
        veyra::Milliseconds{300},
    };

    for (std::size_t index = 0; index < 11; ++index) {
        const auto token = static_cast<veyra::ColorToken>(index);

        require(fresh.color(token) == expected_colors[index], "every color falls back by default");
    }

    for (std::size_t index = 0; index < 6; ++index) {
        const auto token = static_cast<veyra::SpaceToken>(index);

        require(
            fresh.space(token) == expected_spaces[index],
            "every spacing falls back by default"
        );
    }

    for (std::size_t index = 0; index < 4; ++index) {
        const auto token = static_cast<veyra::RadiusToken>(index);

        require(fresh.radius(token) == expected_radii[index], "every radius falls back by default");
    }

    for (std::size_t index = 0; index < 4; ++index) {
        const auto token = static_cast<veyra::MotionToken>(index);

        require(
            fresh.motion(token) == expected_motions[index],
            "every motion falls back by default"
        );
    }

    auto theme = veyra::Theme{};

    require(
        theme.set(veyra::tokens::color_surface, veyra::Srgba8{.red = 1, .green = 2, .blue = 3}) ==
            veyra::ThemeUpdateResult::changed,
        "a surface override reports a change"
    );
    require(
        theme.clear(veyra::tokens::color_surface) == veyra::ThemeUpdateResult::changed &&
            theme.color(veyra::tokens::color_surface) == expected_colors[0],
        "clearing the surface restores its default"
    );
    require(
        theme.set(veyra::tokens::space_small, veyra::LogicalPx{5.0F}) ==
            veyra::ThemeUpdateResult::changed,
        "a small-spacing override reports a change"
    );
    require(
        theme.clear(veyra::tokens::space_small) == veyra::ThemeUpdateResult::changed &&
            theme.space(veyra::tokens::space_small) == expected_spaces[2],
        "clearing the small spacing restores its default"
    );
    require(
        theme.set(veyra::tokens::radius_small, veyra::LogicalPx{3.0F}) ==
            veyra::ThemeUpdateResult::changed,
        "a small-radius override reports a change"
    );
    require(
        theme.clear(veyra::tokens::radius_small) == veyra::ThemeUpdateResult::changed &&
            theme.radius(veyra::tokens::radius_small) == expected_radii[1],
        "clearing the small radius restores its default"
    );
    require(
        theme.set(veyra::tokens::motion_fast, veyra::Milliseconds{120}) ==
            veyra::ThemeUpdateResult::changed,
        "a fast-motion override reports a change"
    );
    require(
        theme.clear(veyra::tokens::motion_fast) == veyra::ThemeUpdateResult::changed &&
            theme.motion(veyra::tokens::motion_fast) == expected_motions[1],
        "clearing the fast motion restores its default"
    );
}

void check_dimension_boundaries_keep_previous() {
    auto theme = veyra::Theme{};
    const auto nan = std::numeric_limits<float>::quiet_NaN();

    require(
        theme.set(veyra::tokens::space_medium, veyra::LogicalPx{10.0F}) ==
            veyra::ThemeUpdateResult::changed,
        "a valid spacing override is stored"
    );

    const auto space_version = theme.version();

    require(
        theme.set(veyra::tokens::space_medium, veyra::LogicalPx{nan}) ==
            veyra::ThemeUpdateResult::invalid_value,
        "NaN spacing is rejected"
    );
    require(
        theme.space(veyra::tokens::space_medium) == veyra::LogicalPx{10.0F},
        "rejected spacing keeps its previous value"
    );
    require(theme.version() == space_version, "rejected spacing leaves the version alone");
    require(
        theme.set(
            veyra::tokens::space_small,
            veyra::LogicalPx{std::numeric_limits<float>::infinity()}
        ) == veyra::ThemeUpdateResult::invalid_value,
        "infinite spacing is rejected"
    );
    require(
        theme.space(veyra::tokens::space_small) == veyra::LogicalPx{4.0F},
        "rejected spacing keeps its default"
    );
    require(
        theme.set(veyra::tokens::radius_medium, veyra::LogicalPx{6.0F}) ==
            veyra::ThemeUpdateResult::changed,
        "a valid radius override is stored"
    );

    const auto radius_version = theme.version();

    require(
        theme.set(veyra::tokens::radius_medium, veyra::LogicalPx{-2.0F}) ==
            veyra::ThemeUpdateResult::invalid_value,
        "negative radii are rejected"
    );
    require(
        theme.set(veyra::tokens::radius_medium, veyra::LogicalPx{nan}) ==
            veyra::ThemeUpdateResult::invalid_value,
        "NaN radii are rejected"
    );
    require(
        theme.radius(veyra::tokens::radius_medium) == veyra::LogicalPx{6.0F},
        "rejected radii keep their previous value"
    );
    require(theme.version() == radius_version, "rejected radii leave the version alone");
    require(
        theme.set(veyra::tokens::space_extra_small, veyra::LogicalPx{0.0F}) ==
            veyra::ThemeUpdateResult::changed,
        "zero spacing is accepted"
    );
    require(
        theme.set(veyra::tokens::radius_none, veyra::LogicalPx{0.0F}) ==
            veyra::ThemeUpdateResult::changed,
        "zero radius is accepted"
    );
}

void check_out_of_range_reads_fall_back_to_first_default() {
    const auto theme = veyra::Theme{};

    require(
        theme.color(veyra::ColorToken::count) == theme.color(veyra::tokens::color_surface),
        "an out-of-range color read falls back to the first default"
    );
    require(
        theme.space(veyra::SpaceToken::count) == theme.space(veyra::tokens::space_none),
        "an out-of-range spacing read falls back to the first default"
    );
    require(
        theme.radius(veyra::RadiusToken::count) == theme.radius(veyra::tokens::radius_none),
        "an out-of-range radius read falls back to the first default"
    );
    require(
        theme.motion(veyra::MotionToken::count) == theme.motion(veyra::tokens::motion_instant),
        "an out-of-range motion read falls back to the first default"
    );
    require(
        theme.color(static_cast<veyra::ColorToken>(250)) ==
            theme.color(veyra::tokens::color_surface),
        "a far out-of-range color read falls back to the first default"
    );
    require(
        theme.space(static_cast<veyra::SpaceToken>(200)) == theme.space(veyra::tokens::space_none),
        "a far out-of-range spacing read falls back to the first default"
    );
}

void check_presets_resolve_their_palettes() {
    const auto light = veyra::Theme::light();
    const auto plain = veyra::Theme{};

    require(
        light.color(veyra::tokens::color_surface) == plain.color(veyra::tokens::color_surface) &&
            light.color(veyra::tokens::color_text_primary) ==
                plain.color(veyra::tokens::color_text_primary),
        "the light preset matches the defaults"
    );

    const auto dark = veyra::Theme::dark();

    require(
        dark.color(veyra::tokens::color_surface) ==
            veyra::Srgba8{.red = 28, .green = 30, .blue = 38},
        "the dark preset flips the surface"
    );
    require(
        dark.color(veyra::tokens::color_text_primary) ==
            veyra::Srgba8{.red = 232, .green = 234, .blue = 240},
        "the dark preset flips the text"
    );
    require(
        dark.space(veyra::tokens::space_medium) == plain.space(veyra::tokens::space_medium),
        "the dark preset keeps the default spacing"
    );

    const auto contrast = veyra::Theme::high_contrast();

    require(
        contrast.color(veyra::tokens::color_surface) ==
            veyra::Srgba8{.red = 0, .green = 0, .blue = 0},
        "the contrast preset uses a black surface"
    );
    require(
        contrast.color(veyra::tokens::color_text_primary) ==
            veyra::Srgba8{.red = 255, .green = 255, .blue = 255},
        "the contrast preset uses white text"
    );
    require(
        contrast.color(veyra::tokens::color_action_primary) ==
            veyra::Srgba8{.red = 255, .green = 205, .blue = 0},
        "the contrast preset marks actions in yellow"
    );
}

void check_apply_overlays_set_tokens() {
    auto base = veyra::Theme::light();
    auto tweaks = veyra::Theme{};
    const auto red = veyra::Srgba8{.red = 200, .green = 30, .blue = 30};

    require(
        tweaks.set(veyra::tokens::color_action_primary, red) == veyra::ThemeUpdateResult::changed,
        "the tweak theme takes the override"
    );

    const auto before = base.version();

    require(
        base.apply(tweaks) == veyra::ThemeUpdateResult::changed,
        "applying set tokens reports a change"
    );
    require(base.version() != before, "an overlay advances the version");
    require(base.color(veyra::tokens::color_action_primary) == red, "applied tokens resolve");
    require(
        base.color(veyra::tokens::color_surface) ==
            veyra::Theme{}.color(veyra::tokens::color_surface),
        "unset tokens keep the base values"
    );

    const auto applied_version = base.version();

    require(
        base.apply(tweaks) == veyra::ThemeUpdateResult::unchanged,
        "re-applying the same overlay is a no-op"
    );
    require(base.version() == applied_version, "a no-op overlay holds the version");

    auto empty = veyra::Theme{};

    require(
        empty.apply(veyra::Theme{}) == veyra::ThemeUpdateResult::unchanged,
        "an empty overlay changes nothing"
    );

    require(
        base.clear(veyra::tokens::color_action_primary) == veyra::ThemeUpdateResult::changed,
        "clearing drops the overlaid token"
    );
    require(
        base.color(veyra::tokens::color_action_primary) ==
            veyra::Theme{}.color(veyra::tokens::color_action_primary),
        "clearing reveals the base value again"
    );
}

}

int main() {
    try {
        check_defaults_and_context_isolation();
        check_override_lifecycle_and_versioning();
        check_dimension_and_motion_overrides();
        check_invalid_values_and_tokens();
        check_invalid_tokens_leave_state_untouched();
        check_unchanged_reports_per_kind();
        check_version_advances_once_per_change();
        check_full_default_tables_and_clear_restores();
        check_dimension_boundaries_keep_previous();
        check_out_of_range_reads_fall_back_to_first_default();
        check_presets_resolve_their_palettes();
        check_apply_overlays_set_tokens();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
