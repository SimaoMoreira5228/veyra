#include "veyra/style/theme/theme.hpp"

#include <cmath>
#include <span>

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

template <typename Token, typename Value, typename Provenance, std::size_t Size>
ThemeUpdateResult set_override(
    Token token,
    Value value,
    std::array<std::optional<Value>, Size>& overrides,
    std::array<Provenance, Size>& provenance,
    ThemeLayer layer,
    std::uint32_t line,
    std::uint64_t& version
) noexcept {
    const auto index = static_cast<std::size_t>(token);
    if (index >= Size) {
        return ThemeUpdateResult::invalid_token;
    }
    provenance[index].layer = layer;
    provenance[index].line = line;
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

template <typename Token, typename Value, typename Provenance, std::size_t Size>
ThemeUpdateResult clear_override(
    Token token,
    std::array<std::optional<Value>, Size>& overrides,
    std::array<Provenance, Size>& provenance,
    std::uint64_t& version
) noexcept {
    const auto index = static_cast<std::size_t>(token);
    if (index >= Size) {
        return ThemeUpdateResult::invalid_token;
    }
    provenance[index] = {};
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

struct ColorOverride {
    ColorToken token;
    Srgba8 value;
};

Theme with_preset(std::span<const ColorOverride> presets) noexcept {
    auto theme = Theme{};

    for (const auto& preset : presets) {
        theme.set(preset.token, preset.value, ThemeLayer::preset);
    }

    return theme;
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

TokenProvenance Theme::provenance(TokenKind kind, std::uint8_t token) const noexcept {
    const auto index = static_cast<std::size_t>(token);
    switch (kind) {
    case TokenKind::color:
        if (index < color_provenance_.size()) {
            return {
                .kind = kind,
                .token = token,
                .layer = color_provenance_[index].layer,
                .line = color_provenance_[index].line,
            };
        }
        break;
    case TokenKind::space:
        if (index < space_provenance_.size()) {
            return {
                .kind = kind,
                .token = token,
                .layer = space_provenance_[index].layer,
                .line = space_provenance_[index].line,
            };
        }
        break;
    case TokenKind::radius:
        if (index < radius_provenance_.size()) {
            return {
                .kind = kind,
                .token = token,
                .layer = radius_provenance_[index].layer,
                .line = radius_provenance_[index].line,
            };
        }
        break;
    case TokenKind::motion:
        if (index < motion_provenance_.size()) {
            return {
                .kind = kind,
                .token = token,
                .layer = motion_provenance_[index].layer,
                .line = motion_provenance_[index].line,
            };
        }
        break;
    }

    return {.kind = kind, .token = token};
}

std::string_view token_name(TokenKind kind, std::uint8_t token) noexcept {
    constexpr std::array<std::string_view, 11> colors{
        "color_surface",
        "color_text_primary",
        "color_action_primary",
        "color_action_on_primary",
        "color_action_hovered",
        "color_action_pressed",
        "color_action_disabled",
        "color_text_disabled",
        "color_toggle_checked",
        "color_toggle_on_checked",
        "color_error",
    };
    constexpr std::array<std::string_view, 6> spaces{
        "space_none",
        "space_extra_small",
        "space_small",
        "space_medium",
        "space_large",
        "space_extra_large",
    };
    constexpr std::array<std::string_view, 4> radii{
        "radius_none",
        "radius_small",
        "radius_medium",
        "radius_large",
    };
    constexpr std::array<std::string_view, 4> motions{
        "motion_instant",
        "motion_fast",
        "motion_normal",
        "motion_slow",
    };

    const auto index = static_cast<std::size_t>(token);
    switch (kind) {
    case TokenKind::color:
        if (index < colors.size()) {
            return colors[index];
        }
        break;
    case TokenKind::space:
        if (index < spaces.size()) {
            return spaces[index];
        }
        break;
    case TokenKind::radius:
        if (index < radii.size()) {
            return radii[index];
        }
        break;
    case TokenKind::motion:
        if (index < motions.size()) {
            return motions[index];
        }
        break;
    }
    return "unknown";
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

ThemeUpdateResult
Theme::set(ColorToken token, Srgba8 value, ThemeLayer layer, std::uint32_t line) noexcept {
    return set_override(token, value, colors_, color_provenance_, layer, line, version_);
}

ThemeUpdateResult
Theme::set(SpaceToken token, LogicalPx value, ThemeLayer layer, std::uint32_t line) noexcept {
    if (static_cast<std::size_t>(token) >= spaces_.size()) {
        return ThemeUpdateResult::invalid_token;
    }
    if (!valid_dimension(value)) {
        return ThemeUpdateResult::invalid_value;
    }
    return set_override(token, value, spaces_, space_provenance_, layer, line, version_);
}

ThemeUpdateResult
Theme::set(RadiusToken token, LogicalPx value, ThemeLayer layer, std::uint32_t line) noexcept {
    if (static_cast<std::size_t>(token) >= radii_.size()) {
        return ThemeUpdateResult::invalid_token;
    }
    if (!valid_dimension(value)) {
        return ThemeUpdateResult::invalid_value;
    }
    return set_override(token, value, radii_, radius_provenance_, layer, line, version_);
}

ThemeUpdateResult
Theme::set(MotionToken token, Milliseconds value, ThemeLayer layer, std::uint32_t line) noexcept {
    return set_override(token, value, motions_, motion_provenance_, layer, line, version_);
}

ThemeUpdateResult Theme::clear(ColorToken token) noexcept {
    return clear_override(token, colors_, color_provenance_, version_);
}

ThemeUpdateResult Theme::clear(SpaceToken token) noexcept {
    return clear_override(token, spaces_, space_provenance_, version_);
}

ThemeUpdateResult Theme::clear(RadiusToken token) noexcept {
    return clear_override(token, radii_, radius_provenance_, version_);
}

ThemeUpdateResult Theme::clear(MotionToken token) noexcept {
    return clear_override(token, motions_, motion_provenance_, version_);
}

std::uint64_t Theme::version() const noexcept {
    return version_;
}

Theme Theme::light() noexcept {
    return Theme{};
}

Theme Theme::dark() noexcept {
    constexpr ColorOverride presets[] = {
        {ColorToken::surface, {.red = 28, .green = 30, .blue = 38}},
        {ColorToken::text_primary, {.red = 232, .green = 234, .blue = 240}},
        {ColorToken::action_primary, {.red = 96, .green = 150, .blue = 228}},
        {ColorToken::action_on_primary, {.red = 12, .green = 14, .blue = 18}},
        {ColorToken::action_hovered, {.red = 120, .green = 168, .blue = 238}},
        {ColorToken::action_pressed, {.red = 74, .green = 120, .blue = 196}},
        {ColorToken::action_disabled, {.red = 70, .green = 74, .blue = 84}},
        {ColorToken::text_disabled, {.red = 132, .green = 138, .blue = 150}},
        {ColorToken::toggle_checked, {.red = 96, .green = 150, .blue = 228}},
        {ColorToken::toggle_on_checked, {.red = 12, .green = 14, .blue = 18}},
        {ColorToken::error, {.red = 255, .green = 110, .blue = 118}},
    };

    return with_preset(presets);
}

Theme Theme::high_contrast() noexcept {
    constexpr ColorOverride presets[] = {
        {ColorToken::surface, {.red = 0, .green = 0, .blue = 0}},
        {ColorToken::text_primary, {.red = 255, .green = 255, .blue = 255}},
        {ColorToken::action_primary, {.red = 255, .green = 205, .blue = 0}},
        {ColorToken::action_on_primary, {.red = 0, .green = 0, .blue = 0}},
        {ColorToken::action_hovered, {.red = 255, .green = 255, .blue = 255}},
        {ColorToken::action_pressed, {.red = 180, .green = 140, .blue = 0}},
        {ColorToken::action_disabled, {.red = 128, .green = 128, .blue = 128}},
        {ColorToken::text_disabled, {.red = 170, .green = 170, .blue = 170}},
        {ColorToken::toggle_checked, {.red = 0, .green = 180, .blue = 255}},
        {ColorToken::toggle_on_checked, {.red = 0, .green = 0, .blue = 0}},
        {ColorToken::error, {.red = 255, .green = 90, .blue = 90}},
    };

    return with_preset(presets);
}

ThemeUpdateResult Theme::apply(const Theme& overlay) noexcept {
    auto changed = false;

    for (auto index = std::size_t{}; index < colors_.size(); ++index) {
        if (!overlay.colors_[index]) {
            continue;
        }
        color_provenance_[index] = overlay.color_provenance_[index];
        if (colors_[index] != overlay.colors_[index]) {
            colors_[index] = overlay.colors_[index];
            changed = true;
        }
    }

    for (auto index = std::size_t{}; index < spaces_.size(); ++index) {
        if (!overlay.spaces_[index]) {
            continue;
        }
        space_provenance_[index] = overlay.space_provenance_[index];
        if (spaces_[index] != overlay.spaces_[index]) {
            spaces_[index] = overlay.spaces_[index];
            changed = true;
        }
    }

    for (auto index = std::size_t{}; index < radii_.size(); ++index) {
        if (!overlay.radii_[index]) {
            continue;
        }
        radius_provenance_[index] = overlay.radius_provenance_[index];
        if (radii_[index] != overlay.radii_[index]) {
            radii_[index] = overlay.radii_[index];
            changed = true;
        }
    }

    for (auto index = std::size_t{}; index < motions_.size(); ++index) {
        if (!overlay.motions_[index]) {
            continue;
        }
        motion_provenance_[index] = overlay.motion_provenance_[index];
        if (motions_[index] != overlay.motions_[index]) {
            motions_[index] = overlay.motions_[index];
            changed = true;
        }
    }

    if (!changed) {
        return ThemeUpdateResult::unchanged;
    }

    ++version_;

    if (version_ == 0) {
        version_ = 1;
    }

    return ThemeUpdateResult::changed;
}
}
