#include "veyra/style/theme/theme_file.hpp"

#include <array>
#include <charconv>
#include <cmath>
#include <string_view>
#include <system_error>

namespace veyra {

namespace {

enum class TokenKind : std::uint8_t {
    color,
    space,
    radius,
    motion,
};

struct TokenEntry {
    std::string_view name;
    TokenKind kind;
    std::uint8_t index;
};

constexpr std::array<TokenEntry, 25> token_table{{
    {"color_action_disabled", TokenKind::color, 6},
    {"color_action_hovered", TokenKind::color, 4},
    {"color_action_on_primary", TokenKind::color, 3},
    {"color_action_pressed", TokenKind::color, 5},
    {"color_action_primary", TokenKind::color, 2},
    {"color_error", TokenKind::color, 10},
    {"color_surface", TokenKind::color, 0},
    {"color_text_disabled", TokenKind::color, 7},
    {"color_text_primary", TokenKind::color, 1},
    {"color_toggle_checked", TokenKind::color, 8},
    {"color_toggle_on_checked", TokenKind::color, 9},
    {"motion_fast", TokenKind::motion, 1},
    {"motion_instant", TokenKind::motion, 0},
    {"motion_normal", TokenKind::motion, 2},
    {"motion_slow", TokenKind::motion, 3},
    {"radius_large", TokenKind::radius, 3},
    {"radius_medium", TokenKind::radius, 2},
    {"radius_none", TokenKind::radius, 0},
    {"radius_small", TokenKind::radius, 1},
    {"space_extra_large", TokenKind::space, 5},
    {"space_extra_small", TokenKind::space, 1},
    {"space_large", TokenKind::space, 4},
    {"space_medium", TokenKind::space, 3},
    {"space_none", TokenKind::space, 0},
    {"space_small", TokenKind::space, 2},
}};

constexpr bool is_blank(char value) noexcept {
    return value == ' ' || value == '\t' || value == '\r';
}

constexpr std::string_view trim(std::string_view text) noexcept {
    while (!text.empty() && is_blank(text.front())) {
        text.remove_prefix(1);
    }
    while (!text.empty() && is_blank(text.back())) {
        text.remove_suffix(1);
    }
    return text;
}

constexpr std::uint32_t column_of(std::string_view line, std::string_view part) noexcept {
    return static_cast<std::uint32_t>(part.data() - line.data()) + 1;
}

bool hex_digit(char value, std::uint8_t& digit_out) noexcept {
    if (value >= '0' && value <= '9') {
        digit_out = static_cast<std::uint8_t>(value - '0');
        return true;
    }
    if (value >= 'a' && value <= 'f') {
        digit_out = static_cast<std::uint8_t>(value - 'a' + 10);
        return true;
    }
    if (value >= 'A' && value <= 'F') {
        digit_out = static_cast<std::uint8_t>(value - 'A' + 10);
        return true;
    }
    return false;
}

bool parse_color(std::string_view text, Srgba8& color_out) noexcept {
    if (text.empty() || text.front() != '#') {
        return false;
    }
    text.remove_prefix(1);
    if (text.size() != 6 && text.size() != 8) {
        return false;
    }

    std::uint8_t bytes[4]{255, 255, 255, 255};
    const std::size_t pairs = text.size() / 2;
    for (std::size_t pair = 0; pair < pairs; ++pair) {
        std::uint8_t high{};
        std::uint8_t low{};
        if (!hex_digit(text[pair * 2], high) || !hex_digit(text[pair * 2 + 1], low)) {
            return false;
        }
        bytes[pair] = static_cast<std::uint8_t>(high * 16 + low);
    }

    color_out = Srgba8{
        .red = bytes[0],
        .green = bytes[1],
        .blue = bytes[2],
        .alpha = text.size() == 6 ? std::uint8_t{255} : bytes[3],
    };
    return true;
}

bool has_px_suffix(std::string_view text) noexcept {
    if (text.size() < 3) {
        return false;
    }
    const char second = text[text.size() - 2];
    const char last = text[text.size() - 1];
    return (second == 'p' || second == 'P') && (last == 'x' || last == 'X');
}

bool parse_decimal(std::string_view text, float& value_out) noexcept {
    std::string_view core = text;
    if (has_px_suffix(core)) {
        core.remove_suffix(2);
    }
    if (core.empty()) {
        return false;
    }

    auto dots = std::size_t{};
    auto digits = std::size_t{};
    for (char step : core) {
        if (step == '.') {
            ++dots;
            continue;
        }
        if (step < '0' || step > '9') {
            return false;
        }
        ++digits;
    }
    if (dots > 1 || digits == 0) {
        return false;
    }

    float value{};
    const auto outcome = std::from_chars(core.data(), core.data() + core.size(), value);
    if (outcome.ec != std::errc{} || outcome.ptr != core.data() + core.size()) {
        return false;
    }
    if (!std::isfinite(value)) {
        return false;
    }
    value_out = value;
    return true;
}

bool parse_motion(std::string_view text, Milliseconds& value_out) noexcept {
    if (text.empty()) {
        return false;
    }
    for (char step : text) {
        if (step < '0' || step > '9') {
            return false;
        }
    }

    std::uint32_t value{};
    const auto outcome = std::from_chars(text.data(), text.data() + text.size(), value);
    if (outcome.ec != std::errc{} || outcome.ptr != text.data() + text.size()) {
        return false;
    }
    value_out = Milliseconds{value};
    return true;
}

bool parse_schema(std::string_view content) noexcept {
    const auto separator = content.find_first_of("=:");

    if (separator == std::string_view::npos) {
        return false;
    }

    return trim(content.substr(0, separator)) == "schema" &&
        trim(content.substr(separator + 1)) == "1";
}

bool append_text(std::span<char> buffer, std::size_t& used, std::string_view chunk) noexcept {
    if (chunk.size() > buffer.size() - used) {
        return false;
    }
    for (std::size_t index = 0; index < chunk.size(); ++index) {
        buffer[used + index] = chunk[index];
    }
    used += chunk.size();
    return true;
}

bool append_value(std::span<char> line, std::size_t& length, std::string_view chunk) noexcept {
    if (chunk.size() > line.size() - length) {
        return false;
    }
    for (std::size_t index = 0; index < chunk.size(); ++index) {
        line[length + index] = chunk[index];
    }
    length += chunk.size();
    return true;
}

bool append_hex(std::span<char> line, std::size_t& length, std::uint8_t byte) noexcept {
    constexpr char digits[] = "0123456789abcdef";
    const char pair[2] = {digits[byte >> 4], digits[byte & 0x0F]};
    return append_value(line, length, std::string_view{pair, 2});
}

bool format_color(std::span<char> line, std::size_t& length, Srgba8 color) noexcept {
    if (!append_value(line, length, "#")) {
        return false;
    }
    if (!append_hex(line, length, color.red) || !append_hex(line, length, color.green) ||
        !append_hex(line, length, color.blue)) {
        return false;
    }
    if (color.alpha == 255) {
        return true;
    }
    return append_hex(line, length, color.alpha);
}

bool format_float(std::span<char> line, std::size_t& length, float value) noexcept {
    if (line.size() <= length) {
        return false;
    }
    const auto outcome = std::to_chars(line.data() + length, line.data() + line.size(), value);
    if (outcome.ec != std::errc{}) {
        return false;
    }
    length = static_cast<std::size_t>(outcome.ptr - line.data());
    return true;
}

bool format_motion(std::span<char> line, std::size_t& length, Milliseconds value) noexcept {
    if (line.size() <= length) {
        return false;
    }
    const auto outcome =
        std::to_chars(line.data() + length, line.data() + line.size(), value.value);
    if (outcome.ec != std::errc{}) {
        return false;
    }
    length = static_cast<std::size_t>(outcome.ptr - line.data());
    return true;
}

ThemeUpdateResult store_alias(
    Theme& overlay,
    const TokenEntry& entry,
    const TokenEntry& source,
    std::uint32_t line
) noexcept {
    switch (entry.kind) {
    case TokenKind::color:
        return overlay.set_alias(
            static_cast<ColorToken>(entry.index),
            static_cast<ColorToken>(source.index),
            ThemeLayer::file,
            line
        );
    case TokenKind::space:
        return overlay.set_alias(
            static_cast<SpaceToken>(entry.index),
            static_cast<SpaceToken>(source.index),
            ThemeLayer::file,
            line
        );
    case TokenKind::radius:
        return overlay.set_alias(
            static_cast<RadiusToken>(entry.index),
            static_cast<RadiusToken>(source.index),
            ThemeLayer::file,
            line
        );
    case TokenKind::motion:
        return overlay.set_alias(
            static_cast<MotionToken>(entry.index),
            static_cast<MotionToken>(source.index),
            ThemeLayer::file,
            line
        );
    }

    return ThemeUpdateResult::invalid_token;
}

std::string_view table_name(TokenKind kind, std::uint8_t token) noexcept {
    for (const auto& entry : token_table) {
        if (entry.kind == kind && entry.index == token) {
            return entry.name;
        }
    }

    return {};
}

bool format_alias(
    const Theme& theme,
    const TokenEntry& entry,
    std::span<char> line,
    std::size_t& length
) noexcept {
    std::string_view source;

    switch (entry.kind) {
    case TokenKind::color: {
        const auto alias = theme.alias(static_cast<ColorToken>(entry.index));
        if (!alias) {
            return false;
        }
        source = table_name(entry.kind, static_cast<std::uint8_t>(*alias));

        break;
    }
    case TokenKind::space: {
        const auto alias = theme.alias(static_cast<SpaceToken>(entry.index));
        if (!alias) {
            return false;
        }
        source = table_name(entry.kind, static_cast<std::uint8_t>(*alias));

        break;
    }
    case TokenKind::radius: {
        const auto alias = theme.alias(static_cast<RadiusToken>(entry.index));
        if (!alias) {
            return false;
        }
        source = table_name(entry.kind, static_cast<std::uint8_t>(*alias));

        break;
    }
    case TokenKind::motion: {
        const auto alias = theme.alias(static_cast<MotionToken>(entry.index));
        if (!alias) {
            return false;
        }
        source = table_name(entry.kind, static_cast<std::uint8_t>(*alias));

        break;
    }
    }

    return !source.empty() && append_value(line, length, "@") &&
        append_value(line, length, source) && append_value(line, length, "\n");
}

bool store_token(
    Theme& overlay,
    const TokenEntry& entry,
    std::string_view value,
    std::uint32_t line
) noexcept {
    const auto color_token = static_cast<ColorToken>(entry.index);
    const auto space_token = static_cast<SpaceToken>(entry.index);
    const auto radius_token = static_cast<RadiusToken>(entry.index);
    const auto motion_token = static_cast<MotionToken>(entry.index);

    if (entry.kind == TokenKind::color) {
        Srgba8 color{};
        if (!parse_color(value, color)) {
            return false;
        }
        return overlay.set(color_token, color, ThemeLayer::file, line) !=
            ThemeUpdateResult::invalid_token;
    }
    if (entry.kind == TokenKind::space) {
        float parsed{};
        if (!parse_decimal(value, parsed)) {
            return false;
        }
        return overlay.set(space_token, LogicalPx{parsed}, ThemeLayer::file, line) !=
            ThemeUpdateResult::invalid_value;
    }
    if (entry.kind == TokenKind::radius) {
        float parsed{};
        if (!parse_decimal(value, parsed)) {
            return false;
        }
        return overlay.set(radius_token, LogicalPx{parsed}, ThemeLayer::file, line) !=
            ThemeUpdateResult::invalid_value;
    }

    Milliseconds parsed{};
    if (!parse_motion(value, parsed)) {
        return false;
    }
    return overlay.set(motion_token, parsed, ThemeLayer::file, line) !=
        ThemeUpdateResult::invalid_token;
}

bool has_alias(const Theme& theme, const TokenEntry& entry) noexcept {
    switch (entry.kind) {
    case TokenKind::color:
        return theme.alias(static_cast<ColorToken>(entry.index)).has_value();
    case TokenKind::space:
        return theme.alias(static_cast<SpaceToken>(entry.index)).has_value();
    case TokenKind::radius:
        return theme.alias(static_cast<RadiusToken>(entry.index)).has_value();
    case TokenKind::motion:
        return theme.alias(static_cast<MotionToken>(entry.index)).has_value();
    }

    return false;
}

bool line_differs(const Theme& theme, const Theme& defaults, const TokenEntry& entry) noexcept {
    if (has_alias(theme, entry)) {
        return true;
    }
    if (entry.kind == TokenKind::color) {
        const auto token = static_cast<ColorToken>(entry.index);
        return theme.color(token) != defaults.color(token);
    }
    if (entry.kind == TokenKind::space) {
        const auto token = static_cast<SpaceToken>(entry.index);
        return theme.space(token) != defaults.space(token);
    }
    if (entry.kind == TokenKind::radius) {
        const auto token = static_cast<RadiusToken>(entry.index);
        return theme.radius(token) != defaults.radius(token);
    }
    const auto token = static_cast<MotionToken>(entry.index);
    return theme.motion(token) != defaults.motion(token);
}

bool format_entry(
    const Theme& theme,
    const TokenEntry& entry,
    std::span<char> line,
    std::size_t& length
) noexcept {
    length = 0;
    if (!append_value(line, length, entry.name) || !append_value(line, length, " = ")) {
        return false;
    }
    if (has_alias(theme, entry)) {
        return format_alias(theme, entry, line, length);
    }
    if (entry.kind == TokenKind::color) {
        const auto token = static_cast<ColorToken>(entry.index);
        return format_color(line, length, theme.color(token)) && append_value(line, length, "\n");
    }
    if (entry.kind == TokenKind::space) {
        const auto token = static_cast<SpaceToken>(entry.index);
        return format_float(line, length, theme.space(token).value) &&
            append_value(line, length, "\n");
    }
    if (entry.kind == TokenKind::radius) {
        const auto token = static_cast<RadiusToken>(entry.index);
        return format_float(line, length, theme.radius(token).value) &&
            append_value(line, length, "\n");
    }
    const auto token = static_cast<MotionToken>(entry.index);
    return format_motion(line, length, theme.motion(token)) && append_value(line, length, "\n");
}

}

ThemeFileResult
parse_theme_file(std::span<const char> text, Theme& overlay_out, ThemeFileError& error) noexcept {
    overlay_out = Theme{};
    error = ThemeFileError{};

    if (text.empty()) {
        error = ThemeFileError{.code = ThemeFileResult::invalid_schema, .line = 1, .column = 1};
        return ThemeFileResult::invalid_schema;
    }

    const std::string_view input(text.data(), text.size());

    auto overlay = Theme{};
    auto seen = std::array<bool, token_table.size()>{};
    auto schema_seen = false;
    auto line_number = std::uint32_t{};
    auto rest = input;

    while (true) {
        const auto newline = rest.find('\n');
        const std::string_view body =
            newline == std::string_view::npos ? rest : rest.substr(0, newline);
        ++line_number;

        const std::string_view content = trim(body);
        if (!content.empty() && content.front() != '#') {
            if (!schema_seen) {
                schema_seen = true;
                if (!parse_schema(content)) {
                    error = ThemeFileError{
                        .code = ThemeFileResult::invalid_schema,
                        .line = line_number,
                        .column = 1,
                    };
                    return ThemeFileResult::invalid_schema;
                }
            } else {
                const auto separator = content.find('=');
                if (separator == std::string_view::npos) {
                    error = ThemeFileError{
                        .code = ThemeFileResult::bad_value,
                        .line = line_number,
                        .column = 1,
                    };
                    return ThemeFileResult::bad_value;
                }

                const std::string_view name = trim(content.substr(0, separator));
                const std::string_view value = trim(content.substr(separator + 1));

                std::size_t entry_index = token_table.size();
                for (std::size_t index = 0; index < token_table.size(); ++index) {
                    if (token_table[index].name == name) {
                        entry_index = index;
                    }
                }
                if (entry_index == token_table.size()) {
                    error = ThemeFileError{
                        .code = ThemeFileResult::unknown_token,
                        .line = line_number,
                        .column = column_of(body, name),
                    };
                    return ThemeFileResult::unknown_token;
                }
                if (seen[entry_index]) {
                    error = ThemeFileError{
                        .code = ThemeFileResult::duplicate_token,
                        .line = line_number,
                        .column = column_of(body, name),
                    };
                    return ThemeFileResult::duplicate_token;
                }
                const auto& entry = token_table[entry_index];
                auto stored = ThemeUpdateResult::changed;

                if (!value.empty() && value.front() == '@') {
                    const auto reference = trim(value.substr(1));
                    auto source_index = token_table.size();

                    for (std::size_t index = 0; index < token_table.size(); ++index) {
                        if (token_table[index].name == reference) {
                            source_index = index;
                        }
                    }
                    if (source_index == token_table.size() ||
                        token_table[source_index].kind != entry.kind) {
                        error = ThemeFileError{
                            .code = ThemeFileResult::unknown_token,
                            .line = line_number,
                            .column = column_of(body, value),
                        };
                        return ThemeFileResult::unknown_token;
                    }
                    stored = store_alias(overlay, entry, token_table[source_index], line_number);
                } else if (!store_token(overlay, entry, value, line_number)) {
                    error = ThemeFileError{
                        .code = ThemeFileResult::bad_value,
                        .line = line_number,
                        .column = column_of(body, value),
                    };
                    return ThemeFileResult::bad_value;
                }

                if (stored == ThemeUpdateResult::alias_cycle) {
                    error = ThemeFileError{
                        .code = ThemeFileResult::alias_cycle,
                        .line = line_number,
                        .column = column_of(body, value),
                    };
                    return ThemeFileResult::alias_cycle;
                }
                if (stored == ThemeUpdateResult::invalid_token ||
                    stored == ThemeUpdateResult::invalid_value) {
                    error = ThemeFileError{
                        .code = ThemeFileResult::bad_value,
                        .line = line_number,
                        .column = column_of(body, value),
                    };
                    return ThemeFileResult::bad_value;
                }
                seen[entry_index] = true;
            }
        }

        if (newline == std::string_view::npos) {
            break;
        }
        rest.remove_prefix(newline + 1);
        if (rest.empty()) {
            break;
        }
    }

    if (!schema_seen) {
        error = ThemeFileError{
            .code = ThemeFileResult::invalid_schema,
            .line = line_number + 1,
            .column = 1,
        };
        return ThemeFileResult::invalid_schema;
    }

    overlay_out = overlay;
    return ThemeFileResult::ok;
}

ThemeFileResult
write_theme_file(const Theme& theme, std::span<char> buffer_out, std::size_t& used_out) noexcept {
    used_out = 0;

    if (!append_text(buffer_out, used_out, "schema: 1\n")) {
        return ThemeFileResult::truncated_output;
    }

    const auto defaults = Theme{};
    char storage[64]{};
    const std::span<char> line(storage, sizeof(storage));

    for (const auto& entry : token_table) {
        if (!line_differs(theme, defaults, entry)) {
            continue;
        }
        auto length = std::size_t{};
        if (!format_entry(theme, entry, line, length)) {
            return ThemeFileResult::truncated_output;
        }
        if (!append_text(buffer_out, used_out, std::string_view{line.data(), length})) {
            return ThemeFileResult::truncated_output;
        }
    }

    return ThemeFileResult::ok;
}

}
