#include "veyra/style/theme/theme_file.hpp"
#include "veyra/widgets/basic/button.hpp"

#include <array>
#include <cstring>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

std::span<const char> to_input(std::string_view text) {
    return std::span<const char>(text.data(), text.size());
}

veyra::Theme parse_ok(std::string_view text) {
    auto overlay = veyra::Theme{};
    auto error = veyra::ThemeFileError{};
    require(
        veyra::parse_theme_file(to_input(text), overlay, error) == veyra::ThemeFileResult::ok,
        "a valid file parses"
    );
    require(error.code == veyra::ThemeFileResult::ok, "a valid file reports ok");
    return overlay;
}

veyra::ThemeFileError parse_fails(std::string_view text, veyra::ThemeFileResult expected) {
    auto overlay = veyra::Theme{};
    auto error = veyra::ThemeFileError{};
    const auto result = veyra::parse_theme_file(to_input(text), overlay, error);
    require(result == expected, "the failure reports the expected code");
    require(error.code == expected, "the error struct reports the expected code");
    return error;
}

std::size_t write_ok(const veyra::Theme& theme, std::span<char> buffer) {
    auto used = std::size_t{};
    require(
        veyra::write_theme_file(theme, buffer, used) == veyra::ThemeFileResult::ok,
        "a fitting buffer writes"
    );
    require(used <= buffer.size(), "the writer reports bytes inside the buffer");
    return used;
}

bool themes_match(const veyra::Theme& first, const veyra::Theme& second) {
    for (std::size_t index = 0; index < 11; ++index) {
        const auto token = static_cast<veyra::ColorToken>(index);
        if (first.color(token) != second.color(token)) {
            return false;
        }
    }
    for (std::size_t index = 0; index < 6; ++index) {
        const auto token = static_cast<veyra::SpaceToken>(index);
        if (first.space(token) != second.space(token)) {
            return false;
        }
    }
    for (std::size_t index = 0; index < 4; ++index) {
        const auto token = static_cast<veyra::RadiusToken>(index);
        if (first.radius(token) != second.radius(token)) {
            return false;
        }
    }
    for (std::size_t index = 0; index < 4; ++index) {
        const auto token = static_cast<veyra::MotionToken>(index);
        if (first.motion(token) != second.motion(token)) {
            return false;
        }
    }
    return true;
}

std::size_t write_preset(const veyra::Theme& preset, std::span<char> buffer) {
    const auto used = write_ok(preset, buffer);
    const auto overlay = parse_ok(std::string_view(buffer.data(), used));
    require(themes_match(overlay, preset), "a parsed preset resolves every token");

    std::array<char, 2048> again{};
    const auto reused = write_ok(overlay, again);
    require(reused == used, "a rewrite keeps the byte count");
    require(std::memcmp(buffer.data(), again.data(), used) == 0, "a rewrite is byte-identical");
    return used;
}

void check_presets_round_trip() {
    std::array<char, 2048> buffer{};

    const auto light_used = write_preset(veyra::Theme::light(), buffer);
    require(
        light_used == 10 && std::string_view(buffer.data(), light_used) == "schema: 1\n",
        "the light preset writes schema-only"
    );
    require(
        themes_match(parse_ok("schema: 1\n"), veyra::Theme::light()),
        "a schema-only file is an empty overlay"
    );

    const auto dark_used = write_preset(veyra::Theme::dark(), buffer);
    require(dark_used > light_used, "the dark preset writes its overrides");
    const std::string_view dark_text(buffer.data(), dark_used);
    require(
        dark_text.find("color_action_disabled") < dark_text.find("color_surface") &&
            dark_text.find("color_surface") < dark_text.find("motion_fast"),
        "override lines stay sorted"
    );

    write_preset(veyra::Theme::high_contrast(), buffer);
}

void check_schema_missing_and_wrong() {
    const auto empty = parse_fails("", veyra::ThemeFileResult::invalid_schema);
    require(empty.line == 1 && empty.column == 1, "an empty file fails on line one");

    const auto blanks = parse_fails("# note\n\n  \n", veyra::ThemeFileResult::invalid_schema);
    require(blanks.line == 4, "a schema-less file fails past its last line");

    const auto missing =
        parse_fails("color_surface = #ffffff\n", veyra::ThemeFileResult::invalid_schema);
    require(missing.line == 1, "a token line cannot stand in for the schema");

    const auto wrong = parse_fails("schema: 2\n", veyra::ThemeFileResult::invalid_schema);
    require(wrong.line == 1, "an unknown schema version fails on its line");

    parse_fails("Schema: 1\n", veyra::ThemeFileResult::invalid_schema);
    parse_fails("schema: 01\n", veyra::ThemeFileResult::invalid_schema);
    parse_fails("schema 1\n", veyra::ThemeFileResult::invalid_schema);
    parse_fails("schema: 1 extra\n", veyra::ThemeFileResult::invalid_schema);

    parse_ok("schema = 1\n");
    parse_ok("  schema: 1  \n");
    parse_ok("schema: 1");
    parse_ok("schema: 1\r\ncolor_surface = #ff0000\r\n");
}

void check_unknown_token_reports_line() {
    const auto error = parse_fails(
        "# theme\n\nschema: 1\ncolor_surface = #ffffff\n  bogus_token = 1\n",
        veyra::ThemeFileResult::unknown_token
    );
    require(error.line == 5, "an unknown token reports its own line");
    require(error.column == 3, "an unknown token reports its name column");
    parse_fails("schema: 1\nColor_Surface = #ffffff\n", veyra::ThemeFileResult::unknown_token);
    parse_fails("schema: 1\nschema = 1\n", veyra::ThemeFileResult::unknown_token);
}

void check_bad_values_report_line_and_column() {
    const auto hex =
        parse_fails("schema: 1\ncolor_surface = #xyz\n", veyra::ThemeFileResult::bad_value);
    require(hex.line == 2, "a bad color reports its own line");
    require(hex.column == 17, "a bad color reports its value column");

    parse_fails("schema: 1\ncolor_surface = #fff\n", veyra::ThemeFileResult::bad_value);
    parse_fails("schema: 1\ncolor_surface = #fffffff\n", veyra::ThemeFileResult::bad_value);
    parse_fails("schema: 1\ncolor_surface = ff0000\n", veyra::ThemeFileResult::bad_value);
    parse_fails("schema: 1\ncolor_surface = #gggggg\n", veyra::ThemeFileResult::bad_value);
    parse_fails("schema: 1\ncolor_surface = #ff0000 extra\n", veyra::ThemeFileResult::bad_value);
    parse_fails("schema: 1\ncolor_surface =\n", veyra::ThemeFileResult::bad_value);
    parse_fails("schema: 1\nspace_medium = abc\n", veyra::ThemeFileResult::bad_value);
    parse_fails("schema: 1\nspace_medium = 8pxx\n", veyra::ThemeFileResult::bad_value);
    parse_fails("schema: 1\nspace_medium = 8 8\n", veyra::ThemeFileResult::bad_value);
    parse_fails("schema: 1\nspace_medium = -2\n", veyra::ThemeFileResult::bad_value);
    parse_fails("schema: 1\nspace_medium =\n", veyra::ThemeFileResult::bad_value);
    parse_fails("schema: 1\ncolor_surface #ff0000\n", veyra::ThemeFileResult::bad_value);
    parse_fails("schema: 1\nmotion_fast = 12.5\n", veyra::ThemeFileResult::bad_value);
    parse_fails("schema: 1\nmotion_fast = -3\n", veyra::ThemeFileResult::bad_value);
    parse_fails("schema: 1\nmotion_fast = abc\n", veyra::ThemeFileResult::bad_value);
    parse_fails("schema: 1\nmotion_fast = 99999999999\n", veyra::ThemeFileResult::bad_value);
}

void check_duplicate_token_rejected() {
    const auto error = parse_fails(
        "schema: 1\ncolor_surface = #ffffff\ncolor_surface = #000000\n",
        veyra::ThemeFileResult::duplicate_token
    );
    require(error.line == 3, "a duplicate reports its second line");
    require(error.column == 1, "a duplicate reports its name column");
}

void check_comments_blanks_and_whitespace() {
    const auto overlay = parse_ok(
        "# leading comment\n"
        "\n"
        "   # indented comment\n"
        "schema: 1\n"
        "\n"
        "color_surface\t=\t#112233\n"
        "   space_medium   =   10px  \n"
        "motion_fast=120\n"
    );
    require(
        overlay.color(veyra::tokens::color_surface) ==
            veyra::Srgba8{.red = 17, .green = 34, .blue = 51},
        "tabs around the separator parse"
    );
    require(
        overlay.space(veyra::tokens::space_medium) == veyra::LogicalPx{10.0F},
        "a px suffix parses"
    );
    require(overlay.motion(veyra::tokens::motion_fast) == veyra::Milliseconds{120}, "tabs parse");
}

void check_color_and_dimension_forms() {
    const auto overlay = parse_ok(
        "schema: 1\n"
        "color_surface = #FF0000\n"
        "color_error = #11223344\n"
        "space_small = 2.5\n"
        "space_medium = 10PX\n"
        "radius_large = 8\n"
        "motion_slow = 0\n"
    );
    require(
        overlay.color(veyra::tokens::color_surface) ==
            veyra::Srgba8{.red = 255, .green = 0, .blue = 0, .alpha = 255},
        "uppercase hex parses with full alpha"
    );
    require(
        overlay.color(veyra::tokens::color_error) ==
            veyra::Srgba8{.red = 17, .green = 34, .blue = 51, .alpha = 68},
        "eight-digit hex keeps its alpha"
    );
    require(
        overlay.space(veyra::tokens::space_small) == veyra::LogicalPx{2.5F},
        "a fractional spacing parses"
    );
    require(
        overlay.space(veyra::tokens::space_medium) == veyra::LogicalPx{10.0F},
        "an uppercase px suffix parses"
    );
    require(
        overlay.radius(veyra::tokens::radius_large) == veyra::LogicalPx{8.0F},
        "a bare radius parses"
    );

    std::array<char, 2048> buffer{};
    const auto used = write_ok(overlay, buffer);
    const auto reparsed = parse_ok(std::string_view(buffer.data(), used));
    require(themes_match(reparsed, overlay), "dimension forms survive a rewrite");
    require(
        std::string_view(buffer.data(), used).find("10px") == std::string_view::npos,
        "the writer stays suffix-free"
    );
}

void check_truncated_output() {
    std::array<char, 2048> room{};
    const auto dark_used = write_ok(veyra::Theme::dark(), room);

    std::array<char, 16> small{};
    auto used = std::size_t{};
    require(
        veyra::write_theme_file(veyra::Theme::dark(), small, used) ==
            veyra::ThemeFileResult::truncated_output,
        "a small buffer truncates"
    );
    require(
        used == 10 && std::string_view(small.data(), used) == "schema: 1\n",
        "a truncated write keeps complete lines only"
    );
    require(dark_used > small.size(), "the fixture exceeds the small buffer");

    std::array<char, 10> exact{};
    require(write_ok(veyra::Theme::light(), exact) == 10, "an exact buffer fits the schema line");

    std::array<char, 9> short_fall{};
    auto short_used = std::size_t{};
    require(
        veyra::write_theme_file(veyra::Theme::light(), short_fall, short_used) ==
            veyra::ThemeFileResult::truncated_output,
        "a short schema buffer truncates"
    );
    require(short_used == 0, "a failed schema line writes nothing");
}

void check_overlay_never_touches_the_base() {
    const auto overlay = parse_ok("schema: 1\ncolor_action_primary = #c81e1e\n");

    auto base = veyra::Theme::dark();
    const auto surface_before = base.color(veyra::tokens::color_surface);
    require(
        base.apply(overlay) == veyra::ThemeUpdateResult::changed,
        "a parsed overlay applies onto a base"
    );
    require(
        base.color(veyra::tokens::color_action_primary) ==
            veyra::Srgba8{.red = 200, .green = 30, .blue = 30},
        "an applied overlay resolves"
    );
    require(
        base.color(veyra::tokens::color_surface) == surface_before,
        "unset tokens keep the base values"
    );

    auto dirty = veyra::Theme::dark();
    auto error = veyra::ThemeFileError{};
    require(
        veyra::parse_theme_file(to_input("schema: 1\ncolor_surface = #zzzzzz\n"), dirty, error) ==
            veyra::ThemeFileResult::bad_value,
        "a bad file fails"
    );
    require(themes_match(dirty, veyra::Theme::light()), "a failed parse leaves an empty overlay");
}

void check_file_theme_drives_widget_colors() {
    const auto overlay = parse_ok("schema: 1\ncolor_action_primary = #c81e1e\n");

    auto app = veyra::Theme::dark();
    require(
        app.apply(overlay) == veyra::ThemeUpdateResult::changed,
        "the file overlay applies onto the app theme"
    );

    const auto idle = veyra::button_appearance(app, {});
    require(
        idle.background == veyra::Srgba8{.red = 200, .green = 30, .blue = 30},
        "a file-driven button uses the file color idle"
    );
    require(
        idle.foreground == app.color(veyra::tokens::color_action_on_primary),
        "a file-driven button keeps the base foreground"
    );

    const auto hovered =
        veyra::button_appearance(app, veyra::PressableState{.hovered = true, .focused = true});
    require(
        hovered.background == veyra::Theme::dark().color(veyra::tokens::color_action_hovered),
        "a file-driven button keeps the base hover color"
    );
}

}

int main() {
    try {
        check_presets_round_trip();
        check_schema_missing_and_wrong();
        check_unknown_token_reports_line();
        check_bad_values_report_line_and_column();
        check_duplicate_token_rejected();
        check_comments_blanks_and_whitespace();
        check_color_and_dimension_forms();
        check_truncated_output();
        check_overlay_never_touches_the_base();
        check_file_theme_drives_widget_colors();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
