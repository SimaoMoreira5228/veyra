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

void check_image_parity(
    veyra::SdlGpuRenderer& renderer,
    std::span<veyra::SoftwarePixel> expected,
    std::span<veyra::SoftwarePixel> actual,
    SoftwareRooms& rooms
) {
    auto nearest_pixels = std::array<veyra::SoftwarePixel, 24>{};

    for (auto index = std::size_t{}; index < nearest_pixels.size(); ++index) {
        nearest_pixels[index] = veyra::SoftwarePixel{
            .red = static_cast<std::uint8_t>((index * 37) % 256),
            .green = static_cast<std::uint8_t>((index * 91) % 256),
            .blue = static_cast<std::uint8_t>((index * 53) % 256),
            .alpha = 255,
        };
    }

    auto linear_pixels = std::array<veyra::SoftwarePixel, 24>{};

    for (auto index = std::size_t{}; index < linear_pixels.size(); ++index) {
        linear_pixels[index] = veyra::SoftwarePixel{
            .red = static_cast<std::uint8_t>(200 - (index * 17) % 200),
            .green = static_cast<std::uint8_t>(40 + (index * 31) % 180),
            .blue = static_cast<std::uint8_t>(90 + (index * 13) % 120),
            .alpha = 255,
        };
    }

    const auto nearest_tile = veyra::SoftwareImage{
        .width = 6,
        .height = 4,
        .stride = 6,
        .pixels = nearest_pixels,
        .generation = 11,
    };

    const auto linear_tile = veyra::SoftwareImage{
        .width = 6,
        .height = 4,
        .stride = 6,
        .pixels = linear_pixels,
        .generation = 12,
    };

    const std::array<veyra::DisplayCommand, 6> storage{
        veyra::FillRect{
            .bounds = {.width = static_cast<float>(kWidth), .height = static_cast<float>(kHeight)},
            .color = {.red = 240, .green = 240, .blue = 244, .alpha = 255},
        },
        veyra::DrawImage{
            .source = {.width = 6.0F, .height = 4.0F},
            .destination = {.x = 8.0F, .y = 10.0F, .width = 12.0F, .height = 8.0F},
            .image = &nearest_tile,
            .sampling = veyra::SoftwareImageSampling::nearest,
            .opacity = 255,
        },
        veyra::DrawImage{
            .source = {.width = 6.0F, .height = 4.0F},
            .destination = {.x = 14.0F, .y = 14.0F, .width = 24.0F, .height = 16.0F},
            .image = &linear_tile,
            .sampling = veyra::SoftwareImageSampling::linear,
            .opacity = 128,
        },
        veyra::FillRect{
            .bounds = {.x = 30.0F, .y = 26.0F, .width = 40.0F, .height = 20.0F},
            .color = {.red = 210, .green = 60, .blue = 40, .alpha = 160},
        },
        veyra::DrawImage{
            .source = {.width = 2.0F, .height = 2.0F},
            .destination = {.x = 60.0F, .y = 50.0F, .width = 8.0F, .height = 8.0F},
            .image = &nearest_tile,
            .sampling = veyra::SoftwareImageSampling::nearest,
            .opacity = 0,
        },
        veyra::DrawImage{
            .source = {.width = 6.0F, .height = 4.0F},
            .destination = {.x = 200.0F, .y = 10.0F, .width = 6.0F, .height = 4.0F},
            .image = &nearest_tile,
            .sampling = veyra::SoftwareImageSampling::nearest,
            .opacity = 255,
        },
    };

    const auto scene = std::span<const veyra::DisplayCommand>{storage.data(), storage.size()};

    render_reference(scene, expected, kWidth, kHeight, rooms);

    auto stats = veyra::SdlGpuRenderStats{};

    require(
        veyra::render_sdl_gpu_rects(renderer, scene, actual, stats) == veyra::WindowResult::ready,
        "mixed rect and image scene completes"
    );
    require(stats.submitted == 4, "alpha-zero and offscreen draws never submit");
    require(stats.image_draws >= 1, "images draw through the image pipeline");
    require(stats.uploads == 2, "both tiles upload once");

    check_parity(renderer, scene, expected, actual, "image", 1);

    require(
        veyra::render_sdl_gpu_rects(renderer, scene, actual, stats) == veyra::WindowResult::ready,
        "a repeat frame completes"
    );
    require(stats.uploads == 0, "cached tiles never re-upload");

    check_parity(renderer, scene, expected, actual, "image-cached", 1);
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

    check_image_parity(fixture.renderer, expected, actual, rooms);

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
