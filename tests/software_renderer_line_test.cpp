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

void check_polyline_matches_line_and_composites_joins_once() {
    constexpr auto width = std::size_t{8};
    constexpr auto height = std::size_t{8};
    const std::array points{
        veyra::LayoutPoint{.x = 1.0F, .y = 3.5F},
        veyra::LayoutPoint{.x = 6.0F, .y = 3.5F},
    };
    const std::array<veyra::DisplayCommand, 1> line_commands{
        veyra::StrokeLine{
            .start = points[0],
            .end = points[1],
            .thickness = 2.0F,
            .color = {.red = 180, .green = 60, .blue = 20, .alpha = 128},
        },
    };
    const std::array<veyra::DisplayCommand, 1> polyline_commands{
        veyra::StrokePolyline{
            .points = points,
            .thickness = 2.0F,
            .color = {.red = 180, .green = 60, .blue = 20, .alpha = 128},
        },
    };
    std::array<veyra::SoftwarePixel, width * height> line_pixels{};
    std::array<veyra::SoftwarePixel, width * height> polyline_pixels{};
    line_pixels.fill({.alpha = 0});
    polyline_pixels.fill({.alpha = 0});
    const auto line_surface = veyra::SoftwareSurface{
        .width = width,
        .height = height,
        .stride = width,
        .pixels = line_pixels,
    };
    const auto polyline_surface = veyra::SoftwareSurface{
        .width = width,
        .height = height,
        .stride = width,
        .pixels = polyline_pixels,
    };
    require(
        veyra::render_software(line_commands, line_surface, {}) ==
                veyra::SoftwareRenderResult::complete &&
            veyra::render_software(polyline_commands, polyline_surface, {}) ==
                veyra::SoftwareRenderResult::complete,
        "single-segment polyline renders like a line without extra workspace"
    );
    for (std::size_t index = 0; index < line_pixels.size(); ++index) {
        require(
            line_pixels[index].red == polyline_pixels[index].red &&
                line_pixels[index].green == polyline_pixels[index].green &&
                line_pixels[index].blue == polyline_pixels[index].blue &&
                line_pixels[index].alpha == polyline_pixels[index].alpha,
            "single-segment polyline matches line coverage"
        );
    }

    std::array<std::uint8_t, width> coverage_row{};
    const auto polyline_workspace = veyra::SoftwareRenderWorkspace{
        .stroke_coverage = coverage_row,
    };
    const std::array joined_points{
        veyra::LayoutPoint{.x = 1.5F, .y = 3.5F},
        veyra::LayoutPoint{.x = 3.5F, .y = 3.5F},
        veyra::LayoutPoint{.x = 3.5F, .y = 5.5F},
    };
    const std::array<veyra::DisplayCommand, 1> joined_commands{
        veyra::StrokePolyline{
            .points = joined_points,
            .thickness = 2.0F,
            .color = {.red = 200, .alpha = 128},
        },
    };
    std::array<veyra::SoftwarePixel, width * height> joined_pixels{};
    joined_pixels.fill({.alpha = 0});
    const auto joined_result = veyra::render_software(
        joined_commands,
        {
            .width = width,
            .height = height,
            .stride = width,
            .pixels = joined_pixels,
        },
        polyline_workspace
    );

    require(joined_result == veyra::SoftwareRenderResult::complete, "polyline joins render");
    require(joined_pixels[3 * width + 3].alpha == 128, "translucent join blends once");
}

void check_closed_polyline_and_invalid_input() {
    constexpr auto width = std::size_t{6};
    constexpr auto height = std::size_t{6};
    const std::array triangle{
        veyra::LayoutPoint{.x = 1.5F, .y = 1.5F},
        veyra::LayoutPoint{.x = 4.5F, .y = 1.5F},
        veyra::LayoutPoint{.x = 4.5F, .y = 4.5F},
    };
    const std::array<veyra::DisplayCommand, 1> closed_commands{
        veyra::StrokePolyline{
            .points = triangle,
            .thickness = 1.0F,
            .color = {.blue = 255},
            .closed = true,
        },
    };
    std::array<veyra::SoftwarePixel, width * height> pixels{};
    pixels.fill({.alpha = 0});
    const auto surface = veyra::SoftwareSurface{
        .width = width,
        .height = height,
        .stride = width,
        .pixels = pixels,
    };
    std::array<std::uint8_t, width> coverage_row{};
    require(
        veyra::render_software(closed_commands, surface, {}) ==
            veyra::SoftwareRenderResult::insufficient_workspace,
        "polyline reports a missing coverage row before drawing"
    );
    require(pixels[3 * width + 3].alpha == 0, "missing coverage storage leaves pixels unchanged");
    require(
        veyra::render_software(closed_commands, surface, {.stroke_coverage = coverage_row}) ==
            veyra::SoftwareRenderResult::complete,
        "closed polyline renders its final segment"
    );
    require(
        pixels[3 * width + 3].blue == 255,
        "closed polyline joins its final point to its start"
    );

    const std::array one_point{veyra::LayoutPoint{}};
    const std::array<veyra::DisplayCommand, 1> invalid_commands{
        veyra::StrokePolyline{
            .points = one_point,
            .thickness = 1.0F,
        },
    };
    require(
        veyra::render_software(invalid_commands, surface, {}) ==
            veyra::SoftwareRenderResult::invalid_input,
        "polylines need at least two points"
    );

    const std::array invisible_points{
        veyra::LayoutPoint{},
        veyra::LayoutPoint{.x = 2.0F, .y = 2.0F},
    };
    const std::array<veyra::DisplayCommand, 1> invisible_commands{
        veyra::StrokePolyline{
            .points = invisible_points,
            .thickness = 0.0F,
            .color = {.red = 255},
        },
    };
    pixels.fill({.alpha = 0});
    require(
        veyra::render_software(invisible_commands, surface, {}) ==
            veyra::SoftwareRenderResult::complete,
        "invisible polylines need no coverage storage"
    );
    require(pixels[3 * width + 3].alpha == 0, "zero-width polyline leaves pixels unchanged");
}

void check_polyline_translation_and_clipping() {
    constexpr auto width = std::size_t{8};
    constexpr auto height = std::size_t{6};
    const std::array points{
        veyra::LayoutPoint{.x = 0.5F, .y = 1.5F},
        veyra::LayoutPoint{.x = 5.5F, .y = 1.5F},
    };
    const std::array<veyra::DisplayCommand, 5> commands{
        veyra::PushTranslation{.offset = {.x = 2.0F, .y = 1.0F}},
        veyra::PushClip{.bounds = {.width = 4.0F, .height = 3.0F}},
        veyra::StrokePolyline{
            .points = points,
            .thickness = 1.0F,
            .color = {.green = 220},
        },
        veyra::PopClip{},
        veyra::PopTranslation{},
    };
    std::array<veyra::SoftwarePixel, width * height> pixels{};
    pixels.fill({.alpha = 0});
    std::array<veyra::SoftwareClipRect, 1> clips{};
    std::array<veyra::LayoutPoint, 1> translations{};
    std::array<std::uint8_t, width> coverage_row{};
    const auto result = veyra::render_software(
        commands,
        {.width = width, .height = height, .stride = width, .pixels = pixels},
        {
            .clips = clips,
            .translations = translations,
            .stroke_coverage = coverage_row,
        }
    );

    require(result == veyra::SoftwareRenderResult::complete, "translated polyline renders");
    require(pixels[2 * width + 2].green == 220, "translation moves polyline into the clip");
    require(pixels[2 * width + 5].green == 220, "clip keeps the last covered pixel");
    require(pixels[2 * width + 6].alpha == 0, "clip excludes the rest of the polyline");
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
        check_polyline_matches_line_and_composites_joins_once();
        check_closed_polyline_and_invalid_input();
        check_polyline_translation_and_clipping();
        check_damage_and_invalid_input();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
