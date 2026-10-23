#include "veyra/render/sdl_gpu_renderer.hpp"
#include "veyra/render/software_renderer.hpp"

#include "sdl_gpu_parity_helpers.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using veyra::sdl_gpu_parity::check_parity;
using veyra::sdl_gpu_parity::render_reference;
using veyra::sdl_gpu_parity::require;
using veyra::sdl_gpu_parity::SdlGpuFixture;
using veyra::sdl_gpu_parity::SoftwareRooms;

constexpr std::size_t kWidth = 96;
constexpr std::size_t kHeight = 72;

void check_line_parity(
    veyra::SdlGpuRenderer& renderer,
    std::span<veyra::SoftwarePixel> expected,
    std::span<veyra::SoftwarePixel> actual,
    SoftwareRooms& rooms
) {
    const std::array<veyra::DisplayCommand, 5> storage{
        veyra::FillRect{
            .bounds = {.width = static_cast<float>(kWidth), .height = static_cast<float>(kHeight)},
            .color = {.red = 240, .green = 240, .blue = 244, .alpha = 255},
        },
        veyra::StrokeLine{
            .start = {.x = 8.0F, .y = 10.0F},
            .end = {.x = 60.0F, .y = 34.0F},
            .thickness = 3.0F,
            .color = {.red = 30, .green = 140, .blue = 60, .alpha = 255},
        },
        veyra::StrokeLine{
            .start = {.x = 10.0F, .y = 50.0F},
            .end = {.x = 70.0F, .y = 44.0F},
            .thickness = 5.0F,
            .color = {.red = 210, .green = 60, .blue = 40, .alpha = 160},
        },
        veyra::StrokeLine{
            .start = {.x = 40.0F, .y = 30.0F},
            .end = {.x = 40.0F, .y = 30.0F},
            .thickness = 6.0F,
            .color = {.red = 40, .green = 70, .blue = 210, .alpha = 255},
        },
        veyra::StrokeRect{
            .bounds = {.x = 70.0F, .y = 8.0F, .width = 16.0F, .height = 40.0F},
            .thickness = 0.0F,
            .color = {.red = 150, .green = 40, .blue = 170, .alpha = 255},
        },
    };

    const auto scene = std::span<const veyra::DisplayCommand>{storage.data(), storage.size()};

    render_reference(scene, expected, kWidth, kHeight, rooms);

    auto stats = veyra::SdlGpuRenderStats{};

    require(
        veyra::render_sdl_gpu_rects(renderer, scene, actual, stats) == veyra::WindowResult::ready,
        "lines and a zero-width stroke complete"
    );
    require(stats.submitted == 4, "zero-width strokes never submit");
    require(stats.stroke_draws >= 1, "segments draw through the stroke pipeline");

    check_parity(renderer, scene, expected, actual, "stroke-line", 2);
}

void check_polyline_parity(
    veyra::SdlGpuRenderer& renderer,
    std::span<veyra::SoftwarePixel> expected,
    std::span<veyra::SoftwarePixel> actual,
    SoftwareRooms& rooms
) {
    auto open_points = std::array<veyra::LayoutPoint, 5>{
        veyra::LayoutPoint{.x = 6.0F, .y = 60.0F},
        veyra::LayoutPoint{.x = 22.0F, .y = 40.0F},
        veyra::LayoutPoint{.x = 38.0F, .y = 58.0F},
        veyra::LayoutPoint{.x = 54.0F, .y = 38.0F},
        veyra::LayoutPoint{.x = 70.0F, .y = 56.0F},
    };

    auto closed_points = std::array<veyra::LayoutPoint, 3>{
        veyra::LayoutPoint{.x = 72.0F, .y = 8.0F},
        veyra::LayoutPoint{.x = 88.0F, .y = 24.0F},
        veyra::LayoutPoint{.x = 72.0F, .y = 24.0F},
    };

    const std::array<veyra::DisplayCommand, 3> storage{
        veyra::FillRect{
            .bounds = {.width = static_cast<float>(kWidth), .height = static_cast<float>(kHeight)},
            .color = {.red = 18, .green = 20, .blue = 28, .alpha = 255},
        },
        veyra::StrokePolyline{
            .points = open_points,
            .thickness = 2.0F,
            .color = {.red = 230, .green = 232, .blue = 240, .alpha = 255},
            .closed = false,
        },
        veyra::StrokePolyline{
            .points = closed_points,
            .thickness = 3.0F,
            .color = {.red = 230, .green = 180, .blue = 20, .alpha = 200},
            .closed = true,
        },
    };

    const auto scene = std::span<const veyra::DisplayCommand>{storage.data(), storage.size()};

    render_reference(scene, expected, kWidth, kHeight, rooms);

    auto stats = veyra::SdlGpuRenderStats{};

    require(
        veyra::render_sdl_gpu_rects(renderer, scene, actual, stats) == veyra::WindowResult::ready,
        "open and closed polylines complete"
    );
    require(stats.submitted == 8, "polylines submit one instance per segment");
    require(stats.stroke_draws >= 1, "segments draw through the stroke pipeline");

    check_parity(renderer, scene, expected, actual, "stroke-polyline", 2);
}

void check_rounded_stroke_parity(
    veyra::SdlGpuRenderer& renderer,
    std::span<veyra::SoftwarePixel> expected,
    std::span<veyra::SoftwarePixel> actual,
    SoftwareRooms& rooms
) {
    const std::array<veyra::DisplayCommand, 7> storage{
        veyra::FillRect{
            .bounds = {.width = static_cast<float>(kWidth), .height = static_cast<float>(kHeight)},
            .color = {.red = 240, .green = 240, .blue = 244, .alpha = 255},
        },
        veyra::StrokeRoundedRect{
            .bounds = {.x = 8.0F, .y = 6.0F, .width = 64.0F, .height = 32.0F},
            .radius = 8.0F,
            .thickness = 4.0F,
            .color = {.red = 30, .green = 140, .blue = 60, .alpha = 255},
        },
        veyra::StrokeRoundedRect{
            .bounds = {.x = 12.0F, .y = 44.0F, .width = 40.0F, .height = 16.0F},
            .radius = 2.0F,
            .thickness = 2.0F,
            .color = {.red = 210, .green = 60, .blue = 40, .alpha = 160},
        },
        veyra::StrokeRoundedRect{
            .bounds = {.x = 56.0F, .y = 44.0F, .width = 32.0F, .height = 16.0F},
            .radius = 6.0F,
            .thickness = 20.0F,
            .color = {.red = 40, .green = 70, .blue = 210, .alpha = 255},
        },
        veyra::StrokeRoundedRect{
            .bounds = {.x = 76.0F, .y = 6.0F, .width = 12.0F, .height = 32.0F},
            .radius = 0.0F,
            .thickness = 2.0F,
            .color = {.red = 150, .green = 40, .blue = 170, .alpha = 255},
        },
        veyra::StrokeRoundedRect{
            .bounds = {.x = 8.0F, .y = 6.0F, .width = 8.0F, .height = 8.0F},
            .radius = 2.0F,
            .thickness = 0.0F,
            .color = {.red = 150, .green = 40, .blue = 170, .alpha = 255},
        },
        veyra::StrokeRoundedRect{
            .bounds = {.x = 8.0F, .y = 6.0F, .width = 0.0F, .height = 8.0F},
            .radius = 2.0F,
            .thickness = 2.0F,
            .color = {.red = 150, .green = 40, .blue = 170, .alpha = 255},
        },
    };

    const auto scene = std::span<const veyra::DisplayCommand>{storage.data(), storage.size()};

    render_reference(scene, expected, kWidth, kHeight, rooms);

    auto stats = veyra::SdlGpuRenderStats{};

    require(
        veyra::render_sdl_gpu_rects(renderer, scene, actual, stats) == veyra::WindowResult::ready,
        "rounded strokes complete"
    );
    require(stats.rounded_stroke_draws >= 1, "rings draw through the rounded stroke pipeline");

    check_parity(renderer, scene, expected, actual, "rounded-stroke", 2);
}

void check_stroke_rejections(
    veyra::SdlGpuRenderer& renderer,
    std::span<veyra::SoftwarePixel> scratch
) {
    auto stats = veyra::SdlGpuRenderStats{};

    const auto nan = std::numeric_limits<float>::quiet_NaN();

    const std::array<veyra::DisplayCommand, 1> negative{
        veyra::StrokeLine{
            .start = {.x = 0.0F, .y = 0.0F},
            .end = {.x = 8.0F, .y = 8.0F},
            .thickness = -1.0F,
            .color = {.red = 255, .alpha = 255},
        },
    };

    require(
        veyra::render_sdl_gpu_rects(
            renderer,
            std::span<const veyra::DisplayCommand>(negative),
            scratch,
            stats
        ) == veyra::WindowResult::invalid_input,
        "negative thickness is rejected"
    );

    const std::array<veyra::DisplayCommand, 1> indefinite{
        veyra::StrokePolyline{
            .points = std::span<const veyra::LayoutPoint>{},
            .thickness = nan,
            .color = {.red = 255, .alpha = 255},
            .closed = false,
        },
    };

    require(
        veyra::render_sdl_gpu_rects(
            renderer,
            std::span<const veyra::DisplayCommand>(indefinite),
            scratch,
            stats
        ) == veyra::WindowResult::invalid_input,
        "NaN thickness is rejected"
    );

    const auto points = std::array<veyra::LayoutPoint, 2>{
        veyra::LayoutPoint{.x = 0.0F, .y = 0.0F},
        veyra::LayoutPoint{.x = std::numeric_limits<float>::infinity(), .y = 0.0F},
    };

    const std::array<veyra::DisplayCommand, 1> unbounded{
        veyra::StrokeLine{
            .start = points[0],
            .end = points[1],
            .thickness = 2.0F,
            .color = {.red = 255, .alpha = 255},
        },
    };

    require(
        veyra::render_sdl_gpu_rects(
            renderer,
            std::span<const veyra::DisplayCommand>(unbounded),
            scratch,
            stats
        ) == veyra::WindowResult::invalid_input,
        "non-finite endpoints are rejected"
    );

    const auto single_point = std::array<veyra::LayoutPoint, 1>{
        veyra::LayoutPoint{.x = 8.0F, .y = 8.0F},
    };

    const std::array<veyra::DisplayCommand, 1> degenerate{
        veyra::StrokePolyline{
            .points = single_point,
            .thickness = 2.0F,
            .color = {.red = 255, .alpha = 255},
            .closed = false,
        },
    };

    require(
        veyra::render_sdl_gpu_rects(
            renderer,
            std::span<const veyra::DisplayCommand>(degenerate),
            scratch,
            stats
        ) == veyra::WindowResult::invalid_input,
        "single-point polylines are rejected"
    );
}

int run() {
    auto fixture = SdlGpuFixture{};

    if (!fixture.setup(kWidth, kHeight)) {
        std::cout << "sdl-gpu device or window unavailable, skipping\n";

        return 77;
    }

    std::cout << "sdl-gpu driver=" << fixture.presenter.device_driver() << '\n';

    auto rooms = SoftwareRooms{};
    auto expected = std::array<veyra::SoftwarePixel, kWidth * kHeight>{};
    auto actual = std::array<veyra::SoftwarePixel, kWidth * kHeight>{};

    check_line_parity(fixture.renderer, expected, actual, rooms);
    check_polyline_parity(fixture.renderer, expected, actual, rooms);
    check_rounded_stroke_parity(fixture.renderer, expected, actual, rooms);
    check_stroke_rejections(fixture.renderer, actual);

    fixture.teardown();

    require(!veyra::sdl_gpu_renderer_ready(fixture.renderer), "renderer tears down");

    return 0;
}

}

int main() {
    try {
        return run();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
