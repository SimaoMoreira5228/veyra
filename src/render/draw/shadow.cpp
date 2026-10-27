#include "veyra/render/draw/shadow.hpp"

#include <cmath>
#include <cstddef>

namespace veyra {
namespace {

constexpr std::size_t kMaxShadowLayers = 8;

bool valid_shadow(const BoxShadow& shadow) noexcept {
    if (!std::isfinite(shadow.bounds.x) || !std::isfinite(shadow.bounds.y) ||
        !std::isfinite(shadow.bounds.width) || !std::isfinite(shadow.bounds.height)) {
        return false;
    }

    if (shadow.bounds.width < 0.0F || shadow.bounds.height < 0.0F) {
        return false;
    }

    if (!std::isfinite(shadow.radius) || shadow.radius < 0.0F) {
        return false;
    }

    if (!std::isfinite(shadow.blur) || shadow.blur < 0.0F) {
        return false;
    }

    return std::isfinite(shadow.offset_x) && std::isfinite(shadow.offset_y);
}

std::size_t layer_total(float blur) noexcept {
    if (blur <= 0.0F) {
        return 1;
    }

    const auto layers = static_cast<std::size_t>(std::ceil(blur));

    if (layers > kMaxShadowLayers) {
        return kMaxShadowLayers;
    }

    return layers;
}

std::uint8_t layer_alpha(std::uint8_t peak, std::size_t layer, std::size_t layers) noexcept {
    const auto weight = 0.25F + 0.75F * static_cast<float>(layer + 1) / static_cast<float>(layers);
    const auto scaled = static_cast<float>(peak) * weight;

    if (scaled >= 255.0F) {
        return 255;
    }

    return static_cast<std::uint8_t>(scaled);
}

}

ShadowLowered lower_box_shadow(const BoxShadow& shadow, std::span<DisplayCommand> output) noexcept {
    if (!valid_shadow(shadow)) {
        return {.result = ShadowResult::invalid_input};
    }

    const auto layers = layer_total(shadow.blur);

    if (output.size() < layers) {
        return {.result = ShadowResult::insufficient_output};
    }

    for (auto layer = std::size_t{}; layer < layers; ++layer) {
        const auto spread =
            shadow.blur * static_cast<float>(layers - layer) / static_cast<float>(layers);

        output[layer] = FillRoundedRect{
            .bounds =
                {
                    .x = shadow.bounds.x + shadow.offset_x - spread,
                    .y = shadow.bounds.y + shadow.offset_y - spread,
                    .width = shadow.bounds.width + spread * 2.0F,
                    .height = shadow.bounds.height + spread * 2.0F,
                },
            .radius = shadow.radius + spread,
            .color = {
                .red = shadow.color.red,
                .green = shadow.color.green,
                .blue = shadow.color.blue,
                .alpha = layer_alpha(shadow.color.alpha, layer, layers),
            },
        };
    }

    return {.commands = layers};
}

}
