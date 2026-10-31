#include "veyra/style/theme/theme_scope.hpp"

#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

void check_inherit_resolves_the_base_theme() {
    auto base = veyra::Theme::light();
    const auto override = veyra::Srgba8{.red = 12, .green = 34, .blue = 56};
    base.set(veyra::tokens::color_text_primary, override, veyra::ThemeLayer::file, 7);

    const veyra::ThemeScope scope(base);

    require(scope.variant() == veyra::ThemeVariant::inherit, "a bare scope inherits");
    require(scope.color(veyra::tokens::color_text_primary) == override, "base override resolves");
    require(
        scope.space(veyra::tokens::space_medium) == base.space(veyra::tokens::space_medium),
        "base spacing resolves"
    );
    require(
        scope.radius(veyra::tokens::radius_medium) == base.radius(veyra::tokens::radius_medium),
        "base radius resolves"
    );
    require(
        scope.motion(veyra::tokens::motion_normal) == base.motion(veyra::tokens::motion_normal),
        "base motion resolves"
    );

    const auto provenance = scope.provenance(
        veyra::TokenKind::color,
        static_cast<std::uint8_t>(veyra::tokens::color_text_primary)
    );
    require(provenance.layer == veyra::ThemeLayer::file, "base provenance passes through");
    require(provenance.line == 7, "base source line passes through");
}

void check_variant_resolves_every_token_kind() {
    const auto base = veyra::Theme::light();
    veyra::ThemeScope scope(base, veyra::ThemeVariant::dark);

    const auto& dark = veyra::theme_for_variant(veyra::ThemeVariant::dark);

    require(
        scope.color(veyra::tokens::color_action_primary) ==
            dark.color(veyra::tokens::color_action_primary),
        "a variant supplies colors"
    );
    require(
        scope.space(veyra::tokens::space_large) == dark.space(veyra::tokens::space_large),
        "a variant supplies spacing"
    );
    require(
        scope.radius(veyra::tokens::radius_small) == dark.radius(veyra::tokens::radius_small),
        "a variant supplies radii"
    );
    require(
        scope.motion(veyra::tokens::motion_fast) == dark.motion(veyra::tokens::motion_fast),
        "a variant supplies motion"
    );

    const auto provenance = scope.provenance(
        veyra::TokenKind::color,
        static_cast<std::uint8_t>(veyra::tokens::color_action_primary)
    );
    require(provenance.layer == veyra::ThemeLayer::preset, "a variant reports preset provenance");

    const auto high = veyra::Theme::high_contrast();
    scope.set_variant(veyra::ThemeVariant::high_contrast);
    require(
        scope.color(veyra::tokens::color_surface) == high.color(veyra::tokens::color_surface),
        "switching variant replaces the resolved palette"
    );
}

void check_local_override_beats_the_variant() {
    const auto base = veyra::Theme::light();
    veyra::ThemeScope scope(base, veyra::ThemeVariant::dark);

    const auto local = veyra::Srgba8{.red = 200, .green = 10, .blue = 90};
    require(
        scope.set(veyra::tokens::color_error, local) == veyra::ThemeUpdateResult::changed,
        "a scope accepts a local override"
    );
    require(scope.color(veyra::tokens::color_error) == local, "a local override beats the variant");
    require(
        scope.provenance(
                 veyra::TokenKind::color,
                 static_cast<std::uint8_t>(veyra::tokens::color_error)
        )
                .layer == veyra::ThemeLayer::override,
        "a local override reports override provenance"
    );

    scope.set_variant(veyra::ThemeVariant::high_contrast);
    require(
        scope.color(veyra::tokens::color_error) == local,
        "a variant switch keeps local overrides"
    );

    require(
        scope.clear(veyra::tokens::color_error) == veyra::ThemeUpdateResult::changed,
        "a scope clears a local override"
    );
    require(
        scope.color(veyra::tokens::color_error) ==
            veyra::theme_for_variant(veyra::ThemeVariant::high_contrast)
                .color(veyra::tokens::color_error),
        "clearing reveals the active variant"
    );
}

void check_nested_scopes_resolve_through_the_parent() {
    const auto base = veyra::Theme::light();
    veyra::ThemeScope parent(base, veyra::ThemeVariant::dark);
    veyra::ThemeScope inherited(parent, veyra::ThemeVariant::inherit);
    veyra::ThemeScope local_light(parent, veyra::ThemeVariant::light);

    const auto& dark = veyra::theme_for_variant(veyra::ThemeVariant::dark);
    const auto& light = veyra::theme_for_variant(veyra::ThemeVariant::light);

    require(inherited.parent() == &parent, "a child scope keeps its parent link");
    require(inherited.variant() == veyra::ThemeVariant::inherit, "a child inherits by default");
    require(
        inherited.color(veyra::tokens::color_surface) == dark.color(veyra::tokens::color_surface),
        "an inheriting child sees the parent variant"
    );
    require(
        local_light.color(veyra::tokens::color_surface) ==
            light.color(veyra::tokens::color_surface),
        "a child variant scopes only that subtree"
    );
    require(
        parent.color(veyra::tokens::color_surface) == dark.color(veyra::tokens::color_surface),
        "a child variant leaves the parent alone"
    );

    parent.set_variant(veyra::ThemeVariant::high_contrast);
    const auto& high = veyra::theme_for_variant(veyra::ThemeVariant::high_contrast);

    require(
        inherited.color(veyra::tokens::color_surface) == high.color(veyra::tokens::color_surface),
        "a live parent switch reaches inheriting children"
    );
    require(
        local_light.color(veyra::tokens::color_surface) ==
            light.color(veyra::tokens::color_surface),
        "a live parent switch leaves an explicit child variant alone"
    );
}

void check_versions_track_scope_inputs() {
    auto base = veyra::Theme::light();
    veyra::ThemeScope parent(base, veyra::ThemeVariant::dark);
    veyra::ThemeScope child(parent, veyra::ThemeVariant::inherit);

    const auto parent_version = parent.version();
    const auto child_version = child.version();
    require(child_version != parent_version, "a child version includes its parent");

    parent.set_variant(veyra::ThemeVariant::high_contrast);
    require(parent.version() != parent_version, "a variant switch advances the version");
    require(child.version() != child_version, "a parent switch advances a child version");

    const auto before_base = parent.version();
    base.set(veyra::tokens::color_surface, veyra::Srgba8{.red = 1, .green = 2, .blue = 3});
    require(parent.version() != before_base, "a base theme change advances the scope version");

    const auto before_local = parent.version();
    parent.set(veyra::tokens::color_surface, veyra::Srgba8{.red = 9, .green = 8, .blue = 7});
    require(parent.version() != before_local, "a local override advances the scope version");
}

}

int main() {
    try {
        check_inherit_resolves_the_base_theme();
        check_variant_resolves_every_token_kind();
        check_local_override_beats_the_variant();
        check_nested_scopes_resolve_through_the_parent();
        check_versions_track_scope_inputs();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';

        return 1;
    }

    std::cout << "theme scope checks passed\n";

    return 0;
}
