#ifndef VEYRA_STYLE_THEME_SCOPE_HPP
#define VEYRA_STYLE_THEME_SCOPE_HPP

#include "veyra/foundation/preferences.hpp"
#include "veyra/style/theme/theme.hpp"

#include <cstdint>
#include <optional>
#include <string_view>

namespace veyra {

enum class ThemeVariant : std::uint8_t {
    inherit,
    light,
    dark,
    high_contrast,
};

std::string_view theme_variant_name(ThemeVariant variant) noexcept;

const Theme& theme_for_variant(ThemeVariant variant) noexcept;

ThemeVariant theme_variant_for_system(ColorScheme scheme, bool high_contrast) noexcept;

inline ThemeVariant theme_variant_for_system(const SystemPreferences& preferences) noexcept {
    return theme_variant_for_system(preferences.color_scheme, preferences.high_contrast);
}

class ThemeScope {
  public:
    explicit ThemeScope(const Theme& base) noexcept;
    ThemeScope(const Theme& base, ThemeVariant variant) noexcept;
    ThemeScope(const ThemeScope& parent, ThemeVariant variant) noexcept;

    ThemeScope(const ThemeScope&) = delete;
    ThemeScope& operator=(const ThemeScope&) = delete;
    ThemeScope(ThemeScope&&) = delete;
    ThemeScope& operator=(ThemeScope&&) = delete;

    Srgba8 color(ColorToken token) const noexcept;
    LogicalPx space(SpaceToken token) const noexcept;
    LogicalPx radius(RadiusToken token) const noexcept;
    Milliseconds motion(MotionToken token) const noexcept;

    ThemeUpdateResult set(ColorToken token, Srgba8 value) noexcept;
    ThemeUpdateResult set(SpaceToken token, LogicalPx value) noexcept;
    ThemeUpdateResult set(RadiusToken token, LogicalPx value) noexcept;
    ThemeUpdateResult set(MotionToken token, Milliseconds value) noexcept;
    ThemeUpdateResult set_alias(ColorToken token, ColorToken source) noexcept;
    ThemeUpdateResult set_alias(SpaceToken token, SpaceToken source) noexcept;
    ThemeUpdateResult set_alias(RadiusToken token, RadiusToken source) noexcept;
    ThemeUpdateResult set_alias(MotionToken token, MotionToken source) noexcept;
    std::optional<ColorToken> alias(ColorToken token) const noexcept;
    std::optional<SpaceToken> alias(SpaceToken token) const noexcept;
    std::optional<RadiusToken> alias(RadiusToken token) const noexcept;
    std::optional<MotionToken> alias(MotionToken token) const noexcept;
    ThemeUpdateResult clear(ColorToken token) noexcept;
    ThemeUpdateResult clear(SpaceToken token) noexcept;
    ThemeUpdateResult clear(RadiusToken token) noexcept;
    ThemeUpdateResult clear(MotionToken token) noexcept;
    ThemeUpdateResult apply(const Theme& overlay) noexcept;

    void set_variant(ThemeVariant variant) noexcept;
    ThemeVariant variant() const noexcept;

    bool set_text_scale(float scale) noexcept;
    bool clear_text_scale() noexcept;
    float text_scale() const noexcept;

    void apply_preferences(const SystemPreferences& preferences) noexcept;

    bool set_reduced_motion(bool reduced) noexcept;
    bool clear_reduced_motion() noexcept;
    bool reduced_motion() const noexcept;

    TokenProvenance provenance(TokenKind kind, std::uint8_t token) const noexcept;
    std::uint64_t version() const noexcept;

    const Theme& base() const noexcept;
    const ThemeScope* parent() const noexcept;

  private:
    bool local_override(TokenKind kind, std::uint8_t token) const noexcept;
    Srgba8 resolve_color(ColorToken token) const noexcept;
    LogicalPx resolve_space(SpaceToken token) const noexcept;
    LogicalPx resolve_radius(RadiusToken token) const noexcept;
    Milliseconds resolve_motion(MotionToken token) const noexcept;

    const Theme* base_;
    const ThemeScope* parent_{};
    Theme local_{};
    ThemeVariant variant_{ThemeVariant::inherit};
    std::optional<float> text_scale_{};
    std::optional<bool> reduced_motion_{};
};

}

namespace veyra::style {
using ::veyra::theme_for_variant;
using ::veyra::theme_variant_name;
using ::veyra::ThemeScope;
using ::veyra::ThemeVariant;
}

namespace veyra::style::theme {
using ::veyra::theme_for_variant;
using ::veyra::theme_variant_name;
using ::veyra::ThemeScope;
using ::veyra::ThemeVariant;
}

#endif
