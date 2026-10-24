#ifndef VEYRA_STYLE_THEME_FILE_HPP
#define VEYRA_STYLE_THEME_FILE_HPP

#include "veyra/style/theme.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace veyra {

enum class ThemeFileResult : std::uint8_t {
    ok,
    invalid_schema,
    unknown_token,
    bad_value,
    duplicate_token,
    truncated_output,
};

struct ThemeFileError {
    ThemeFileResult code{ThemeFileResult::ok};
    std::uint32_t line{};
    std::uint32_t column{};
};

ThemeFileResult
parse_theme_file(std::span<const char> text, Theme& overlay_out, ThemeFileError& error) noexcept;

ThemeFileResult
write_theme_file(const Theme& theme, std::span<char> buffer_out, std::size_t& used_out) noexcept;

}

#endif
