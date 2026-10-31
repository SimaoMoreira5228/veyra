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

template <std::size_t Size>
std::size_t
follow_alias(std::size_t index, const std::array<std::uint8_t, Size>& aliases) noexcept {
    for (auto step = std::size_t{}; step < Size; ++step) {
        const auto next = aliases[index];
        if (next == 0) {
            break;
        }
        index = static_cast<std::size_t>(next - 1);
    }
    return index;
}

template <std::size_t Size>
bool alias_would_cycle(
    const std::array<std::uint8_t, Size>& aliases,
    std::size_t token,
    std::size_t source
) noexcept {
    if (token == source) {
        return true;
    }
    for (auto step = std::size_t{}; step < Size; ++step) {
        const auto next = aliases[source];
        if (next == 0) {
            return false;
        }
        source = static_cast<std::size_t>(next - 1);
        if (source == token) {
            return true;
        }
    }
    return true;
}

template <typename Token, typename Value, typename Provenance, std::size_t Size>
ThemeUpdateResult set_override(
    Token token,
    Value value,
    std::array<std::optional<Value>, Size>& overrides,
    std::array<Provenance, Size>& provenance,
    std::array<std::uint8_t, Size>& aliases,
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
    auto changed = false;
    if (aliases[index] != 0) {
        aliases[index] = 0;
        changed = true;
    }
    if (overrides[index] != value) {
        overrides[index] = value;
        changed = true;
    }
    if (!changed) {
        return ThemeUpdateResult::unchanged;
    }
    ++version;
    if (version == 0) {
        version = 1;
    }
    return ThemeUpdateResult::changed;
}

template <typename Token, typename Value, typename Provenance, std::size_t Size>
ThemeUpdateResult set_alias_impl(
    Token token,
    Token source,
    std::array<std::optional<Value>, Size>& overrides,
    std::array<Provenance, Size>& provenance,
    std::array<std::uint8_t, Size>& aliases,
    ThemeLayer layer,
    std::uint32_t line,
    std::uint64_t& version
) noexcept {
    const auto index = static_cast<std::size_t>(token);
    const auto source_index = static_cast<std::size_t>(source);
    if (index >= Size || source_index >= Size) {
        return ThemeUpdateResult::invalid_token;
    }
    if (alias_would_cycle(aliases, index, source_index)) {
        return ThemeUpdateResult::alias_cycle;
    }

    provenance[index].layer = layer;
    provenance[index].line = line;
    auto changed = false;
    const auto encoded = static_cast<std::uint8_t>(source_index + 1);
    if (aliases[index] != encoded) {
        aliases[index] = encoded;
        changed = true;
    }
    if (overrides[index]) {
        overrides[index].reset();
        changed = true;
    }
    if (!changed) {
        return ThemeUpdateResult::unchanged;
    }
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
    std::array<std::uint8_t, Size>& aliases,
    std::uint64_t& version
) noexcept {
    const auto index = static_cast<std::size_t>(token);
    if (index >= Size) {
        return ThemeUpdateResult::invalid_token;
    }
    provenance[index] = {};
    auto changed = false;
    if (aliases[index] != 0) {
        aliases[index] = 0;
        changed = true;
    }
    if (overrides[index]) {
        overrides[index].reset();
        changed = true;
    }
    if (!changed) {
        return ThemeUpdateResult::unchanged;
    }
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
    const std::array<std::optional<Value>, Size>& overrides,
    const std::array<std::uint8_t, Size>& aliases
) noexcept {
    const auto index = static_cast<std::size_t>(token);
    if (index >= Size) {
        return defaults.front();
    }
    const auto resolved = follow_alias(index, aliases);
    return overrides[resolved].value_or(defaults[resolved]);
}

template <typename Token, typename SourceToken, std::size_t Size>
std::optional<SourceToken>
alias_of(Token token, const std::array<std::uint8_t, Size>& aliases) noexcept {
    const auto index = static_cast<std::size_t>(token);
    if (index >= Size || aliases[index] == 0) {
        return std::nullopt;
    }
    return static_cast<SourceToken>(aliases[index] - 1);
}

bool valid_dimension(LogicalPx value) noexcept {
    return std::isfinite(value.value) && value.value >= 0.0F;
}

template <typename Provenance, std::size_t Size>
TokenProvenance resolve_provenance(
    TokenKind kind,
    std::uint8_t token,
    const std::array<Provenance, Size>& provenance,
    const std::array<std::uint8_t, Size>& aliases
) noexcept {
    const auto index = static_cast<std::size_t>(token);
    if (index >= Size) {
        return {.kind = kind, .token = token};
    }
    if (aliases[index] == 0) {
        return {
            .kind = kind,
            .token = token,
            .layer = provenance[index].layer,
            .line = provenance[index].line,
        };
    }

    const auto resolved = follow_alias(index, aliases);
    return {
        .kind = kind,
        .token = static_cast<std::uint8_t>(resolved),
        .layer = provenance[index].layer,
        .line = provenance[index].line,
    };
}

}

TokenProvenance Theme::provenance(TokenKind kind, std::uint8_t token) const noexcept {
    switch (kind) {
    case TokenKind::color:
        return resolve_provenance(kind, token, color_provenance_, color_aliases_);
    case TokenKind::space:
        return resolve_provenance(kind, token, space_provenance_, space_aliases_);
    case TokenKind::radius:
        return resolve_provenance(kind, token, radius_provenance_, radius_aliases_);
    case TokenKind::motion:
        return resolve_provenance(kind, token, motion_provenance_, motion_aliases_);
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
    return resolve(token, default_colors, colors_, color_aliases_);
}

LogicalPx Theme::space(SpaceToken token) const noexcept {
    return resolve(token, default_spaces, spaces_, space_aliases_);
}

LogicalPx Theme::radius(RadiusToken token) const noexcept {
    return resolve(token, default_radii, radii_, radius_aliases_);
}

Milliseconds Theme::motion(MotionToken token) const noexcept {
    return resolve(token, default_motions, motions_, motion_aliases_);
}

ThemeUpdateResult
Theme::set(ColorToken token, Srgba8 value, ThemeLayer layer, std::uint32_t line) noexcept {
    return set_override(
        token,
        value,
        colors_,
        color_provenance_,
        color_aliases_,
        layer,
        line,
        version_
    );
}

ThemeUpdateResult
Theme::set(SpaceToken token, LogicalPx value, ThemeLayer layer, std::uint32_t line) noexcept {
    if (static_cast<std::size_t>(token) >= spaces_.size()) {
        return ThemeUpdateResult::invalid_token;
    }
    if (!valid_dimension(value)) {
        return ThemeUpdateResult::invalid_value;
    }
    return set_override(
        token,
        value,
        spaces_,
        space_provenance_,
        space_aliases_,
        layer,
        line,
        version_
    );
}

ThemeUpdateResult
Theme::set(RadiusToken token, LogicalPx value, ThemeLayer layer, std::uint32_t line) noexcept {
    if (static_cast<std::size_t>(token) >= radii_.size()) {
        return ThemeUpdateResult::invalid_token;
    }
    if (!valid_dimension(value)) {
        return ThemeUpdateResult::invalid_value;
    }
    return set_override(
        token,
        value,
        radii_,
        radius_provenance_,
        radius_aliases_,
        layer,
        line,
        version_
    );
}

ThemeUpdateResult
Theme::set(MotionToken token, Milliseconds value, ThemeLayer layer, std::uint32_t line) noexcept {
    return set_override(
        token,
        value,
        motions_,
        motion_provenance_,
        motion_aliases_,
        layer,
        line,
        version_
    );
}

ThemeUpdateResult Theme::set_alias(
    ColorToken token,
    ColorToken source,
    ThemeLayer layer,
    std::uint32_t line
) noexcept {
    return set_alias_impl(
        token,
        source,
        colors_,
        color_provenance_,
        color_aliases_,
        layer,
        line,
        version_
    );
}

ThemeUpdateResult Theme::set_alias(
    SpaceToken token,
    SpaceToken source,
    ThemeLayer layer,
    std::uint32_t line
) noexcept {
    return set_alias_impl(
        token,
        source,
        spaces_,
        space_provenance_,
        space_aliases_,
        layer,
        line,
        version_
    );
}

ThemeUpdateResult Theme::set_alias(
    RadiusToken token,
    RadiusToken source,
    ThemeLayer layer,
    std::uint32_t line
) noexcept {
    return set_alias_impl(
        token,
        source,
        radii_,
        radius_provenance_,
        radius_aliases_,
        layer,
        line,
        version_
    );
}

ThemeUpdateResult Theme::set_alias(
    MotionToken token,
    MotionToken source,
    ThemeLayer layer,
    std::uint32_t line
) noexcept {
    return set_alias_impl(
        token,
        source,
        motions_,
        motion_provenance_,
        motion_aliases_,
        layer,
        line,
        version_
    );
}

std::optional<ColorToken> Theme::alias(ColorToken token) const noexcept {
    return alias_of<ColorToken, ColorToken>(token, color_aliases_);
}

std::optional<SpaceToken> Theme::alias(SpaceToken token) const noexcept {
    return alias_of<SpaceToken, SpaceToken>(token, space_aliases_);
}

std::optional<RadiusToken> Theme::alias(RadiusToken token) const noexcept {
    return alias_of<RadiusToken, RadiusToken>(token, radius_aliases_);
}

std::optional<MotionToken> Theme::alias(MotionToken token) const noexcept {
    return alias_of<MotionToken, MotionToken>(token, motion_aliases_);
}

ThemeUpdateResult Theme::clear(ColorToken token) noexcept {
    return clear_override(token, colors_, color_provenance_, color_aliases_, version_);
}

ThemeUpdateResult Theme::clear(SpaceToken token) noexcept {
    return clear_override(token, spaces_, space_provenance_, space_aliases_, version_);
}

ThemeUpdateResult Theme::clear(RadiusToken token) noexcept {
    return clear_override(token, radii_, radius_provenance_, radius_aliases_, version_);
}

ThemeUpdateResult Theme::clear(MotionToken token) noexcept {
    return clear_override(token, motions_, motion_provenance_, motion_aliases_, version_);
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
        if (overlay.color_aliases_[index] != 0) {
            if (color_aliases_[index] != overlay.color_aliases_[index]) {
                color_aliases_[index] = overlay.color_aliases_[index];
                changed = true;
            }
            color_provenance_[index] = overlay.color_provenance_[index];
            if (colors_[index]) {
                colors_[index].reset();
                changed = true;
            }

            continue;
        }
        if (!overlay.colors_[index]) {
            continue;
        }
        color_provenance_[index] = overlay.color_provenance_[index];
        if (color_aliases_[index] != 0) {
            color_aliases_[index] = 0;
            changed = true;
        }
        if (colors_[index] != overlay.colors_[index]) {
            colors_[index] = overlay.colors_[index];
            changed = true;
        }
    }

    for (auto index = std::size_t{}; index < spaces_.size(); ++index) {
        if (overlay.space_aliases_[index] != 0) {
            if (space_aliases_[index] != overlay.space_aliases_[index]) {
                space_aliases_[index] = overlay.space_aliases_[index];
                changed = true;
            }
            space_provenance_[index] = overlay.space_provenance_[index];
            if (spaces_[index]) {
                spaces_[index].reset();
                changed = true;
            }

            continue;
        }
        if (!overlay.spaces_[index]) {
            continue;
        }
        space_provenance_[index] = overlay.space_provenance_[index];
        if (space_aliases_[index] != 0) {
            space_aliases_[index] = 0;
            changed = true;
        }
        if (spaces_[index] != overlay.spaces_[index]) {
            spaces_[index] = overlay.spaces_[index];
            changed = true;
        }
    }

    for (auto index = std::size_t{}; index < radii_.size(); ++index) {
        if (overlay.radius_aliases_[index] != 0) {
            if (radius_aliases_[index] != overlay.radius_aliases_[index]) {
                radius_aliases_[index] = overlay.radius_aliases_[index];
                changed = true;
            }
            radius_provenance_[index] = overlay.radius_provenance_[index];
            if (radii_[index]) {
                radii_[index].reset();
                changed = true;
            }

            continue;
        }
        if (!overlay.radii_[index]) {
            continue;
        }
        radius_provenance_[index] = overlay.radius_provenance_[index];
        if (radius_aliases_[index] != 0) {
            radius_aliases_[index] = 0;
            changed = true;
        }
        if (radii_[index] != overlay.radii_[index]) {
            radii_[index] = overlay.radii_[index];
            changed = true;
        }
    }

    for (auto index = std::size_t{}; index < motions_.size(); ++index) {
        if (overlay.motion_aliases_[index] != 0) {
            if (motion_aliases_[index] != overlay.motion_aliases_[index]) {
                motion_aliases_[index] = overlay.motion_aliases_[index];
                changed = true;
            }
            motion_provenance_[index] = overlay.motion_provenance_[index];
            if (motions_[index]) {
                motions_[index].reset();
                changed = true;
            }

            continue;
        }
        if (!overlay.motions_[index]) {
            continue;
        }
        motion_provenance_[index] = overlay.motion_provenance_[index];
        if (motion_aliases_[index] != 0) {
            motion_aliases_[index] = 0;
            changed = true;
        }
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
