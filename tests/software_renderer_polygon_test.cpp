#include "veyra/render/software_renderer.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
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

bool pixel_is(
    const veyra::SoftwarePixel& pixel,
    std::uint8_t red,
    std::uint8_t green,
    std::uint8_t blue,
    std::uint8_t alpha = 255
) {
    return pixel.red == red && pixel.green == green && pixel.blue == blue && pixel.alpha == alpha;
}

void check_concave_polygon_and_clipping() {
    constexpr auto width = std::size_t{6};
    constexpr auto height = std::size_t{6};
    constexpr auto color = veyra::DisplayColor{
        .red = 30,
        .green = 60,
        .blue = 90,
    };
    const std::array points{
        veyra::LayoutPoint{.x = 0.0F, .y = 0.0F},
        veyra::LayoutPoint{.x = 4.0F, .y = 0.0F},
        veyra::LayoutPoint{.x = 4.0F, .y = 1.0F},
        veyra::LayoutPoint{.x = 1.0F, .y = 1.0F},
        veyra::LayoutPoint{.x = 1.0F, .y = 4.0F},
        veyra::LayoutPoint{.x = 0.0F, .y = 4.0F},
    };
    const std::array<veyra::DisplayCommand, 5> commands{
        veyra::PushTranslation{
            .offset = {.x = 1.0F, .y = 1.0F},
        },
        veyra::PushClip{
            .bounds = {.width = 3.0F, .height = 3.0F},
        },
        veyra::FillPolygon{
            .points = points,
            .color = color,
        },
        veyra::PopClip{},
        veyra::PopTranslation{},
    };
    std::array<veyra::SoftwarePixel, width * height> pixels{};
    std::array<veyra::SoftwareClipRect, 1> clips{};
    std::array<veyra::LayoutPoint, 1> translations{};
    std::array<double, 6> intersections{};
    const auto surface = veyra::SoftwareSurface{
        .width = width,
        .height = height,
        .stride = width,
        .pixels = pixels,
    };
    const auto workspace = veyra::SoftwareRenderWorkspace{
        .clips = clips,
        .translations = translations,
        .polygon_intersections = intersections,
    };

    require(
        veyra::render_software(commands, surface, workspace) ==
            veyra::SoftwareRenderResult::complete,
        "concave polygon renders"
    );
    require(pixel_is(pixels[1 * width + 1], 30, 60, 90), "polygon uses translated coordinates");
    require(pixel_is(pixels[1 * width + 3], 30, 60, 90), "polygon fills its upper arm");
    require(pixel_is(pixels[2 * width + 1], 30, 60, 90), "polygon fills its lower arm");
    require(pixel_is(pixels[2 * width + 2], 0, 0, 0), "concave cutout remains empty");
    require(pixel_is(pixels[1 * width + 4], 0, 0, 0), "clip bounds the polygon");

    std::array<veyra::SoftwarePixel, width * height> untouched{};
    auto insufficient_workspace = workspace;
    insufficient_workspace.polygon_intersections = {};
    const auto insufficient_result = veyra::render_software(
        commands,
        {
            .width = width,
            .height = height,
            .stride = width,
            .pixels = untouched,
        },
        insufficient_workspace
    );
    require(
        insufficient_result == veyra::SoftwareRenderResult::insufficient_workspace,
        "polygon intersection scratch is checked before drawing"
    );
    require(
        std::all_of(
            untouched.begin(),
            untouched.end(),
            [](const auto& pixel) {
                return pixel_is(pixel, 0, 0, 0);
            }
        ),
        "insufficient polygon scratch leaves pixels unchanged"
    );
}

void check_invalid_polygons() {
    const std::array too_few_points{
        veyra::LayoutPoint{},
        veyra::LayoutPoint{.x = 1.0F},
    };
    const std::array<veyra::DisplayCommand, 1> too_few_commands{
        veyra::FillPolygon{
            .points = too_few_points,
        },
    };
    const std::array non_finite_points{
        veyra::LayoutPoint{},
        veyra::LayoutPoint{.x = 1.0F},
        veyra::LayoutPoint{
            .y = std::numeric_limits<float>::infinity(),
        },
    };
    const std::array<veyra::DisplayCommand, 1> non_finite_commands{
        veyra::FillPolygon{
            .points = non_finite_points,
        },
    };
    std::array<veyra::SoftwarePixel, 1> pixels{};
    const auto surface = veyra::SoftwareSurface{
        .width = 1,
        .height = 1,
        .stride = 1,
        .pixels = pixels,
    };

    require(
        veyra::render_software(too_few_commands, surface, {}) ==
            veyra::SoftwareRenderResult::invalid_input,
        "polygons need at least three points"
    );
    require(
        veyra::render_software(non_finite_commands, surface, {}) ==
            veyra::SoftwareRenderResult::invalid_input,
        "non-finite polygon points are rejected"
    );
}

void check_alpha_compositing() {
    const std::array points{
        veyra::LayoutPoint{},
        veyra::LayoutPoint{.x = 1.0F},
        veyra::LayoutPoint{.x = 1.0F, .y = 1.0F},
        veyra::LayoutPoint{.y = 1.0F},
    };
    const std::array<veyra::DisplayCommand, 1> commands{
        veyra::FillPolygon{
            .points = points,
            .color = {.red = 100, .green = 120, .blue = 140, .alpha = 128},
        },
    };
    std::array<veyra::SoftwarePixel, 1> pixels{
        veyra::SoftwarePixel{
            .alpha = 0,
        },
    };
    const auto surface = veyra::SoftwareSurface{
        .width = 1,
        .height = 1,
        .stride = 1,
        .pixels = pixels,
    };
    std::array<double, 4> intersections{};

    require(
        veyra::render_software(
            commands,
            surface,
            {
                .polygon_intersections = intersections,
            }
        ) == veyra::SoftwareRenderResult::complete,
        "translucent polygon renders"
    );
    require(pixel_is(pixels[0], 100, 120, 140, 128), "polygon uses source-over alpha");
}

}

int main() {
    try {
        check_concave_polygon_and_clipping();
        check_invalid_polygons();
        check_alpha_compositing();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
