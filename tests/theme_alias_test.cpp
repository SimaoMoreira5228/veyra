#include "veyra/style/theme/theme_file.hpp"
#include "veyra/style/theme/theme_scope.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
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

void check_alias_resolves_and_reports() {
    auto theme = veyra::Theme{};
    const auto accent = veyra::Srgba8{.red = 210, .green = 40, .blue = 90};

    require(
        theme.set(veyra::tokens::color_text_primary, accent, veyra::ThemeLayer::file, 3) ==
            veyra::ThemeUpdateResult::changed,
        "the alias source accepts a value"
    );
    require(
        theme.set_alias(
            veyra::tokens::color_action_primary,
            veyra::tokens::color_text_primary,
            veyra::ThemeLayer::file,
            4
        ) == veyra::ThemeUpdateResult::changed,
        "a color alias is accepted"
    );

    require(
        theme.color(veyra::tokens::color_action_primary) == accent,
        "an alias resolves to its source value"
    );
    require(
        theme.alias(veyra::tokens::color_action_primary) ==
            std::optional{veyra::tokens::color_text_primary},
        "the alias target is observable"
    );

    const auto provenance = theme.provenance(
        veyra::TokenKind::color,
        static_cast<std::uint8_t>(veyra::tokens::color_action_primary)
    );
    require(
        provenance.token == static_cast<std::uint8_t>(veyra::tokens::color_text_primary),
        "provenance names the token that supplied the value"
    );
    require(
        provenance.layer == veyra::ThemeLayer::file && provenance.line == 4,
        "provenance keeps the alias declaration site"
    );
}

void check_alias_chains_and_assignment_replaces_alias() {
    auto theme = veyra::Theme{};
    const auto accent = veyra::Srgba8{.red = 10, .green = 120, .blue = 200};
    theme.set(veyra::tokens::color_text_primary, accent);

    require(
        theme.set_alias(veyra::tokens::color_action_primary, veyra::tokens::color_text_primary) ==
            veyra::ThemeUpdateResult::changed,
        "the first link is accepted"
    );
    require(
        theme.set_alias(veyra::tokens::color_action_hovered, veyra::tokens::color_action_primary) ==
            veyra::ThemeUpdateResult::changed,
        "the second link is accepted"
    );
    require(
        theme.color(veyra::tokens::color_action_hovered) == accent,
        "a chain resolves to the final value"
    );

    const auto direct = veyra::Srgba8{.red = 220, .green = 220, .blue = 10};
    theme.set(veyra::tokens::color_action_primary, direct);
    require(!theme.alias(veyra::tokens::color_action_primary), "a direct value replaces the alias");
    require(
        theme.color(veyra::tokens::color_action_hovered) == direct,
        "the remaining chain sees the direct value"
    );
}

void check_alias_cycles_are_rejected() {
    auto theme = veyra::Theme{};

    require(
        theme.set_alias(veyra::tokens::color_action_primary, veyra::tokens::color_action_primary) ==
            veyra::ThemeUpdateResult::alias_cycle,
        "a self alias is a cycle"
    );
    require(
        theme.set_alias(veyra::tokens::color_action_primary, veyra::tokens::color_text_primary) ==
            veyra::ThemeUpdateResult::changed,
        "a valid alias is accepted"
    );
    require(
        theme.set_alias(veyra::tokens::color_text_primary, veyra::tokens::color_action_primary) ==
            veyra::ThemeUpdateResult::alias_cycle,
        "a two-token cycle is rejected"
    );
    require(
        theme.set_alias(veyra::tokens::color_text_primary, veyra::tokens::color_action_hovered) ==
            veyra::ThemeUpdateResult::changed,
        "a second valid link is accepted"
    );
    require(
        theme.set_alias(veyra::tokens::color_action_hovered, veyra::tokens::color_action_primary) ==
            veyra::ThemeUpdateResult::alias_cycle,
        "a longer cycle is rejected"
    );
}

void check_clear_and_apply_track_aliases() {
    auto base = veyra::Theme{};
    const auto accent = veyra::Srgba8{.red = 1, .green = 2, .blue = 3};
    base.set(veyra::tokens::color_text_primary, accent);
    base.set_alias(veyra::tokens::color_action_primary, veyra::tokens::color_text_primary);

    auto target = veyra::Theme{};
    require(target.apply(base) == veyra::ThemeUpdateResult::changed, "apply copies an alias");
    require(
        target.color(veyra::tokens::color_action_primary) == accent,
        "the copied alias resolves"
    );

    const auto replacement = veyra::Srgba8{.red = 9, .green = 9, .blue = 9};
    auto literal = veyra::Theme{};
    literal.set(veyra::tokens::color_action_primary, replacement);
    require(
        target.apply(literal) == veyra::ThemeUpdateResult::changed,
        "apply replaces an alias with a literal"
    );
    require(!target.alias(veyra::tokens::color_action_primary), "the literal removed the alias");
    require(
        target.color(veyra::tokens::color_action_primary) == replacement,
        "the literal value wins"
    );

    require(
        target.clear(veyra::tokens::color_action_primary) == veyra::ThemeUpdateResult::changed,
        "clear removes an aliased token"
    );
    require(
        target.color(veyra::tokens::color_action_primary) ==
            veyra::Theme{}.color(veyra::tokens::color_action_primary),
        "clearing returns to the default value"
    );
}

void check_scope_resolves_aliases_through_the_base() {
    auto base = veyra::Theme{};
    const auto accent = veyra::Srgba8{.red = 77, .green = 66, .blue = 55};
    base.set(veyra::tokens::color_text_primary, accent);
    base.set_alias(veyra::tokens::color_action_primary, veyra::tokens::color_text_primary);

    veyra::ThemeScope scope(base);
    require(
        scope.color(veyra::tokens::color_action_primary) == accent,
        "a scope resolves a base alias"
    );

    veyra::ThemeScope dark(base, veyra::ThemeVariant::dark);
    require(
        dark.color(veyra::tokens::color_action_primary) ==
            veyra::theme_for_variant(veyra::ThemeVariant::dark)
                .color(veyra::tokens::color_action_primary),
        "a variant palette replaces the aliased base value"
    );

    veyra::ThemeScope local(base);
    const auto local_value = veyra::Srgba8{.red = 4, .green = 5, .blue = 6};
    local.set_alias(veyra::tokens::color_action_primary, veyra::tokens::color_error);
    local.set(veyra::tokens::color_error, local_value);
    require(
        local.color(veyra::tokens::color_action_primary) == local_value,
        "a scope-local alias resolves inside the scope"
    );
    require(
        scope.color(veyra::tokens::color_action_primary) == accent,
        "a scope-local alias does not leak"
    );
}

void check_theme_files_round_trip_aliases() {
    constexpr std::string_view source = "schema: 1\ncolor_action_primary = @color_text_primary\n";
    auto theme = veyra::Theme{};
    auto error = veyra::ThemeFileError{};

    require(
        veyra::parse_theme_file(
            std::span<const char>(source.data(), source.size()),
            theme,
            error
        ) == veyra::ThemeFileResult::ok,
        "an alias parses"
    );
    require(
        theme.alias(veyra::tokens::color_action_primary) ==
            std::optional{veyra::tokens::color_text_primary},
        "the parsed alias points at its source"
    );

    std::array<char, 512> buffer{};
    auto used = std::size_t{};
    require(
        veyra::write_theme_file(theme, buffer, used) == veyra::ThemeFileResult::ok,
        "an aliased theme writes"
    );
    require(
        std::string_view(buffer.data(), used).find("color_action_primary = @color_text_primary") !=
            std::string_view::npos,
        "the writer keeps alias syntax"
    );

    auto reparsed = veyra::Theme{};
    require(
        veyra::parse_theme_file(std::span<const char>(buffer.data(), used), reparsed, error) ==
            veyra::ThemeFileResult::ok,
        "the written alias parses again"
    );
    require(
        reparsed.alias(veyra::tokens::color_action_primary) ==
            std::optional{veyra::tokens::color_text_primary},
        "the round trip preserves the alias"
    );
}

void check_theme_files_reject_bad_aliases() {
    auto theme = veyra::Theme{};
    auto error = veyra::ThemeFileError{};

    constexpr std::string_view unknown = "schema: 1\ncolor_action_primary = @color_missing\n";
    require(
        veyra::parse_theme_file(
            std::span<const char>(unknown.data(), unknown.size()),
            theme,
            error
        ) == veyra::ThemeFileResult::unknown_token,
        "an unknown alias target is rejected"
    );

    constexpr std::string_view wrong_kind = "schema: 1\nspace_small = @color_surface\n";
    require(
        veyra::parse_theme_file(
            std::span<const char>(wrong_kind.data(), wrong_kind.size()),
            theme,
            error
        ) == veyra::ThemeFileResult::unknown_token,
        "a cross-kind alias is rejected"
    );

    constexpr std::string_view cycle = "schema: 1\ncolor_action_primary = @color_text_primary\n"
                                       "color_text_primary = @color_action_primary\n";
    require(
        veyra::parse_theme_file(std::span<const char>(cycle.data(), cycle.size()), theme, error) ==
            veyra::ThemeFileResult::alias_cycle,
        "a file cycle is reported"
    );
    require(error.line == 3, "the cycle reports the offending line");
}

}

int main() {
    try {
        check_alias_resolves_and_reports();
        check_alias_chains_and_assignment_replaces_alias();
        check_alias_cycles_are_rejected();
        check_clear_and_apply_track_aliases();
        check_scope_resolves_aliases_through_the_base();
        check_theme_files_round_trip_aliases();
        check_theme_files_reject_bad_aliases();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';

        return 1;
    }

    std::cout << "theme alias checks passed\n";

    return 0;
}
