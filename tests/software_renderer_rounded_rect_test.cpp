#include "veyra/render/cpu/software_renderer.hpp"

#include <array>
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

bool pixel_is(
    const veyra::SoftwarePixel& pixel,
    std::uint8_t red,
    std::uint8_t green,
    std::uint8_t blue,
    std::uint8_t alpha
) {
    return pixel.red == red && pixel.green == green && pixel.blue == blue && pixel.alpha == alpha;
}

void check_fill_and_radius_validation() {
    constexpr std::size_t width = 7;
    constexpr std::size_t height = 7;
    std::array<veyra::SoftwarePixel, width * height> pixels{};
    pixels.fill({.alpha = 0});
    const std::array<veyra::DisplayCommand, 1> commands{
        veyra::FillRoundedRect{
            .bounds = {.x = 1.0F, .y = 1.0F, .width = 5.0F, .height = 5.0F},
            .radius = 2.0F,
            .color = {.red = 20, .green = 40, .blue = 60},
        },
    };
    const auto surface = veyra::SoftwareSurface{
        .width = width,
        .height = height,
        .stride = width,
        .pixels = pixels,
    };

    require(
        veyra::render_software(commands, surface, {}) == veyra::SoftwareRenderResult::complete,
        "rounded rectangle renders"
    );
    require(pixel_is(pixels[3 * width + 3], 20, 40, 60, 255), "center is filled");
    require(pixel_is(pixels[0], 0, 0, 0, 0), "pixels outside the bounds stay transparent");
    require(pixels[1 * width + 1].alpha == 97, "rounded corner uses partial coverage");

    auto tiny_pixels = pixels;
    tiny_pixels.fill({.alpha = 0});
    const std::array<veyra::DisplayCommand, 1> tiny_command{
        veyra::FillRoundedRect{
            .bounds = {.x = 2.25F, .y = 2.25F, .width = 0.5F, .height = 0.5F},
            .radius = 0.25F,
            .color = {.red = 20, .green = 40, .blue = 60},
        },
    };
    const auto tiny_surface = veyra::SoftwareSurface{
        .width = width,
        .height = height,
        .stride = width,
        .pixels = tiny_pixels,
    };
    require(
        veyra::render_software(tiny_command, tiny_surface, {}) ==
            veyra::SoftwareRenderResult::complete,
        "subpixel rounded rectangle renders"
    );
    require(
        tiny_pixels[2 * width + 2].alpha == 191,
        "subpixel rounded rectangle retains center coverage"
    );

    tiny_pixels.fill({.alpha = 0});
    const std::array<veyra::DisplayCommand, 1> narrow_command{
        veyra::FillRoundedRect{
            .bounds = {.x = 2.25F, .y = -0.3F, .width = 0.5F, .height = 7.2F},
            .radius = 0.01F,
            .color = {.red = 20, .green = 40, .blue = 60},
        },
    };
    require(
        veyra::render_software(narrow_command, tiny_surface, {}) ==
            veyra::SoftwareRenderResult::complete,
        "narrow rounded rectangle renders"
    );
    require(
        tiny_pixels[6 * width + 2].alpha == 191,
        "narrow rounded rectangle does not blend its center twice"
    );

    auto invalid = commands;
    std::get<veyra::FillRoundedRect>(invalid[0]).radius = -1.0F;
    pixels.fill({.alpha = 0});
    require(
        veyra::render_software(invalid, surface, {}) == veyra::SoftwareRenderResult::invalid_input,
        "negative corner radius is rejected"
    );
    require(pixel_is(pixels[3 * width + 3], 0, 0, 0, 0), "invalid radius leaves pixels untouched");
}

void check_rounded_border() {
    constexpr std::size_t width = 10;
    constexpr std::size_t height = 10;
    std::array<veyra::SoftwarePixel, width * height> pixels{};
    pixels.fill({.alpha = 0});
    const std::array<veyra::DisplayCommand, 1> commands{
        veyra::StrokeRoundedRect{
            .bounds = {.x = 1.0F, .y = 1.0F, .width = 8.0F, .height = 8.0F},
            .radius = 3.0F,
            .thickness = 2.0F,
            .color = {.red = 80, .green = 100, .blue = 120},
        },
    };
    const auto surface = veyra::SoftwareSurface{
        .width = width,
        .height = height,
        .stride = width,
        .pixels = pixels,
    };

    require(
        veyra::render_software(commands, surface, {}) == veyra::SoftwareRenderResult::complete,
        "rounded border renders"
    );
    require(pixel_is(pixels[1 * width + 4], 80, 100, 120, 255), "top border is filled");
    require(pixel_is(pixels[4 * width + 4], 0, 0, 0, 0), "rounded border leaves its center clear");
    require(
        pixels[1 * width + 2].alpha > 0 && pixels[1 * width + 2].alpha < 255,
        "rounded border antialiases its corner"
    );

    auto zero_thickness = commands;
    std::get<veyra::StrokeRoundedRect>(zero_thickness[0]).thickness = 0.0F;
    pixels.fill({.alpha = 0});
    require(
        veyra::render_software(zero_thickness, surface, {}) ==
            veyra::SoftwareRenderResult::complete,
        "zero-thickness rounded border renders as empty"
    );
    require(pixel_is(pixels[1 * width + 4], 0, 0, 0, 0), "zero thickness leaves pixels untouched");

    auto full_thickness = commands;
    std::get<veyra::StrokeRoundedRect>(full_thickness[0]).thickness = 8.0F;
    require(
        veyra::render_software(full_thickness, surface, {}) ==
            veyra::SoftwareRenderResult::complete,
        "full-size rounded border renders as a fill"
    );
    require(
        pixel_is(pixels[4 * width + 4], 80, 100, 120, 255),
        "full-size border fills the center"
    );

    auto invalid = commands;
    std::get<veyra::StrokeRoundedRect>(invalid[0]).thickness = -1.0F;
    pixels.fill({.alpha = 0});
    require(
        veyra::render_software(invalid, surface, {}) == veyra::SoftwareRenderResult::invalid_input,
        "negative rounded border thickness is rejected"
    );
    require(
        pixel_is(pixels[4 * width + 4], 0, 0, 0, 0),
        "invalid thickness leaves pixels untouched"
    );
}

}

int main() {
    try {
        check_fill_and_radius_validation();
        check_rounded_border();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
