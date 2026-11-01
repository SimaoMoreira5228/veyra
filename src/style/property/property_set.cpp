#include "veyra/style/property/property_set.hpp"

#include <cmath>

namespace veyra {
namespace {

PropertySource token_source(TokenKind kind, std::uint8_t token) noexcept {
    return {
        .from_token = true,
        .token_kind = kind,
        .token = token,
    };
}

bool valid_value(StyleProperty property, const PropertyValue& value) noexcept {
    if (value.kind != style_property_kind(property)) {
        return false;
    }

    switch (value.kind) {
    case PropertyValueKind::color:
        return true;
    case PropertyValueKind::length:
        return std::isfinite(value.length) && value.length >= 0.0F;
    case PropertyValueKind::duration:
        return true;
    case PropertyValueKind::number:
        return std::isfinite(value.number) && value.number >= 0.0F && value.number <= 1.0F;
    }

    return false;
}

bool same_source(const PropertySource& left, const PropertySource& right) noexcept {
    return left.from_token == right.from_token && left.token_kind == right.token_kind &&
        left.token == right.token && left.line == right.line;
}

}

std::string_view style_layer_name(StyleLayer layer) noexcept {
    switch (layer) {
    case StyleLayer::platform_default:
        return "platform";
    case StyleLayer::theme:
        return "theme";
    case StyleLayer::recipe:
        return "recipe";
    case StyleLayer::variant:
        return "variant";
    case StyleLayer::state:
        return "state";
    case StyleLayer::local:
        return "local";
    case StyleLayer::controller:
        return "controller";
    case StyleLayer::accessibility:
        return "access";
    case StyleLayer::count:
        break;
    }

    return "?";
}

std::string_view style_property_name(StyleProperty property) noexcept {
    switch (property) {
    case StyleProperty::foreground:
        return "foreground";
    case StyleProperty::background:
        return "background";
    case StyleProperty::border_color:
        return "border";
    case StyleProperty::corner_radius:
        return "radius";
    case StyleProperty::border_width:
        return "border_width";
    case StyleProperty::padding:
        return "padding";
    case StyleProperty::font_size:
        return "font_size";
    case StyleProperty::opacity:
        return "opacity";
    case StyleProperty::transition_duration:
        return "duration";
    case StyleProperty::count:
        break;
    }

    return "?";
}

PropertyValueKind style_property_kind(StyleProperty property) noexcept {
    switch (property) {
    case StyleProperty::foreground:
    case StyleProperty::background:
    case StyleProperty::border_color:
        return PropertyValueKind::color;
    case StyleProperty::corner_radius:
    case StyleProperty::border_width:
    case StyleProperty::padding:
    case StyleProperty::font_size:
        return PropertyValueKind::length;
    case StyleProperty::opacity:
        return PropertyValueKind::number;
    case StyleProperty::transition_duration:
        return PropertyValueKind::duration;
    case StyleProperty::count:
        break;
    }

    return PropertyValueKind::color;
}

PropertyUpdateResult PropertySet::set(
    StyleProperty property,
    StyleLayer layer,
    PropertyValue value,
    PropertySource source
) noexcept {
    if (!valid_property(property)) {
        return PropertyUpdateResult::invalid_property;
    }
    if (!valid_layer(layer)) {
        return PropertyUpdateResult::invalid_layer;
    }
    if (!valid_value(property, value)) {
        return PropertyUpdateResult::invalid_value;
    }

    auto& slot = slots_[static_cast<std::size_t>(property)][static_cast<std::size_t>(layer)];
    if (slot.set && slot.value == value && same_source(slot.source, source)) {
        return PropertyUpdateResult::unchanged;
    }

    slot.set = true;
    slot.value = value;
    slot.source = source;
    ++version_;

    if (version_ == 0) {
        version_ = 1;
    }

    return PropertyUpdateResult::changed;
}

PropertyUpdateResult PropertySet::clear(StyleProperty property, StyleLayer layer) noexcept {
    if (!valid_property(property)) {
        return PropertyUpdateResult::invalid_property;
    }
    if (!valid_layer(layer)) {
        return PropertyUpdateResult::invalid_layer;
    }

    auto& slot = slots_[static_cast<std::size_t>(property)][static_cast<std::size_t>(layer)];
    if (!slot.set) {
        return PropertyUpdateResult::unchanged;
    }

    slot = {};
    ++version_;

    if (version_ == 0) {
        version_ = 1;
    }

    return PropertyUpdateResult::changed;
}

bool PropertySet::has(StyleProperty property, StyleLayer layer) const noexcept {
    if (!valid_property(property) || !valid_layer(layer)) {
        return false;
    }

    return slots_[static_cast<std::size_t>(property)][static_cast<std::size_t>(layer)].set;
}

ResolvedProperty PropertySet::resolve(StyleProperty property) const noexcept {
    auto resolved = ResolvedProperty{.property = property};

    if (!valid_property(property)) {
        return resolved;
    }

    const auto& layers = slots_[static_cast<std::size_t>(property)];
    for (auto index = kLayerCount; index > 0; --index) {
        const auto& slot = layers[index - 1];
        if (!slot.set) {
            continue;
        }

        const auto layer = static_cast<StyleLayer>(index - 1);
        resolved.set = true;
        resolved.value = slot.value;
        resolved.provenance = {
            .layer = layer,
            .from_token = slot.source.from_token,
            .token_kind = slot.source.token_kind,
            .token = slot.source.token,
            .line = slot.source.line,
        };

        return resolved;
    }

    return resolved;
}

void PropertySet::apply_theme(const Theme& theme) noexcept {
    set(StyleProperty::foreground,
        StyleLayer::theme,
        make_color_value(theme.color(tokens::color_text_primary)),
        token_source(TokenKind::color, static_cast<std::uint8_t>(tokens::color_text_primary)));
    set(StyleProperty::background,
        StyleLayer::theme,
        make_color_value(theme.color(tokens::color_surface)),
        token_source(TokenKind::color, static_cast<std::uint8_t>(tokens::color_surface)));
    set(StyleProperty::border_color,
        StyleLayer::theme,
        make_color_value(theme.color(tokens::color_text_disabled)),
        token_source(TokenKind::color, static_cast<std::uint8_t>(tokens::color_text_disabled)));
    set(StyleProperty::corner_radius,
        StyleLayer::theme,
        make_length_value(theme.radius(tokens::radius_medium).value),
        token_source(TokenKind::radius, static_cast<std::uint8_t>(tokens::radius_medium)));
    set(StyleProperty::padding,
        StyleLayer::theme,
        make_length_value(theme.space(tokens::space_small).value),
        token_source(TokenKind::space, static_cast<std::uint8_t>(tokens::space_small)));
    set(StyleProperty::transition_duration,
        StyleLayer::theme,
        make_duration_value(theme.motion(tokens::motion_normal).value),
        token_source(TokenKind::motion, static_cast<std::uint8_t>(tokens::motion_normal)));
}

std::uint64_t PropertySet::version() const noexcept {
    return version_;
}

bool PropertySet::valid_property(StyleProperty property) noexcept {
    return static_cast<std::size_t>(property) < kPropertyCount;
}

bool PropertySet::valid_layer(StyleLayer layer) noexcept {
    return static_cast<std::size_t>(layer) < kLayerCount;
}

}
