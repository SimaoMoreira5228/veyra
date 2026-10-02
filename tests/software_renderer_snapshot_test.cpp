#include "veyra/render/software_renderer.hpp"

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

void check_display_list_snapshot() {
    constexpr auto width = std::size_t{4};
    constexpr auto height = std::size_t{3};
    constexpr auto background = veyra::SoftwarePixel{
        .red = 8,
        .green = 16,
        .blue = 24,
    };
    const std::array<std::uint8_t, 2> coverage{128, 255};
    const auto mask = veyra::SoftwareMask{
        .width = coverage.size(),
        .height = 1,
        .stride = coverage.size(),
        .coverage = coverage,
    };
    const std::array image_pixels{
        veyra::SoftwarePixel{
            .green = 255,
            .alpha = 128,
        },
    };
    const auto image = veyra::SoftwareImage{
        .width = 1,
        .height = 1,
        .stride = 1,
        .pixels = image_pixels,
    };
    const std::array<veyra::DisplayCommand, 6> commands{
        veyra::FillRect{
            .bounds = {.width = static_cast<float>(width), .height = static_cast<float>(height)},
            .color =
                {
                    .red = background.red,
                    .green = background.green,
                    .blue = background.blue,
                },
        },
        veyra::StrokeRect{
            .bounds = {.width = static_cast<float>(width), .height = static_cast<float>(height)},
            .thickness = 1.0F,
            .color = {.red = 255, .alpha = 255},
        },
        veyra::PushClip{
            .bounds = {.x = 1.0F, .y = 1.0F, .width = 2.0F, .height = 1.0F},
        },
        veyra::DrawMask{
            .x = 1,
            .y = 1,
            .mask = &mask,
            .color = {.blue = 255},
        },
        veyra::PopClip{},
        veyra::DrawImage{
            .x = 3,
            .y = 1,
            .image = &image,
        },
    };
    const std::array<veyra::SoftwarePixel, width * height> expected{
        veyra::SoftwarePixel{.red = 255},
        veyra::SoftwarePixel{.red = 255},
        veyra::SoftwarePixel{.red = 255},
        veyra::SoftwarePixel{.red = 255},
        veyra::SoftwarePixel{.red = 255},
        veyra::SoftwarePixel{.red = 4, .green = 8, .blue = 140},
        veyra::SoftwarePixel{.blue = 255},
        veyra::SoftwarePixel{.red = 127, .green = 128},
        veyra::SoftwarePixel{.red = 255},
        veyra::SoftwarePixel{.red = 255},
        veyra::SoftwarePixel{.red = 255},
        veyra::SoftwarePixel{.red = 255},
    };
    std::array<veyra::SoftwarePixel, width * height> pixels{};
    std::array<veyra::SoftwareClipRect, 1> clips{};
    const auto result = veyra::render_software(
        commands,
        {
            .width = width,
            .height = height,
            .stride = width,
            .pixels = pixels,
        },
        {.clips = clips}
    );
    require(result == veyra::SoftwareRenderResult::complete, "display list renders");
    for (std::size_t index = 0; index < pixels.size(); ++index) {
        require(
            pixels[index].red == expected[index].red &&
                pixels[index].green == expected[index].green &&
                pixels[index].blue == expected[index].blue &&
                pixels[index].alpha == expected[index].alpha,
            "rendered pixels match the snapshot"
        );
    }
}

}

int main() {
    try {
        check_display_list_snapshot();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
