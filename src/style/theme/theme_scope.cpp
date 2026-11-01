#include "veyra/style/theme/theme_scope.hpp"

#include <bit>

namespace veyra {
namespace {

std::uint64_t mix_version(std::uint64_t hash, std::uint64_t value) noexcept {
    hash ^= value;
    hash *= 1099511628211ULL;

    return hash;
}

}

std::string_view theme_variant_name(ThemeVariant variant) noexcept {
    switch (variant) {
    case ThemeVariant::inherit:
        return "inherit";
    case ThemeVariant::light:
        return "light";
    case ThemeVariant::dark:
        return "dark";
    case ThemeVariant::high_contrast:
        return "high";
    }

    return "?";
}

const Theme& theme_for_variant(ThemeVariant variant) noexcept {
    static const Theme light = Theme::light();
    static const Theme dark = Theme::dark();
    static const Theme high_contrast = Theme::high_contrast();

    switch (variant) {
    case ThemeVariant::dark:
        return dark;
    case ThemeVariant::high_contrast:
        return high_contrast;
    case ThemeVariant::inherit:
    case ThemeVariant::light:
        return light;
    }

    return light;
}

ThemeVariant theme_variant_for_system(ColorScheme scheme, bool high_contrast) noexcept {
    if (high_contrast) {
        return ThemeVariant::high_contrast;
    }

    return scheme == ColorScheme::dark ? ThemeVariant::dark : ThemeVariant::light;
}

ThemeScope::ThemeScope(const Theme& base) noexcept : base_(&base) {
}

ThemeScope::ThemeScope(const Theme& base, ThemeVariant variant) noexcept :
    base_(&base), variant_(variant) {
}

ThemeScope::ThemeScope(const ThemeScope& parent, ThemeVariant variant) noexcept :
    base_(parent.base_), parent_(&parent), variant_(variant) {
}

bool ThemeScope::local_override(TokenKind kind, std::uint8_t token) const noexcept {
    return local_.provenance(kind, token).layer != ThemeLayer::defaults;
}

Srgba8 ThemeScope::resolve_color(ColorToken token) const noexcept {
    if (local_override(TokenKind::color, static_cast<std::uint8_t>(token))) {
        return local_.color(token);
    }

    if (variant_ != ThemeVariant::inherit) {
        return theme_for_variant(variant_).color(token);
    }

    if (parent_ != nullptr) {
        return parent_->color(token);
    }

    return base_->color(token);
}

LogicalPx ThemeScope::resolve_space(SpaceToken token) const noexcept {
    if (local_override(TokenKind::space, static_cast<std::uint8_t>(token))) {
        return local_.space(token);
    }

    if (variant_ != ThemeVariant::inherit) {
        return theme_for_variant(variant_).space(token);
    }

    if (parent_ != nullptr) {
        return parent_->space(token);
    }

    return base_->space(token);
}

LogicalPx ThemeScope::resolve_radius(RadiusToken token) const noexcept {
    if (local_override(TokenKind::radius, static_cast<std::uint8_t>(token))) {
        return local_.radius(token);
    }

    if (variant_ != ThemeVariant::inherit) {
        return theme_for_variant(variant_).radius(token);
    }

    if (parent_ != nullptr) {
        return parent_->radius(token);
    }

    return base_->radius(token);
}

Milliseconds ThemeScope::resolve_motion(MotionToken token) const noexcept {
    if (local_override(TokenKind::motion, static_cast<std::uint8_t>(token))) {
        return local_.motion(token);
    }

    if (variant_ != ThemeVariant::inherit) {
        return theme_for_variant(variant_).motion(token);
    }

    if (parent_ != nullptr) {
        return parent_->resolve_motion(token);
    }

    return base_->motion(token);
}

Srgba8 ThemeScope::color(ColorToken token) const noexcept {
    return resolve_color(token);
}

LogicalPx ThemeScope::space(SpaceToken token) const noexcept {
    return resolve_space(token);
}

LogicalPx ThemeScope::radius(RadiusToken token) const noexcept {
    return resolve_radius(token);
}

Milliseconds ThemeScope::motion(MotionToken token) const noexcept {
    if (reduced_motion()) {
        return Milliseconds{0};
    }

    return resolve_motion(token);
}

ThemeUpdateResult ThemeScope::set(ColorToken token, Srgba8 value) noexcept {
    return local_.set(token, value);
}

ThemeUpdateResult ThemeScope::set(SpaceToken token, LogicalPx value) noexcept {
    return local_.set(token, value);
}

ThemeUpdateResult ThemeScope::set(RadiusToken token, LogicalPx value) noexcept {
    return local_.set(token, value);
}

ThemeUpdateResult ThemeScope::set(MotionToken token, Milliseconds value) noexcept {
    return local_.set(token, value);
}

ThemeUpdateResult ThemeScope::set_alias(ColorToken token, ColorToken source) noexcept {
    return local_.set_alias(token, source);
}

ThemeUpdateResult ThemeScope::set_alias(SpaceToken token, SpaceToken source) noexcept {
    return local_.set_alias(token, source);
}

ThemeUpdateResult ThemeScope::set_alias(RadiusToken token, RadiusToken source) noexcept {
    return local_.set_alias(token, source);
}

ThemeUpdateResult ThemeScope::set_alias(MotionToken token, MotionToken source) noexcept {
    return local_.set_alias(token, source);
}

std::optional<ColorToken> ThemeScope::alias(ColorToken token) const noexcept {
    return local_.alias(token);
}

std::optional<SpaceToken> ThemeScope::alias(SpaceToken token) const noexcept {
    return local_.alias(token);
}

std::optional<RadiusToken> ThemeScope::alias(RadiusToken token) const noexcept {
    return local_.alias(token);
}

std::optional<MotionToken> ThemeScope::alias(MotionToken token) const noexcept {
    return local_.alias(token);
}

ThemeUpdateResult ThemeScope::clear(ColorToken token) noexcept {
    return local_.clear(token);
}

ThemeUpdateResult ThemeScope::clear(SpaceToken token) noexcept {
    return local_.clear(token);
}

ThemeUpdateResult ThemeScope::clear(RadiusToken token) noexcept {
    return local_.clear(token);
}

ThemeUpdateResult ThemeScope::clear(MotionToken token) noexcept {
    return local_.clear(token);
}

ThemeUpdateResult ThemeScope::apply(const Theme& overlay) noexcept {
    return local_.apply(overlay);
}

void ThemeScope::set_variant(ThemeVariant variant) noexcept {
    variant_ = variant;
}

ThemeVariant ThemeScope::variant() const noexcept {
    return variant_;
}

bool ThemeScope::set_text_scale(float scale) noexcept {
    const auto sanitized = sanitize_text_scale(scale);
    if (text_scale_ == sanitized) {
        return false;
    }

    text_scale_ = sanitized;
    return true;
}

bool ThemeScope::clear_text_scale() noexcept {
    if (!text_scale_) {
        return false;
    }

    text_scale_.reset();
    return true;
}

float ThemeScope::text_scale() const noexcept {
    if (text_scale_) {
        return *text_scale_;
    }
    if (parent_ != nullptr) {
        return parent_->text_scale();
    }

    return 1.0F;
}

void ThemeScope::apply_preferences(const SystemPreferences& preferences) noexcept {
    set_variant(theme_variant_for_system(preferences));
    set_text_scale(preferences.text_scale);
    set_reduced_motion(preferences.reduced_motion);
}

bool ThemeScope::set_reduced_motion(bool reduced) noexcept {
    if (reduced_motion_ == reduced) {
        return false;
    }

    reduced_motion_ = reduced;
    return true;
}

bool ThemeScope::clear_reduced_motion() noexcept {
    if (!reduced_motion_) {
        return false;
    }

    reduced_motion_.reset();
    return true;
}

bool ThemeScope::reduced_motion() const noexcept {
    if (reduced_motion_) {
        return *reduced_motion_;
    }
    if (parent_ != nullptr) {
        return parent_->reduced_motion();
    }

    return false;
}

TokenProvenance ThemeScope::provenance(TokenKind kind, std::uint8_t token) const noexcept {
    if (local_override(kind, token)) {
        return local_.provenance(kind, token);
    }

    if (variant_ != ThemeVariant::inherit) {
        return theme_for_variant(variant_).provenance(kind, token);
    }

    if (parent_ != nullptr) {
        return parent_->provenance(kind, token);
    }

    return base_->provenance(kind, token);
}

std::uint64_t ThemeScope::version() const noexcept {
    auto hash = std::uint64_t{1469598103934665603ULL};

    hash = mix_version(hash, base_->version());
    hash = mix_version(hash, static_cast<std::uint64_t>(variant_) + 1);
    hash = mix_version(hash, local_.version());

    if (text_scale_) {
        hash = mix_version(
            hash,
            static_cast<std::uint64_t>(std::bit_cast<std::uint32_t>(*text_scale_)) + 1
        );
    }
    if (reduced_motion_) {
        hash = mix_version(hash, *reduced_motion_ ? 3 : 2);
    }

    if (parent_ != nullptr) {
        hash = mix_version(hash, parent_->version());
    }

    return hash;
}

const Theme& ThemeScope::base() const noexcept {
    return *base_;
}

const ThemeScope* ThemeScope::parent() const noexcept {
    return parent_;
}

}
