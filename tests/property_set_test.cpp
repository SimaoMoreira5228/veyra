#include "veyra/style/property/property_set.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

constexpr std::array<veyra::StyleLayer, 8> kLayers{
    veyra::StyleLayer::platform_default,
    veyra::StyleLayer::theme,
    veyra::StyleLayer::recipe,
    veyra::StyleLayer::variant,
    veyra::StyleLayer::state,
    veyra::StyleLayer::local,
    veyra::StyleLayer::controller,
    veyra::StyleLayer::accessibility,
};

veyra::Srgba8 shade(std::uint8_t value) {
    return {.red = value, .green = value, .blue = value, .alpha = 255};
}

void check_every_layer_outranks_the_previous() {
    auto properties = veyra::PropertySet{};
    const auto initial_version = properties.version();

    for (std::size_t index = 0; index < kLayers.size(); ++index) {
        require(
            properties.set(
                veyra::StyleProperty::foreground,
                kLayers[index],
                veyra::make_color_value(shade(static_cast<std::uint8_t>(index + 1)))
            ) == veyra::PropertyUpdateResult::changed,
            "each layer accepts a value"
        );
        const auto resolved = properties.resolve(veyra::StyleProperty::foreground);
        require(resolved.set, "the property resolves after a set");
        require(resolved.provenance.layer == kLayers[index], "the highest set layer wins");
        require(
            resolved.value.color == shade(static_cast<std::uint8_t>(index + 1)),
            "the resolved value comes from that layer"
        );
    }
    require(properties.version() > initial_version, "sets advance the version");

    for (std::size_t index = kLayers.size(); index > 1; --index) {
        require(
            properties.clear(veyra::StyleProperty::foreground, kLayers[index - 1]) ==
                veyra::PropertyUpdateResult::changed,
            "the top layer clears"
        );
        const auto resolved = properties.resolve(veyra::StyleProperty::foreground);
        require(
            resolved.provenance.layer == kLayers[index - 2],
            "clearing reveals the next lower layer"
        );
    }
}

void check_unset_and_unchanged_results() {
    auto properties = veyra::PropertySet{};
    const auto resolved = properties.resolve(veyra::StyleProperty::padding);
    require(!resolved.set, "an unset property does not resolve");
    require(
        resolved.provenance.layer == veyra::StyleLayer::platform_default,
        "an unset property reports the base layer"
    );

    const auto value = veyra::make_length_value(8.0F);
    require(
        properties.set(veyra::StyleProperty::padding, veyra::StyleLayer::recipe, value) ==
            veyra::PropertyUpdateResult::changed,
        "the first set changes"
    );
    const auto version = properties.version();
    require(
        properties.set(veyra::StyleProperty::padding, veyra::StyleLayer::recipe, value) ==
            veyra::PropertyUpdateResult::unchanged,
        "an identical set is unchanged"
    );
    require(properties.version() == version, "an unchanged set keeps the version");
    require(
        properties.clear(veyra::StyleProperty::padding, veyra::StyleLayer::recipe) ==
            veyra::PropertyUpdateResult::changed,
        "a set slot clears"
    );
    require(
        properties.clear(veyra::StyleProperty::padding, veyra::StyleLayer::recipe) ==
            veyra::PropertyUpdateResult::unchanged,
        "clearing an empty slot is unchanged"
    );
    require(properties.version() == version + 1, "clear advances the version once");
}

void check_invalid_inputs_are_rejected() {
    auto properties = veyra::PropertySet{};
    const auto version = properties.version();

    require(
        properties.set(
            static_cast<veyra::StyleProperty>(99),
            veyra::StyleLayer::theme,
            veyra::make_color_value(shade(1))
        ) == veyra::PropertyUpdateResult::invalid_property,
        "an unknown property is rejected"
    );
    require(
        properties.set(
            veyra::StyleProperty::foreground,
            static_cast<veyra::StyleLayer>(99),
            veyra::make_color_value(shade(1))
        ) == veyra::PropertyUpdateResult::invalid_layer,
        "an unknown layer is rejected"
    );
    require(
        properties.set(
            veyra::StyleProperty::foreground,
            veyra::StyleLayer::theme,
            veyra::make_length_value(4.0F)
        ) == veyra::PropertyUpdateResult::invalid_value,
        "a mismatched value kind is rejected"
    );
    require(
        properties.set(
            veyra::StyleProperty::padding,
            veyra::StyleLayer::theme,
            veyra::make_length_value(-1.0F)
        ) == veyra::PropertyUpdateResult::invalid_value,
        "a negative length is rejected"
    );
    require(
        properties.set(
            veyra::StyleProperty::opacity,
            veyra::StyleLayer::theme,
            veyra::make_number_value(1.5F)
        ) == veyra::PropertyUpdateResult::invalid_value,
        "opacity outside zero to one is rejected"
    );
    require(properties.version() == version, "rejected sets do not advance the version");
}

void check_theme_application_records_token_provenance() {
    auto properties = veyra::PropertySet{};
    properties.apply_theme(veyra::Theme::dark());

    const auto background = properties.resolve(veyra::StyleProperty::background);
    require(background.set, "the theme supplies a background");
    require(background.provenance.layer == veyra::StyleLayer::theme, "the theme layer wins");
    require(background.provenance.from_token, "the theme value records its token");
    require(
        background.provenance.token == static_cast<std::uint8_t>(veyra::tokens::color_surface),
        "the background records the surface token"
    );

    const auto padding = properties.resolve(veyra::StyleProperty::padding);
    require(
        padding.provenance.token == static_cast<std::uint8_t>(veyra::tokens::space_small),
        "spacing records the space token"
    );
    const auto duration = properties.resolve(veyra::StyleProperty::transition_duration);
    require(
        duration.value.kind == veyra::PropertyValueKind::duration &&
            duration.value.duration_ms ==
                veyra::Theme::dark().motion(veyra::tokens::motion_normal).value,
        "motion records its duration"
    );

    const auto local = shade(9);
    properties.set(
        veyra::StyleProperty::background,
        veyra::StyleLayer::local,
        veyra::make_color_value(local)
    );
    const auto overridden = properties.resolve(veyra::StyleProperty::background);
    require(overridden.value.color == local, "a local value outranks the theme");
    require(
        overridden.provenance.layer == veyra::StyleLayer::local,
        "local provenance is reported"
    );
    require(!overridden.provenance.from_token, "a literal value has no token source");

    properties.clear(veyra::StyleProperty::background, veyra::StyleLayer::local);
    require(
        properties.resolve(veyra::StyleProperty::background).provenance.layer ==
            veyra::StyleLayer::theme,
        "clearing local reveals the theme"
    );
}

void check_layer_and_property_names() {
    require(
        veyra::style_layer_name(veyra::StyleLayer::accessibility) == "access",
        "layer names map"
    );
    require(
        veyra::style_property_name(veyra::StyleProperty::corner_radius) == "radius",
        "property names map"
    );
    require(
        veyra::style_property_kind(veyra::StyleProperty::transition_duration) ==
            veyra::PropertyValueKind::duration,
        "duration kind maps"
    );
    require(
        veyra::style_property_kind(veyra::StyleProperty::opacity) ==
            veyra::PropertyValueKind::number,
        "number kind maps"
    );
}

}

int main() {
    try {
        check_every_layer_outranks_the_previous();
        check_unset_and_unchanged_results();
        check_invalid_inputs_are_rejected();
        check_theme_application_records_token_provenance();
        check_layer_and_property_names();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';

        return 1;
    }

    std::cout << "property set checks passed\n";

    return 0;
}
