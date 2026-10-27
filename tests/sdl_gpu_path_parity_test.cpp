#include "veyra/render/gpu/sdl/sdl_gpu_renderer.hpp"
#include "veyra/render/cpu/software_renderer.hpp"

#include "sdl_gpu_parity_helpers.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
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

void check_path_parity(
    veyra::SdlGpuRenderer& renderer,
    std::span<veyra::SoftwarePixel> expected,
    std::span<veyra::SoftwarePixel> actual,
    SoftwareRooms& rooms
) {
    const std::array<veyra::SoftwarePathSegment, 3> outer_segments{
        veyra::SoftwareLineTo{.end = {.x = 60.0F, .y = 10.0F}},
        veyra::SoftwareLineTo{.end = {.x = 60.0F, .y = 40.0F}},
        veyra::SoftwareLineTo{.end = {.x = 10.0F, .y = 40.0F}},
    };
    const std::array<veyra::SoftwarePathSegment, 3> hole_segments{
        veyra::SoftwareLineTo{.end = {.x = 45.0F, .y = 18.0F}},
        veyra::SoftwareLineTo{.end = {.x = 45.0F, .y = 32.0F}},
        veyra::SoftwareLineTo{.end = {.x = 25.0F, .y = 32.0F}},
    };
    const std::array<veyra::SoftwarePathContour, 2> holed_contours{
        veyra::SoftwarePathContour{
            .start = {.x = 10.0F, .y = 10.0F},
            .segments = outer_segments,
        },
        veyra::SoftwarePathContour{
            .start = {.x = 25.0F, .y = 18.0F},
            .segments = hole_segments,
        },
    };

    const std::array<veyra::SoftwarePathSegment, 3> union_left_segments{
        veyra::SoftwareLineTo{.end = {.x = 34.0F, .y = 44.0F}},
        veyra::SoftwareLineTo{.end = {.x = 34.0F, .y = 64.0F}},
        veyra::SoftwareLineTo{.end = {.x = 10.0F, .y = 64.0F}},
    };
    const std::array<veyra::SoftwarePathSegment, 3> union_right_segments{
        veyra::SoftwareLineTo{.end = {.x = 48.0F, .y = 44.0F}},
        veyra::SoftwareLineTo{.end = {.x = 48.0F, .y = 64.0F}},
        veyra::SoftwareLineTo{.end = {.x = 24.0F, .y = 64.0F}},
    };
    const std::array<veyra::SoftwarePathContour, 2> union_contours{
        veyra::SoftwarePathContour{
            .start = {.x = 10.0F, .y = 44.0F},
            .segments = union_left_segments,
        },
        veyra::SoftwarePathContour{
            .start = {.x = 24.0F, .y = 44.0F},
            .segments = union_right_segments,
        },
    };

    const std::array<veyra::SoftwarePathSegment, 3> dome_segments{
        veyra::SoftwareLineTo{.end = {.x = 90.0F, .y = 20.0F}},
        veyra::SoftwareQuadraticTo{
            .control = {.x = 75.0F, .y = 5.0F},
            .end = {.x = 60.0F, .y = 20.0F},
        },
        veyra::SoftwareLineTo{.end = {.x = 60.0F, .y = 40.0F}},
    };
    const std::array<veyra::SoftwarePathContour, 1> dome_contours{
        veyra::SoftwarePathContour{
            .start = {.x = 90.0F, .y = 40.0F},
            .segments = dome_segments,
        },
    };

    const std::array<veyra::SoftwarePathSegment, 3> blob_segments{
        veyra::SoftwareCubicTo{
            .control_1 = {.x = 66.0F, .y = 44.0F},
            .control_2 = {.x = 84.0F, .y = 44.0F},
            .end = {.x = 88.0F, .y = 56.0F},
        },
        veyra::SoftwareLineTo{.end = {.x = 88.0F, .y = 64.0F}},
        veyra::SoftwareLineTo{.end = {.x = 56.0F, .y = 64.0F}},
    };
    const std::array<veyra::SoftwarePathContour, 1> blob_contours{
        veyra::SoftwarePathContour{
            .start = {.x = 56.0F, .y = 52.0F},
            .segments = blob_segments,
        },
    };

    const std::array<veyra::DisplayCommand, 7> storage{
        veyra::FillRect{
            .bounds = {.width = static_cast<float>(kWidth), .height = static_cast<float>(kHeight)},
            .color = {.red = 240, .green = 240, .blue = 244, .alpha = 255},
        },
        veyra::FillPath{
            .contours = holed_contours,
            .color = {.red = 30, .green = 140, .blue = 60, .alpha = 255},
            .fill_rule = veyra::SoftwarePathFillRule::even_odd,
        },
        veyra::FillPath{
            .contours = union_contours,
            .color = {.red = 40, .green = 70, .blue = 210, .alpha = 255},
            .fill_rule = veyra::SoftwarePathFillRule::non_zero,
        },
        veyra::PushTranslation{.offset = {.x = 0.0F, .y = 0.0F}},
        veyra::FillPath{
            .contours = dome_contours,
            .color = {.red = 230, .green = 120, .blue = 20, .alpha = 255},
            .fill_rule = veyra::SoftwarePathFillRule::even_odd,
        },
        veyra::PopTranslation{},
        veyra::FillPath{
            .contours = blob_contours,
            .color = {.red = 150, .green = 40, .blue = 170, .alpha = 160},
            .fill_rule = veyra::SoftwarePathFillRule::even_odd,
        },
    };

    const auto scene = std::span<const veyra::DisplayCommand>{storage.data(), storage.size()};

    render_reference(scene, expected, kWidth, kHeight, rooms);

    require(
        expected[25 * kWidth + 35].red > 100,
        "even-odd hole stays background inside the inner contour"
    );
    require(expected[20 * kWidth + 20].red < 100, "even-odd frame fills between the contours");
    require(expected[54 * kWidth + 29].red < 100, "non-zero union fills the same-winding overlap");

    auto stats = veyra::SdlGpuRenderStats{};

    require(
        veyra::render_sdl_gpu_rects(renderer, scene, actual, stats) == veyra::WindowResult::ready,
        "rect, hole, union, dome, and blob paths complete"
    );
    require(stats.submitted == 150, "paths lower to one rect per scanline span");
    require(stats.rect_draws >= 1, "spans draw through the rect pipeline");

    check_parity(renderer, scene, expected, actual, "path", 1);
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

    check_path_parity(fixture.renderer, expected, actual, rooms);

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
