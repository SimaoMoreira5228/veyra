#include "veyra/style/theme.hpp"

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

}

int main() {
    try {
        check_defaults_and_context_isolation();
        check_override_lifecycle_and_versioning();
        check_dimension_and_motion_overrides();
        check_invalid_values_and_tokens();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
