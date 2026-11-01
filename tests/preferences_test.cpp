#include "veyra/behaviors/control/pressable.hpp"
#include "veyra/foundation/preferences.hpp"
#include "veyra/media/animation/animation_interpolation.hpp"
#include "veyra/platform/headless/fake_services.hpp"
#include "veyra/style/theme/theme_scope.hpp"

#include <cmath>
#include <cstdint>
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

void check_text_scale_is_bounded() {
    using veyra::sanitize_text_scale;

    require(sanitize_text_scale(1.25F) == 1.25F, "a normal scale is preserved");
    require(sanitize_text_scale(0.1F) == veyra::kMinimumTextScale, "a tiny scale is clamped");
    require(sanitize_text_scale(99.0F) == veyra::kMaximumTextScale, "a huge scale is clamped");
    require(sanitize_text_scale(-2.0F) == 1.0F, "a negative scale falls back");
    require(sanitize_text_scale(0.0F) == 1.0F, "zero falls back");
    require(
        sanitize_text_scale(std::numeric_limits<float>::quiet_NaN()) == 1.0F,
        "a non-finite scale falls back"
    );
}

void check_system_preferences_are_sanitized() {
    auto preferences = veyra::SystemPreferences{
        .text_scale = 8.0F,
        .reduced_motion = true,
        .high_contrast = true,
        .color_scheme = veyra::ColorScheme::dark,
        .version = 7,
    };

    const auto sanitized = veyra::sanitize_system_preferences(preferences);
    require(sanitized.text_scale == veyra::kMaximumTextScale, "the scale is bounded");
    require(sanitized.reduced_motion && sanitized.high_contrast, "flags survive sanitizing");
    require(sanitized.color_scheme == veyra::ColorScheme::dark, "the scheme survives");
    require(sanitized.version == 7, "the provider version is preserved");

    require(
        veyra::read_system_preferences(nullptr, nullptr).text_scale == 1.0F,
        "a missing provider yields defaults"
    );
}

veyra::SystemPreferences dark_motion_source(void*) noexcept {
    return {
        .text_scale = 2.0F,
        .reduced_motion = true,
        .high_contrast = false,
        .color_scheme = veyra::ColorScheme::dark,
        .version = 11,
    };
}

void check_provider_source_is_applied() {
    const auto preferences = veyra::read_system_preferences(dark_motion_source, nullptr);
    require(preferences.text_scale == 2.0F, "the source supplies a text scale");
    require(preferences.reduced_motion, "the source supplies reduced motion");
    require(preferences.color_scheme == veyra::ColorScheme::dark, "the source supplies a scheme");
    require(preferences.version == 11, "the source supplies a version");
}

void check_variant_mapping() {
    using veyra::ColorScheme;
    using veyra::ThemeVariant;

    require(
        veyra::theme_variant_for_system(ColorScheme::light, false) == ThemeVariant::light,
        "light maps to light"
    );
    require(
        veyra::theme_variant_for_system(ColorScheme::dark, false) == ThemeVariant::dark,
        "dark maps to dark"
    );
    require(
        veyra::theme_variant_for_system(ColorScheme::light, true) == ThemeVariant::high_contrast &&
            veyra::theme_variant_for_system(ColorScheme::dark, true) == ThemeVariant::high_contrast,
        "contrast wins over the scheme"
    );
}

void check_scope_text_scale_and_motion() {
    const auto base = veyra::Theme::light();
    veyra::ThemeScope root(base);

    require(root.text_scale() == 1.0F, "a fresh scope is unscaled");
    require(!root.reduced_motion(), "a fresh scope allows motion");

    const auto root_version = root.version();
    require(root.set_text_scale(1.5F), "setting a scale reports a change");
    require(root.text_scale() == 1.5F, "the scope exposes the scale");
    require(root.version() != root_version, "a scale changes the scope version");
    require(!root.set_text_scale(1.5F), "repeating a scale reports no change");

    veyra::ThemeScope child(root, veyra::ThemeVariant::inherit);
    require(child.text_scale() == 1.5F, "a child inherits the scale");
    require(child.set_text_scale(2.0F), "a child can override the scale");
    require(child.text_scale() == 2.0F, "the child reports its own scale");
    require(root.text_scale() == 1.5F, "the parent scale is untouched");
    require(child.clear_text_scale(), "a child can drop its scale");
    require(child.text_scale() == 1.5F, "dropping the scale inherits again");

    require(root.set_reduced_motion(true), "reduced motion can be enabled");
    require(root.reduced_motion(), "the root reports reduced motion");
    require(child.reduced_motion(), "a child inherits reduced motion");
    require(
        root.motion(veyra::tokens::motion_normal) == veyra::Milliseconds{0},
        "reduced motion collapses theme durations"
    );

    require(child.set_reduced_motion(false), "a child can override reduced motion");
    require(!child.reduced_motion(), "the child allows motion");
    require(
        child.motion(veyra::tokens::motion_normal) != veyra::Milliseconds{0},
        "the child restores token durations"
    );
    require(root.reduced_motion(), "the parent stays reduced");
    require(child.clear_reduced_motion(), "the child can drop the override");
    require(child.reduced_motion(), "dropping the override inherits the parent");

    auto scoped = veyra::ThemeScope(base);
    scoped.apply_preferences(
        {.text_scale = 1.75F,
         .reduced_motion = true,
         .high_contrast = true,
         .color_scheme = veyra::ColorScheme::dark}
    );
    require(scoped.variant() == veyra::ThemeVariant::high_contrast, "preferences pick the variant");
    require(scoped.text_scale() == 1.75F, "preferences carry the text scale");
    require(scoped.reduced_motion(), "preferences carry reduced motion");
}

void check_animator_applies_preferences() {
    auto preferences = veyra::SystemPreferences{};
    preferences.reduced_motion = true;

    auto animator = veyra::ScalarAnimator{};
    animator.apply_preferences(preferences);
    animator.start(0.0F, 1.0F, 0, 100, veyra::AnimationEasing::linear);
    require(animator.value() == 1.0F, "reduced motion snaps a new animation");
    require(animator.finished(), "a snapped animation is finished");

    auto mid_flight = veyra::ScalarAnimator{};
    mid_flight.start(0.0F, 1.0F, 0, 100, veyra::AnimationEasing::linear);
    require(mid_flight.advance(40) == 0.4F, "the animation advances before the preference");
    mid_flight.apply_preferences(preferences);
    require(mid_flight.value() == 1.0F, "engaging reduced motion snaps to the target");
    require(mid_flight.finished(), "the snapped animation finishes");

    auto tint = veyra::PressableTintAnimator{};
    tint.apply_preferences(preferences);
    tint.tick({.hovered = true}, 0);
    require(tint.tint().hover == 1.0F, "the tint snaps to its hover target");
}

void check_fake_provider_tracks_changes() {
    auto services = veyra::FakeServices{};
    const auto initial_version = services.system_preferences_version();
    require(services.system_preferences().text_scale == 1.0F, "fakes start unscaled");

    auto preferences = veyra::SystemPreferences{
        .text_scale = 12.0F,
        .reduced_motion = true,
        .high_contrast = true,
        .color_scheme = veyra::ColorScheme::dark,
    };
    services.set_system_preferences(preferences);

    const auto stored = services.system_preferences();
    require(stored.text_scale == veyra::kMaximumTextScale, "the fake sanitizes its input");
    require(stored.reduced_motion && stored.high_contrast, "the fake stores flags");
    require(
        services.system_preferences_version() != initial_version,
        "a change advances the version"
    );

    const auto changed_version = services.system_preferences_version();
    services.set_system_preferences(stored);
    require(
        services.system_preferences_version() == changed_version,
        "an identical update keeps the version"
    );

    services.reset();
    require(
        services.system_preferences() == veyra::SystemPreferences{},
        "reset clears preferences"
    );
    require(services.system_preferences_version() == 0, "reset clears the version");
}

}

int main() {
    try {
        check_text_scale_is_bounded();
        check_system_preferences_are_sanitized();
        check_provider_source_is_applied();
        check_variant_mapping();
        check_scope_text_scale_and_motion();
        check_animator_applies_preferences();
        check_fake_provider_tracks_changes();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';

        return 1;
    }

    std::cout << "system preference checks passed\n";

    return 0;
}
