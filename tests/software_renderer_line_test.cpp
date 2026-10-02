#include "veyra/render/software_renderer.hpp"

#include <array>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

void check_coverage_and_clip() {
    constexpr auto width = std::size_t{8};
    constexpr auto height = std::size_t{6};
    const std::array<veyra::DisplayCommand, 3> clipped_commands{
        veyra::PushClip{
            .bounds = {.x = 3.0F, .y = 1.0F, .width = 2.0F, .height = 4.0F},
        },
        veyra::StrokeLine{
            .start = {.x = 1.0F, .y = 2.5F},
            .end = {.x = 6.0F, .y = 2.5F},
            .thickness = 1.0F,
            .color = {.red = 200},
        },
        veyra::PopClip{},
    };
    std::array<veyra::SoftwarePixel, width * height> pixels{};
    pixels.fill(veyra::SoftwarePixel{.alpha = 0});
    std::array<veyra::SoftwareClipRect, 1> clips{};
    const auto result = veyra::render_software(
        clipped_commands,
        {
            .width = width,
            .height = height,
            .stride = width,
            .pixels = pixels,
        },
        {.clips = clips}
    );

    require(result == veyra::SoftwareRenderResult::complete, "clipped line renders");
    require(pixels[2 * width + 2].alpha == 0, "clip excludes pixels before the line");
    require(pixels[2 * width + 3].red == 200, "clip keeps pixels inside the line");
    require(pixels[2 * width + 4].red == 200, "clip includes its last pixel");
    require(pixels[2 * width + 5].alpha == 0, "clip excludes pixels after the line");
    require(pixels[1 * width + 3].alpha == 0, "thin line leaves the row above empty");
    require(pixels[3 * width + 3].alpha == 0, "thin line leaves the row below empty");
}

void check_round_caps_and_thickness() {
    constexpr auto width = std::size_t{8};
    constexpr auto height = std::size_t{8};
    const std::array<veyra::DisplayCommand, 1> commands{
        veyra::StrokeLine{
            .start = {.x = 3.0F, .y = 3.5F},
            .end = {.x = 3.0F, .y = 3.5F},
            .thickness = 4.0F,
            .color = {.green = 180},
        },
    };
    std::array<veyra::SoftwarePixel, width * height> pixels{};
    pixels.fill(veyra::SoftwarePixel{.alpha = 0});
    const auto result = veyra::render_software(
        commands,
        {
            .width = width,
            .height = height,
            .stride = width,
            .pixels = pixels,
        },
        {}
    );

    require(result == veyra::SoftwareRenderResult::complete, "round-capped point renders");
    require(pixels[3 * width + 3].green == 180, "point center is fully covered");
    require(pixels[2 * width + 3].alpha == 255, "round cap covers pixels inside its radius");
    require(pixels[2 * width + 1].alpha == 178, "round cap antialiases its outer edge");
    require(pixels[0].alpha == 0, "point bounds leave distant pixels empty");
}

void check_damage_and_invalid_input() {
    constexpr auto width = std::size_t{8};
    constexpr auto height = std::size_t{6};
    const std::array<veyra::DisplayCommand, 1> line{
        veyra::StrokeLine{
            .start = {.x = 0.0F, .y = 2.5F},
            .end = {.x = 7.0F, .y = 2.5F},
            .thickness = 1.0F,
            .color = {.blue = 255},
        },
    };
    const std::array<veyra::LayoutRect, 1> damage{
        veyra::LayoutRect{.x = 2.0F, .y = 2.0F, .width = 2.0F, .height = 1.0F},
    };
    std::array<veyra::SoftwarePixel, width * height> pixels{};
    pixels.fill(veyra::SoftwarePixel{.alpha = 0});
    auto result = veyra::render_software(
        line,
        {
            .width = width,
            .height = height,
            .stride = width,
            .pixels = pixels,
        },
        {},
        damage
    );
    require(result == veyra::SoftwareRenderResult::complete, "line respects damage bounds");
    require(pixels[2 * width + 1].alpha == 0, "damage leaves preceding pixels unchanged");
    require(pixels[2 * width + 2].blue == 255, "damage paints pixels inside its bounds");
    require(pixels[2 * width + 4].alpha == 0, "damage leaves following pixels unchanged");

    const std::array<veyra::DisplayCommand, 2> invalid_commands{
        veyra::FillRect{
            .bounds = {.width = static_cast<float>(width), .height = static_cast<float>(height)},
            .color = {.red = 255},
        },
        veyra::StrokeLine{
            .start = {.x = std::numeric_limits<float>::quiet_NaN(), .y = 1.0F},
            .end = {.x = 4.0F, .y = 4.0F},
            .thickness = 1.0F,
        },
    };
    pixels.fill(veyra::SoftwarePixel{.alpha = 0});
    result = veyra::render_software(
        invalid_commands,
        {
            .width = width,
            .height = height,
            .stride = width,
            .pixels = pixels,
        },
        {}
    );
    require(result == veyra::SoftwareRenderResult::invalid_input, "non-finite line is rejected");
    require(pixels[0].alpha == 0, "invalid line leaves the surface unchanged");
}

}

int main() {
    try {
        check_coverage_and_clip();
        check_round_caps_and_thickness();
        check_damage_and_invalid_input();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
