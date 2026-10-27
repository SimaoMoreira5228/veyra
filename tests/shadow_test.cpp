#include "veyra/render/draw/shadow.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <variant>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

const veyra::FillRoundedRect& rounded_at(const veyra::DisplayCommand& command) {
    const auto* rounded = std::get_if<veyra::FillRoundedRect>(&command);

    if (rounded == nullptr) {
        throw std::runtime_error("expected a rounded rect shadow layer");
    }

    return *rounded;
}

void check_sharp_shadow_is_one_rect() {
    const veyra::BoxShadow shadow{
        .bounds = {.x = 10.0F, .y = 20.0F, .width = 38.0F, .height = 30.0F},
        .radius = 6.0F,
        .blur = 0.0F,
        .offset_x = 2.0F,
        .offset_y = 3.0F,
        .color = {.red = 0, .green = 0, .blue = 0, .alpha = 90},
    };
    auto output = std::array<veyra::DisplayCommand, 8>{};
    const auto lowered = veyra::lower_box_shadow(shadow, output);

    require(
        lowered.result == veyra::ShadowResult::complete && lowered.commands == 1,
        "zero blur lowers to a single offset rect"
    );

    const auto& layer = rounded_at(output[0]);
    require(
        layer.bounds.x == 12.0F && layer.bounds.y == 23.0F && layer.bounds.width == 38.0F &&
            layer.bounds.height == 30.0F && layer.radius == 6.0F,
        "zero blur keeps exact bounds and radius, shifted by the offset"
    );
    require(layer.color.alpha == 90, "zero blur keeps the full shadow alpha");
}

void check_blurred_shadow_falls_off() {
    const veyra::BoxShadow shadow{
        .bounds = {.x = 10.0F, .y = 20.0F, .width = 38.0F, .height = 30.0F},
        .radius = 6.0F,
        .blur = 3.0F,
        .offset_x = 2.0F,
        .offset_y = 3.0F,
        .color = {.red = 0, .green = 0, .blue = 0, .alpha = 90},
    };
    auto output = std::array<veyra::DisplayCommand, 8>{};
    const auto lowered = veyra::lower_box_shadow(shadow, output);

    require(
        lowered.result == veyra::ShadowResult::complete && lowered.commands == 3,
        "a blur of three lowers to three layers"
    );

    const auto& back = rounded_at(output[0]);
    const auto& front = rounded_at(output[2]);
    require(
        back.bounds.width == 44.0F && back.bounds.height == 36.0F && back.radius == 9.0F,
        "the back layer spreads the full blur"
    );
    require(
        front.bounds.width == 40.0F && front.bounds.height == 32.0F && front.radius == 7.0F,
        "the front layer spreads one third of the blur"
    );
    require(back.color.alpha < front.color.alpha, "alpha grows toward the front layer");
    require(front.color.alpha == 90, "the front layer carries the peak alpha");
    require(
        back.color.red == 0 && back.color.green == 0 && back.color.blue == 0,
        "layers preserve the shadow color channels"
    );
}

void check_huge_blur_clamps_layers() {
    const veyra::BoxShadow shadow{
        .bounds = {.width = 38.0F, .height = 30.0F},
        .radius = 6.0F,
        .blur = 100.0F,
        .color = {.alpha = 255},
    };
    auto output = std::array<veyra::DisplayCommand, 8>{};
    const auto lowered = veyra::lower_box_shadow(shadow, output);

    require(
        lowered.result == veyra::ShadowResult::complete && lowered.commands == 8,
        "huge blurs clamp to eight layers"
    );
}

void check_rejects_bad_geometry() {
    auto output = std::array<veyra::DisplayCommand, 8>{};
    const veyra::BoxShadow negative_width{
        .bounds = {.width = -1.0F, .height = 30.0F},
        .color = {.alpha = 255},
    };
    const auto width_outcome = veyra::lower_box_shadow(negative_width, output);
    require(
        width_outcome.result == veyra::ShadowResult::invalid_input && width_outcome.commands == 0,
        "negative bounds are rejected without writing"
    );

    const veyra::BoxShadow negative_radius{
        .bounds = {.width = 38.0F, .height = 30.0F},
        .radius = -1.0F,
        .color = {.alpha = 255},
    };
    require(
        veyra::lower_box_shadow(negative_radius, output).result ==
            veyra::ShadowResult::invalid_input,
        "negative radius is rejected"
    );

    const veyra::BoxShadow negative_blur{
        .bounds = {.width = 38.0F, .height = 30.0F},
        .blur = -2.0F,
        .color = {.alpha = 255},
    };
    require(
        veyra::lower_box_shadow(negative_blur, output).result == veyra::ShadowResult::invalid_input,
        "negative blur is rejected"
    );

    const veyra::BoxShadow negative_height{
        .bounds = {.width = 38.0F, .height = -1.0F},
        .color = {.alpha = 255},
    };
    require(
        veyra::lower_box_shadow(negative_height, output).result ==
            veyra::ShadowResult::invalid_input,
        "negative height is rejected"
    );

    const veyra::BoxShadow nan_x{
        .bounds = {.x = std::numeric_limits<float>::quiet_NaN(), .width = 38.0F, .height = 30.0F},
        .color = {.alpha = 255},
    };
    require(
        veyra::lower_box_shadow(nan_x, output).result == veyra::ShadowResult::invalid_input,
        "a NaN bounds x is rejected"
    );

    const veyra::BoxShadow nan_radius{
        .bounds = {.width = 38.0F, .height = 30.0F},
        .radius = std::numeric_limits<float>::quiet_NaN(),
        .color = {.alpha = 255},
    };
    require(
        veyra::lower_box_shadow(nan_radius, output).result == veyra::ShadowResult::invalid_input,
        "a NaN radius is rejected"
    );

    const veyra::BoxShadow nan_blur{
        .bounds = {.width = 38.0F, .height = 30.0F},
        .blur = std::numeric_limits<float>::quiet_NaN(),
        .color = {.alpha = 255},
    };
    require(
        veyra::lower_box_shadow(nan_blur, output).result == veyra::ShadowResult::invalid_input,
        "a NaN blur is rejected"
    );

    const veyra::BoxShadow inf_blur{
        .bounds = {.width = 38.0F, .height = 30.0F},
        .blur = std::numeric_limits<float>::infinity(),
        .color = {.alpha = 255},
    };
    require(
        veyra::lower_box_shadow(inf_blur, output).result == veyra::ShadowResult::invalid_input,
        "an infinite blur is rejected"
    );

    const veyra::BoxShadow bad_offset{
        .bounds = {.width = 38.0F, .height = 30.0F},
        .offset_x = std::numeric_limits<float>::quiet_NaN(),
        .color = {.alpha = 255},
    };
    require(
        veyra::lower_box_shadow(bad_offset, output).result == veyra::ShadowResult::invalid_input,
        "a NaN offset is rejected"
    );
}

void check_short_buffer_writes_nothing() {
    const veyra::BoxShadow shadow{
        .bounds = {.width = 38.0F, .height = 30.0F},
        .radius = 6.0F,
        .blur = 3.0F,
        .color = {.alpha = 255},
    };
    auto output = std::array<veyra::DisplayCommand, 2>{};
    const auto lowered = veyra::lower_box_shadow(shadow, output);

    require(
        lowered.result == veyra::ShadowResult::insufficient_output && lowered.commands == 0,
        "a short buffer fails without partial layers"
    );

    auto none = std::array<veyra::DisplayCommand, 0>{};
    const auto empty_outcome = veyra::lower_box_shadow(shadow, none);
    require(
        empty_outcome.result == veyra::ShadowResult::insufficient_output &&
            empty_outcome.commands == 0,
        "a zero-slot buffer fails without partial layers"
    );
}

void check_layers_render_end_to_end() {
    const veyra::BoxShadow shadow{
        .bounds = {.x = 8.0F, .y = 8.0F, .width = 16.0F, .height = 16.0F},
        .radius = 4.0F,
        .blur = 3.0F,
        .offset_x = 2.0F,
        .offset_y = 2.0F,
        .color = {.red = 0, .green = 0, .blue = 0, .alpha = 200},
    };
    auto commands = std::array<veyra::DisplayCommand, 8>{};
    const auto lowered = veyra::lower_box_shadow(shadow, commands);
    require(lowered.result == veyra::ShadowResult::complete, "the shadow lowers cleanly");

    constexpr auto width = std::size_t{40};
    constexpr auto height = std::size_t{40};
    std::array<veyra::SoftwarePixel, width * height> pixels{};
    std::array<veyra::SoftwareClipRect, 1> clips{};
    std::array<veyra::LayoutPoint, 1> translations{};

    for (auto& pixel : pixels) {
        pixel = veyra::SoftwarePixel{.red = 255, .green = 255, .blue = 255, .alpha = 255};
    }

    const auto result = veyra::render_software(
        std::span<const veyra::DisplayCommand>{commands.data(), lowered.commands},
        {
            .width = width,
            .height = height,
            .stride = width,
            .pixels = pixels,
        },
        {.clips = clips, .translations = translations}
    );
    require(result == veyra::SoftwareRenderResult::complete, "shadow layers render");

    bool touched = false;

    for (const auto& pixel : pixels) {
        if (pixel.red < 255 || pixel.green < 255 || pixel.blue < 255) {
            touched = true;

            break;
        }
    }

    require(touched, "the shadow darkens pixels under its layers");

    const auto& front = rounded_at(commands[lowered.commands - 1]);
    require(
        front.bounds.x >= 0.0F && front.bounds.y >= 0.0F &&
            front.bounds.x + front.bounds.width <= static_cast<float>(width) &&
            front.bounds.y + front.bounds.height <= static_cast<float>(height),
        "every layer fits inside the render surface"
    );
}

}

int main() {
    try {
        check_sharp_shadow_is_one_rect();
        check_blurred_shadow_falls_off();
        check_huge_blur_clamps_layers();
        check_rejects_bad_geometry();
        check_short_buffer_writes_nothing();
        check_layers_render_end_to_end();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
