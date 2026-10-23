#include "veyra/render/sdl_gpu_renderer.hpp"
#include "veyra/render/software_renderer.hpp"

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

void check_mask_parity(
    veyra::SdlGpuRenderer& renderer,
    std::span<veyra::SoftwarePixel> expected,
    std::span<veyra::SoftwarePixel> actual,
    SoftwareRooms& rooms
) {
    auto latin_coverage = std::array<std::uint8_t, 72>{};

    for (auto index = std::size_t{}; index < latin_coverage.size(); ++index) {
        latin_coverage[index] = static_cast<std::uint8_t>(96 + (index * 37) % 160);
    }

    auto cjk_coverage = std::array<std::uint8_t, 120>{};

    for (auto index = std::size_t{}; index < cjk_coverage.size(); ++index) {
        cjk_coverage[index] = static_cast<std::uint8_t>(128 + (index * 53) % 128);
    }

    const auto latin = veyra::SoftwareMask{
        .width = 6,
        .height = 12,
        .stride = 6,
        .coverage = latin_coverage,
        .generation = 71,
    };

    const auto cjk = veyra::SoftwareMask{
        .width = 10,
        .height = 12,
        .stride = 10,
        .coverage = cjk_coverage,
        .generation = 72,
    };

    const std::array<veyra::DisplayCommand, 6> storage{
        veyra::FillRect{
            .bounds = {.width = static_cast<float>(kWidth), .height = static_cast<float>(kHeight)},
            .color = {.red = 240, .green = 240, .blue = 244, .alpha = 255},
        },
        veyra::DrawMask{
            .x = 8.0F,
            .y = 10.0F,
            .mask = &latin,
            .color = {.red = 30, .green = 31, .blue = 38, .alpha = 255},
        },
        veyra::DrawMask{
            .x = 20.5F,
            .y = 12.0F,
            .mask = &cjk,
            .color = {.red = 120, .green = 40, .blue = 30, .alpha = 180},
        },
        veyra::FillRect{
            .bounds = {.x = 30.0F, .y = 26.0F, .width = 40.0F, .height = 20.0F},
            .color = {.red = 210, .green = 60, .blue = 40, .alpha = 160},
        },
        veyra::DrawMask{
            .x = 60.0F,
            .y = 50.0F,
            .mask = &latin,
            .color = {.red = 20, .green = 110, .blue = 230, .alpha = 0},
        },
        veyra::DrawMask{
            .x = 200.0F,
            .y = 10.0F,
            .mask = &latin,
            .color = {.red = 20, .green = 110, .blue = 230, .alpha = 255},
        },
    };

    const auto scene = std::span<const veyra::DisplayCommand>{storage.data(), storage.size()};

    render_reference(scene, expected, kWidth, kHeight, rooms);

    auto stats = veyra::SdlGpuRenderStats{};

    require(
        veyra::render_sdl_gpu_rects(renderer, scene, actual, stats) == veyra::WindowResult::ready,
        "mixed rect and mask scene completes"
    );
    require(stats.submitted == 4, "alpha-zero and offscreen masks never submit");
    require(stats.mask_draws >= 1, "masks draw through the mask pipeline");

    check_parity(renderer, scene, expected, actual, "mask", 1);
}

void check_mask_edge_parity(
    veyra::SdlGpuRenderer& renderer,
    std::span<veyra::SoftwarePixel> expected,
    std::span<veyra::SoftwarePixel> actual,
    SoftwareRooms& rooms
) {
    auto coverage = std::array<std::uint8_t, 96>{};

    for (auto index = std::size_t{}; index < coverage.size(); ++index) {
        coverage[index] = static_cast<std::uint8_t>(64 + (index * 91) % 192);
    }

    const auto tile = veyra::SoftwareMask{
        .width = 8,
        .height = 12,
        .stride = 8,
        .coverage = coverage,
        .generation = 75,
    };

    const std::array<veyra::DisplayCommand, 4> storage{
        veyra::FillRect{
            .bounds = {.width = static_cast<float>(kWidth), .height = static_cast<float>(kHeight)},
            .color = {.red = 240, .green = 240, .blue = 244, .alpha = 255},
        },
        veyra::DrawMask{
            .x = -3.5F,
            .y = 20.0F,
            .mask = &tile,
            .color = {.red = 30, .green = 31, .blue = 38, .alpha = 255},
        },
        veyra::DrawMask{
            .x = 90.0F,
            .y = 64.5F,
            .mask = &tile,
            .color = {.red = 120, .green = 40, .blue = 30, .alpha = 200},
        },
        veyra::DrawMask{
            .x = 40.25F,
            .y = 40.75F,
            .mask = &tile,
            .color = {.red = 20, .green = 110, .blue = 230, .alpha = 128},
        },
    };

    const auto scene = std::span<const veyra::DisplayCommand>{storage.data(), storage.size()};

    render_reference(scene, expected, kWidth, kHeight, rooms);

    auto stats = veyra::SdlGpuRenderStats{};

    require(
        veyra::render_sdl_gpu_rects(renderer, scene, actual, stats) == veyra::WindowResult::ready,
        "negative and edge-clipped masks complete"
    );

    check_parity(renderer, scene, expected, actual, "mask-edge", 1);
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

    check_mask_parity(fixture.renderer, expected, actual, rooms);
    check_mask_edge_parity(fixture.renderer, expected, actual, rooms);

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
