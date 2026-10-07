#include "veyra/render/software_renderer.hpp"

#include <array>
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

void check_linear_color_interpolation() {
    const std::array<veyra::SoftwarePixel, 2> image_pixels{
        veyra::SoftwarePixel{.alpha = 255},
        veyra::SoftwarePixel{.red = 255, .green = 255, .blue = 255, .alpha = 255},
    };
    const auto image = veyra::SoftwareImage{
        .width = image_pixels.size(),
        .height = 1,
        .stride = image_pixels.size(),
        .pixels = image_pixels,
    };
    const auto command = veyra::DisplayCommand{veyra::DrawImage{
        .source = {.width = 2.0F, .height = 1.0F},
        .destination = {.width = 4.0F, .height = 1.0F},
        .image = &image,
        .sampling = veyra::SoftwareImageSampling::linear,
    }};
    std::array<veyra::SoftwarePixel, 4> pixels{};
    pixels.fill({.alpha = 0});
    const auto result = veyra::render_software(
        std::span<const veyra::DisplayCommand>(&command, 1),
        {.width = pixels.size(), .height = 1, .stride = pixels.size(), .pixels = pixels},
        {}
    );

    require(result == veyra::SoftwareRenderResult::complete, "linear image sampling renders");
    require(pixels[0].red == 0 && pixels[0].alpha == 255, "first sample clamps to the first texel");
    require(pixels[1].red == 64, "linear sampling interpolates the first interval");
    require(pixels[2].red == 191, "linear sampling interpolates the second interval");
    require(pixels[3].red == 255, "last sample clamps to the final texel");
}

void check_linear_sampling_uses_premultiplied_alpha() {
    const std::array<veyra::SoftwarePixel, 2> image_pixels{
        veyra::SoftwarePixel{.red = 255, .alpha = 0},
        veyra::SoftwarePixel{.blue = 255, .alpha = 255},
    };
    const auto image = veyra::SoftwareImage{
        .width = image_pixels.size(),
        .height = 1,
        .stride = image_pixels.size(),
        .pixels = image_pixels,
    };
    const auto command = veyra::DisplayCommand{veyra::DrawImage{
        .source = {.width = 2.0F, .height = 1.0F},
        .destination = {.width = 4.0F, .height = 1.0F},
        .image = &image,
        .sampling = veyra::SoftwareImageSampling::linear,
    }};
    std::array<veyra::SoftwarePixel, 4> pixels{};
    pixels.fill({.alpha = 0});
    const auto result = veyra::render_software(
        std::span<const veyra::DisplayCommand>(&command, 1),
        {.width = pixels.size(), .height = 1, .stride = pixels.size(), .pixels = pixels},
        {}
    );

    require(result == veyra::SoftwareRenderResult::complete, "transparent linear samples render");
    require(
        pixels[1].red == 0 && pixels[1].blue == 255 && pixels[1].alpha == 64,
        "transparent texels do not add color fringes"
    );
}

void check_image_opacity_modulates_source_alpha() {
    const std::array<veyra::SoftwarePixel, 1> image_pixels{
        veyra::SoftwarePixel{.red = 30, .green = 60, .blue = 90},
    };
    const auto image = veyra::SoftwareImage{
        .width = 1,
        .height = 1,
        .stride = 1,
        .pixels = image_pixels,
    };
    const auto command = veyra::DisplayCommand{veyra::DrawImage{
        .source = {.width = 1.0F, .height = 1.0F},
        .destination = {.width = 1.0F, .height = 1.0F},
        .image = &image,
        .opacity = 128,
    }};
    std::array<veyra::SoftwarePixel, 1> pixels{veyra::SoftwarePixel{.alpha = 0}};
    const auto result = veyra::render_software(
        std::span<const veyra::DisplayCommand>(&command, 1),
        {.width = 1, .height = 1, .stride = 1, .pixels = pixels},
        {}
    );

    require(result == veyra::SoftwareRenderResult::complete, "image opacity renders");
    require(
        pixels[0].red == 30 && pixels[0].green == 60 && pixels[0].blue == 90 &&
            pixels[0].alpha == 128,
        "image opacity scales source alpha"
    );

    auto invisible_command = command;
    std::get<veyra::DrawImage>(invisible_command).opacity = 0;
    pixels[0] = {.red = 7, .green = 8, .blue = 9, .alpha = 255};
    const auto invisible_result = veyra::render_software(
        std::span<const veyra::DisplayCommand>(&invisible_command, 1),
        {.width = 1, .height = 1, .stride = 1, .pixels = pixels},
        {}
    );

    require(
        invisible_result == veyra::SoftwareRenderResult::complete && pixels[0].red == 7 &&
            pixels[0].green == 8 && pixels[0].blue == 9 && pixels[0].alpha == 255,
        "zero image opacity leaves the destination unchanged"
    );

    const auto scaled_draw = [&](veyra::SoftwareImageSampling sampling) {
        const auto scaled_command = veyra::DisplayCommand{veyra::DrawImage{
            .source = {.width = 1.0F, .height = 1.0F},
            .destination = {.width = 2.0F, .height = 2.0F},
            .image = &image,
            .sampling = sampling,
            .opacity = 128,
        }};
        std::array<veyra::SoftwarePixel, 4> scaled_pixels{
            veyra::SoftwarePixel{.alpha = 0},
            veyra::SoftwarePixel{.alpha = 0},
            veyra::SoftwarePixel{.alpha = 0},
            veyra::SoftwarePixel{.alpha = 0},
        };
        const auto scaled_result = veyra::render_software(
            std::span<const veyra::DisplayCommand>(&scaled_command, 1),
            {.width = 2, .height = 2, .stride = 2, .pixels = scaled_pixels},
            {}
        );

        require(scaled_result == veyra::SoftwareRenderResult::complete, "scaled opacity renders");
        for (const auto& pixel : scaled_pixels) {
            require(
                pixel.red == 30 && pixel.green == 60 && pixel.blue == 90 && pixel.alpha == 128,
                "scaled sampling applies image opacity"
            );
        }
    };
    scaled_draw(veyra::SoftwareImageSampling::nearest);
    scaled_draw(veyra::SoftwareImageSampling::linear);
}

}

int main() {
    try {
        check_linear_color_interpolation();
        check_linear_sampling_uses_premultiplied_alpha();
        check_image_opacity_modulates_source_alpha();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
