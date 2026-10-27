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

void check_clip_parity(
    veyra::SdlGpuRenderer& renderer,
    std::span<veyra::SoftwarePixel> expected,
    std::span<veyra::SoftwarePixel> actual,
    SoftwareRooms& rooms
) {
    auto tile_pixels = std::array<veyra::SoftwarePixel, 4>{};

    tile_pixels[0] = {.red = 200, .green = 40, .blue = 40, .alpha = 255};
    tile_pixels[1] = {.red = 40, .green = 200, .blue = 40, .alpha = 255};
    tile_pixels[2] = {.red = 40, .green = 40, .blue = 200, .alpha = 255};
    tile_pixels[3] = {.red = 200, .green = 200, .blue = 40, .alpha = 255};

    const auto tile = veyra::SoftwareImage{
        .width = 2,
        .height = 2,
        .stride = 2,
        .pixels = tile_pixels,
        .generation = 61,
    };

    auto mask_coverage = std::array<std::uint8_t, 16>{};

    for (auto index = std::size_t{}; index < mask_coverage.size(); ++index) {
        mask_coverage[index] = static_cast<std::uint8_t>(64 + (index * 37) % 192);
    }

    const auto mask = veyra::SoftwareMask{
        .width = 4,
        .height = 4,
        .stride = 4,
        .coverage = mask_coverage,
        .generation = 62,
    };

    const std::array<veyra::SoftwarePathSegment, 3> triangle_segments{
        veyra::SoftwareLineTo{.end = {.x = 88.0F, .y = 64.0F}},
        veyra::SoftwareLineTo{.end = {.x = 56.0F, .y = 64.0F}},
        veyra::SoftwareLineTo{.end = {.x = 72.0F, .y = 48.0F}},
    };
    const std::array<veyra::SoftwarePathContour, 1> triangle_contours{
        veyra::SoftwarePathContour{
            .start = {.x = 72.0F, .y = 48.0F},
            .segments = triangle_segments,
        },
    };

    const std::array<veyra::DisplayCommand, 28> storage{
        veyra::FillRect{
            .bounds = {.width = static_cast<float>(kWidth), .height = static_cast<float>(kHeight)},
            .color = {.red = 240, .green = 240, .blue = 244, .alpha = 255},
        },
        veyra::PushClip{.bounds = {.x = 16.0F, .y = 16.0F, .width = 48.0F, .height = 32.0F}},
        veyra::FillRect{
            .bounds = {.x = 8.0F, .y = 8.0F, .width = 80.0F, .height = 56.0F},
            .color = {.red = 200, .green = 40, .blue = 40, .alpha = 255},
        },
        veyra::PushClip{.bounds = {.x = 24.0F, .y = 24.0F, .width = 32.0F, .height = 16.0F}},
        veyra::FillRect{
            .bounds = {.width = static_cast<float>(kWidth), .height = static_cast<float>(kHeight)},
            .color = {.red = 40, .green = 70, .blue = 210, .alpha = 255},
        },
        veyra::PopClip{},
        veyra::PopClip{},
        veyra::PushTranslation{.offset = {.x = 4.0F, .y = 4.0F}},
        veyra::PushClip{.bounds = {.x = 60.0F, .y = 40.0F, .width = 20.0F, .height = 20.0F}},
        veyra::FillRect{
            .bounds = {.width = static_cast<float>(kWidth), .height = static_cast<float>(kHeight)},
            .color = {.red = 30, .green = 140, .blue = 60, .alpha = 255},
        },
        veyra::PopClip{},
        veyra::PopTranslation{},
        veyra::PushClip{.bounds = {.x = 60.0F, .y = 12.0F, .width = 24.0F, .height = 16.0F}},
        veyra::FillRoundedRect{
            .bounds = {.x = 56.0F, .y = 8.0F, .width = 32.0F, .height = 24.0F},
            .radius = 6.0F,
            .color = {.red = 230, .green = 120, .blue = 20, .alpha = 255},
        },
        veyra::PopClip{},
        veyra::PushClip{.bounds = {.x = 32.0F, .y = 52.0F, .width = 32.0F, .height = 16.0F}},
        veyra::StrokeLine{
            .start = {.x = 0.0F, .y = 60.0F},
            .end = {.x = 96.0F, .y = 60.0F},
            .thickness = 3.0F,
            .color = {.red = 20, .green = 20, .blue = 24, .alpha = 255},
        },
        veyra::StrokeRoundedRect{
            .bounds = {.x = 28.0F, .y = 50.0F, .width = 40.0F, .height = 20.0F},
            .radius = 5.0F,
            .thickness = 2.0F,
            .color = {.red = 230, .green = 180, .blue = 20, .alpha = 255},
        },
        veyra::FillPath{
            .contours = triangle_contours,
            .color = {.red = 150, .green = 40, .blue = 170, .alpha = 255},
            .fill_rule = veyra::SoftwarePathFillRule::even_odd,
        },
        veyra::DrawMask{
            .x = 32.0F,
            .y = 52.0F,
            .mask = &mask,
            .color = {.red = 230, .green = 232, .blue = 240, .alpha = 255},
        },
        veyra::PopClip{},
        veyra::PushClip{.bounds = {.x = 64.0F, .y = 48.0F, .width = 16.0F, .height = 8.0F}},
        veyra::DrawImage{
            .source = {.width = 2.0F, .height = 2.0F},
            .destination = {.x = 64.0F, .y = 44.0F, .width = 16.0F, .height = 16.0F},
            .image = &tile,
            .sampling = veyra::SoftwareImageSampling::nearest,
            .opacity = 255,
        },
        veyra::PopClip{},
        veyra::PushClip{.bounds = {.x = 80.0F, .y = 60.0F, .width = 8.0F, .height = 4.0F}},
        veyra::FillRect{
            .bounds = {.x = 0.0F, .y = 0.0F, .width = 8.0F, .height = 8.0F},
            .color = {.red = 200, .green = 40, .blue = 40, .alpha = 255},
        },
        veyra::PopClip{},
    };

    const auto scene = std::span<const veyra::DisplayCommand>{storage.data(), storage.size()};

    render_reference(scene, expected, kWidth, kHeight, rooms);

    require(expected[20 * kWidth + 20].green < 100, "outer clip keeps the red block inside");
    require(expected[20 * kWidth + 70].green > 100, "outer clip cuts the red block outside");

    auto stats = veyra::SdlGpuRenderStats{};

    require(
        veyra::render_sdl_gpu_rects(renderer, scene, actual, stats) == veyra::WindowResult::ready,
        "nested clips across every pipe complete"
    );

    check_parity(renderer, scene, expected, actual, "clip", 2);
}

void check_unbalanced_pop(
    veyra::SdlGpuRenderer& renderer,
    std::span<veyra::SoftwarePixel> scratch
) {
    auto stats = veyra::SdlGpuRenderStats{};

    const std::array<veyra::DisplayCommand, 1> storage{
        veyra::PopClip{},
    };

    require(
        veyra::render_sdl_gpu_rects(
            renderer,
            std::span<const veyra::DisplayCommand>(storage),
            scratch,
            stats
        ) == veyra::WindowResult::invalid_input,
        "an unbalanced pop is rejected"
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

    check_clip_parity(fixture.renderer, expected, actual, rooms);
    check_unbalanced_pop(fixture.renderer, actual);

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
