#ifndef VEYRA_STYLE_THEME_HPP
#define VEYRA_STYLE_THEME_HPP

#include "veyra/foundation/color.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace veyra {

struct LogicalPx {
    float value{};

    constexpr bool operator==(const LogicalPx&) const noexcept = default;
};

struct Milliseconds {
    std::uint32_t value{};

    constexpr bool operator==(const Milliseconds&) const noexcept = default;
};

enum class ColorToken : std::uint8_t {
    surface,
    text_primary,
    action_primary,
    action_on_primary,
    action_hovered,
    action_pressed,
    action_disabled,
    text_disabled,
    toggle_checked,
    toggle_on_checked,
    error,
    count,
};

enum class SpaceToken : std::uint8_t {
    none,
    extra_small,
    small,
    medium,
    large,
    extra_large,
    count,
};

enum class RadiusToken : std::uint8_t {
    none,
    small,
    medium,
    large,
    count,
};

enum class MotionToken : std::uint8_t {
    instant,
    fast,
    normal,
    slow,
    count,
};

namespace tokens {

inline constexpr auto color_surface = ColorToken::surface;
inline constexpr auto color_text_primary = ColorToken::text_primary;
inline constexpr auto color_action_primary = ColorToken::action_primary;
inline constexpr auto color_action_on_primary = ColorToken::action_on_primary;
inline constexpr auto color_action_hovered = ColorToken::action_hovered;
inline constexpr auto color_action_pressed = ColorToken::action_pressed;
inline constexpr auto color_action_disabled = ColorToken::action_disabled;
inline constexpr auto color_text_disabled = ColorToken::text_disabled;
inline constexpr auto color_toggle_checked = ColorToken::toggle_checked;
inline constexpr auto color_toggle_on_checked = ColorToken::toggle_on_checked;
inline constexpr auto color_error = ColorToken::error;

inline constexpr auto space_none = SpaceToken::none;
inline constexpr auto space_extra_small = SpaceToken::extra_small;
inline constexpr auto space_small = SpaceToken::small;
inline constexpr auto space_medium = SpaceToken::medium;
inline constexpr auto space_large = SpaceToken::large;
inline constexpr auto space_extra_large = SpaceToken::extra_large;

inline constexpr auto radius_none = RadiusToken::none;
inline constexpr auto radius_small = RadiusToken::small;
inline constexpr auto radius_medium = RadiusToken::medium;
inline constexpr auto radius_large = RadiusToken::large;

inline constexpr auto motion_instant = MotionToken::instant;
inline constexpr auto motion_fast = MotionToken::fast;
inline constexpr auto motion_normal = MotionToken::normal;
inline constexpr auto motion_slow = MotionToken::slow;

}

enum class ThemeUpdateResult : std::uint8_t {
    unchanged,
    changed,
    invalid_token,
    invalid_value,
};

enum class ThemeLayer : std::uint8_t {
    defaults,
    preset,
    file,
    override,
};

enum class TokenKind : std::uint8_t {
    color,
    space,
    radius,
    motion,
};

struct TokenProvenance {
    TokenKind kind{TokenKind::color};
    std::uint8_t token{};
    ThemeLayer layer{ThemeLayer::defaults};
    std::uint32_t line{};
};

constexpr std::string_view theme_layer_name(ThemeLayer layer) noexcept {
    switch (layer) {
    case ThemeLayer::defaults:
        return "defaults";
    case ThemeLayer::preset:
        return "preset";
    case ThemeLayer::file:
        return "file";
    case ThemeLayer::override:
        return "override";
    }
    return "?";
}

std::string_view token_name(TokenKind kind, std::uint8_t token) noexcept;

class Theme {
  public:
    static Theme light() noexcept;
    static Theme dark() noexcept;
    static Theme high_contrast() noexcept;

    TokenProvenance provenance(TokenKind kind, std::uint8_t token) const noexcept;

    Srgba8 color(ColorToken token) const noexcept;
    LogicalPx space(SpaceToken token) const noexcept;
    LogicalPx radius(RadiusToken token) const noexcept;
    Milliseconds motion(MotionToken token) const noexcept;

    ThemeUpdateResult
    set(ColorToken token,
        Srgba8 value,
        ThemeLayer layer = ThemeLayer::override,
        std::uint32_t line = 0) noexcept;
    ThemeUpdateResult
    set(SpaceToken token,
        LogicalPx value,
        ThemeLayer layer = ThemeLayer::override,
        std::uint32_t line = 0) noexcept;
    ThemeUpdateResult
    set(RadiusToken token,
        LogicalPx value,
        ThemeLayer layer = ThemeLayer::override,
        std::uint32_t line = 0) noexcept;
    ThemeUpdateResult
    set(MotionToken token,
        Milliseconds value,
        ThemeLayer layer = ThemeLayer::override,
        std::uint32_t line = 0) noexcept;

    ThemeUpdateResult clear(ColorToken token) noexcept;
    ThemeUpdateResult clear(SpaceToken token) noexcept;
    ThemeUpdateResult clear(RadiusToken token) noexcept;
    ThemeUpdateResult clear(MotionToken token) noexcept;
    ThemeUpdateResult apply(const Theme& overlay) noexcept;

    std::uint64_t version() const noexcept;

  private:
    struct ProvenanceEntry {
        ThemeLayer layer{ThemeLayer::defaults};
        std::uint32_t line{};
    };

    std::array<std::optional<Srgba8>, static_cast<std::size_t>(ColorToken::count)> colors_{};
    std::array<ProvenanceEntry, static_cast<std::size_t>(ColorToken::count)> color_provenance_{};
    std::array<std::optional<LogicalPx>, static_cast<std::size_t>(SpaceToken::count)> spaces_{};
    std::array<ProvenanceEntry, static_cast<std::size_t>(SpaceToken::count)> space_provenance_{};
    std::array<std::optional<LogicalPx>, static_cast<std::size_t>(RadiusToken::count)> radii_{};
    std::array<ProvenanceEntry, static_cast<std::size_t>(RadiusToken::count)> radius_provenance_{};
    std::array<std::optional<Milliseconds>, static_cast<std::size_t>(MotionToken::count)>
        motions_{};
    std::array<ProvenanceEntry, static_cast<std::size_t>(MotionToken::count)> motion_provenance_{};
    std::uint64_t version_{1};
};

}

namespace veyra::style {
using ::veyra::ColorToken;
using ::veyra::LogicalPx;
using ::veyra::Milliseconds;
using ::veyra::MotionToken;
using ::veyra::RadiusToken;
using ::veyra::SpaceToken;
using ::veyra::Theme;
using ::veyra::ThemeLayer;
using ::veyra::ThemeUpdateResult;
using ::veyra::TokenKind;
using ::veyra::TokenProvenance;
}

namespace veyra::style::theme {
using ::veyra::ColorToken;
using ::veyra::LogicalPx;
using ::veyra::Milliseconds;
using ::veyra::MotionToken;
using ::veyra::RadiusToken;
using ::veyra::SpaceToken;
using ::veyra::Theme;
using ::veyra::ThemeLayer;
using ::veyra::ThemeUpdateResult;
using ::veyra::TokenKind;
using ::veyra::TokenProvenance;
}

#endif
