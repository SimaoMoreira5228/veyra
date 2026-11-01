#ifndef VEYRA_STYLE_PROPERTY_SET_HPP
#define VEYRA_STYLE_PROPERTY_SET_HPP

#include "veyra/foundation/color.hpp"
#include "veyra/style/theme/theme.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace veyra {

enum class StyleLayer : std::uint8_t {
    platform_default,
    theme,
    recipe,
    variant,
    state,
    local,
    controller,
    accessibility,
    count,
};

enum class StyleProperty : std::uint8_t {
    foreground,
    background,
    border_color,
    corner_radius,
    border_width,
    padding,
    font_size,
    opacity,
    transition_duration,
    count,
};

enum class PropertyValueKind : std::uint8_t {
    color,
    length,
    duration,
    number,
};

struct PropertyValue {
    PropertyValueKind kind{PropertyValueKind::color};
    Srgba8 color{};
    float length{};
    std::uint32_t duration_ms{};
    float number{};

    constexpr bool operator==(const PropertyValue&) const noexcept = default;
};

struct PropertySource {
    bool from_token{};
    TokenKind token_kind{TokenKind::color};
    std::uint8_t token{};
    std::uint32_t line{};
};

struct PropertyProvenance {
    StyleLayer layer{StyleLayer::platform_default};
    bool from_token{};
    TokenKind token_kind{TokenKind::color};
    std::uint8_t token{};
    std::uint32_t line{};
};

struct ResolvedProperty {
    StyleProperty property{StyleProperty::foreground};
    bool set{};
    PropertyValue value{};
    PropertyProvenance provenance{};
};

enum class PropertyUpdateResult : std::uint8_t {
    unchanged,
    changed,
    invalid_property,
    invalid_layer,
    invalid_value,
};

constexpr PropertyValue make_color_value(Srgba8 value) noexcept {
    return {.kind = PropertyValueKind::color, .color = value};
}

constexpr PropertyValue make_length_value(float value) noexcept {
    return {.kind = PropertyValueKind::length, .length = value};
}

constexpr PropertyValue make_duration_value(std::uint32_t value) noexcept {
    return {.kind = PropertyValueKind::duration, .duration_ms = value};
}

constexpr PropertyValue make_number_value(float value) noexcept {
    return {.kind = PropertyValueKind::number, .number = value};
}

std::string_view style_layer_name(StyleLayer layer) noexcept;
std::string_view style_property_name(StyleProperty property) noexcept;
PropertyValueKind style_property_kind(StyleProperty property) noexcept;

class PropertySet {
  public:
    static constexpr std::size_t kLayerCount = static_cast<std::size_t>(StyleLayer::count);
    static constexpr std::size_t kPropertyCount = static_cast<std::size_t>(StyleProperty::count);

    PropertyUpdateResult
    set(StyleProperty property,
        StyleLayer layer,
        PropertyValue value,
        PropertySource source = {}) noexcept;
    PropertyUpdateResult clear(StyleProperty property, StyleLayer layer) noexcept;
    bool has(StyleProperty property, StyleLayer layer) const noexcept;
    ResolvedProperty resolve(StyleProperty property) const noexcept;

    void apply_theme(const Theme& theme) noexcept;

    std::uint64_t version() const noexcept;

  private:
    struct Slot {
        bool set{};
        PropertyValue value{};
        PropertySource source{};
    };

    static bool valid_property(StyleProperty property) noexcept;
    static bool valid_layer(StyleLayer layer) noexcept;

    std::array<std::array<Slot, kLayerCount>, kPropertyCount> slots_{};
    std::uint64_t version_{1};
};

}

namespace veyra::style {
using ::veyra::make_color_value;
using ::veyra::make_duration_value;
using ::veyra::make_length_value;
using ::veyra::make_number_value;
using ::veyra::PropertyProvenance;
using ::veyra::PropertySet;
using ::veyra::PropertySource;
using ::veyra::PropertyUpdateResult;
using ::veyra::PropertyValue;
using ::veyra::PropertyValueKind;
using ::veyra::ResolvedProperty;
using ::veyra::style_layer_name;
using ::veyra::style_property_kind;
using ::veyra::style_property_name;
using ::veyra::StyleLayer;
using ::veyra::StyleProperty;
}

namespace veyra::style::property {
using ::veyra::make_color_value;
using ::veyra::make_duration_value;
using ::veyra::make_length_value;
using ::veyra::make_number_value;
using ::veyra::PropertyProvenance;
using ::veyra::PropertySet;
using ::veyra::PropertySource;
using ::veyra::PropertyUpdateResult;
using ::veyra::PropertyValue;
using ::veyra::PropertyValueKind;
using ::veyra::ResolvedProperty;
using ::veyra::style_layer_name;
using ::veyra::style_property_kind;
using ::veyra::style_property_name;
using ::veyra::StyleLayer;
using ::veyra::StyleProperty;
}

#endif
