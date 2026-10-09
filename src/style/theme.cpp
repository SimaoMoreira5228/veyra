#include "veyra/style/theme.hpp"

#include <cmath>

namespace veyra {

namespace {

constexpr std::array default_colors{
    Srgba8{.red = 255, .green = 255, .blue = 255},
    Srgba8{.red = 23, .green = 25, .blue = 31},
    Srgba8{.red = 57, .green = 120, .blue = 216},
    Srgba8{.red = 255, .green = 255, .blue = 255},
    Srgba8{.red = 48, .green = 105, .blue = 190},
    Srgba8{.red = 40, .green = 88, .blue = 164},
    Srgba8{.red = 208, .green = 211, .blue = 218},
    Srgba8{.red = 99, .green = 104, .blue = 116},
    Srgba8{.red = 57, .green = 120, .blue = 216},
    Srgba8{.red = 255, .green = 255, .blue = 255},
    Srgba8{.red = 176, .green = 0, .blue = 32},
};

constexpr std::array default_spaces{
    LogicalPx{0.0F},
    LogicalPx{2.0F},
    LogicalPx{4.0F},
    LogicalPx{8.0F},
    LogicalPx{16.0F},
    LogicalPx{24.0F},
};

constexpr std::array default_radii{
    LogicalPx{0.0F},
    LogicalPx{2.0F},
    LogicalPx{4.0F},
    LogicalPx{8.0F},
};

constexpr std::array default_motions{
    Milliseconds{0},
    Milliseconds{100},
    Milliseconds{180},
    Milliseconds{300},
};

template <typename Token, typename Value, std::size_t Size>
ThemeUpdateResult set_override(
    Token token,
    Value value,
    std::array<std::optional<Value>, Size>& overrides,
    std::uint64_t& version
) noexcept {
    const auto index = static_cast<std::size_t>(token);
    if (index >= Size) {
        return ThemeUpdateResult::invalid_token;
    }
    if (overrides[index] == value) {
        return ThemeUpdateResult::unchanged;
    }
    overrides[index] = value;
    ++version;
    if (version == 0) {
        version = 1;
    }
    return ThemeUpdateResult::changed;
}

template <typename Token, typename Value, std::size_t Size>
ThemeUpdateResult clear_override(
    Token token,
    std::array<std::optional<Value>, Size>& overrides,
    std::uint64_t& version
) noexcept {
    const auto index = static_cast<std::size_t>(token);
    if (index >= Size) {
        return ThemeUpdateResult::invalid_token;
    }
    if (!overrides[index]) {
        return ThemeUpdateResult::unchanged;
    }
    overrides[index].reset();
    ++version;
    if (version == 0) {
        version = 1;
    }
    return ThemeUpdateResult::changed;
}

template <typename Token, typename Value, std::size_t Size>
Value resolve(
    Token token,
    const std::array<Value, Size>& defaults,
    const std::array<std::optional<Value>, Size>& overrides
) noexcept {
    const auto index = static_cast<std::size_t>(token);
    if (index >= Size) {
        return defaults.front();
    }
    return overrides[index].value_or(defaults[index]);
}

bool valid_dimension(LogicalPx value) noexcept {
    return std::isfinite(value.value) && value.value >= 0.0F;
}

}

Srgba8 Theme::color(ColorToken token) const noexcept {
    return resolve(token, default_colors, colors_);
}

LogicalPx Theme::space(SpaceToken token) const noexcept {
    return resolve(token, default_spaces, spaces_);
}

LogicalPx Theme::radius(RadiusToken token) const noexcept {
    return resolve(token, default_radii, radii_);
}

Milliseconds Theme::motion(MotionToken token) const noexcept {
    return resolve(token, default_motions, motions_);
}

ThemeUpdateResult Theme::set(ColorToken token, Srgba8 value) noexcept {
    return set_override(token, value, colors_, version_);
}

ThemeUpdateResult Theme::set(SpaceToken token, LogicalPx value) noexcept {
    if (static_cast<std::size_t>(token) >= spaces_.size()) {
        return ThemeUpdateResult::invalid_token;
    }
    if (!valid_dimension(value)) {
        return ThemeUpdateResult::invalid_value;
    }
    return set_override(token, value, spaces_, version_);
}

ThemeUpdateResult Theme::set(RadiusToken token, LogicalPx value) noexcept {
    if (static_cast<std::size_t>(token) >= radii_.size()) {
        return ThemeUpdateResult::invalid_token;
    }
    if (!valid_dimension(value)) {
        return ThemeUpdateResult::invalid_value;
    }
    return set_override(token, value, radii_, version_);
}

ThemeUpdateResult Theme::set(MotionToken token, Milliseconds value) noexcept {
    return set_override(token, value, motions_, version_);
}

ThemeUpdateResult Theme::clear(ColorToken token) noexcept {
    return clear_override(token, colors_, version_);
}

ThemeUpdateResult Theme::clear(SpaceToken token) noexcept {
    return clear_override(token, spaces_, version_);
}

ThemeUpdateResult Theme::clear(RadiusToken token) noexcept {
    return clear_override(token, radii_, version_);
}

ThemeUpdateResult Theme::clear(MotionToken token) noexcept {
    return clear_override(token, motions_, version_);
}

std::uint64_t Theme::version() const noexcept {
    return version_;
}

}
