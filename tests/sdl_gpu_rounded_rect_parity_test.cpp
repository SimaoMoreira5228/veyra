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

void check_rounded_parity(
    veyra::SdlGpuRenderer& renderer,
    std::span<veyra::SoftwarePixel> expected,
    std::span<veyra::SoftwarePixel> actual,
    SoftwareRooms& rooms
) {
    const std::array<veyra::DisplayCommand, 6> storage{
        veyra::FillRect{
            .bounds = {.width = static_cast<float>(kWidth), .height = static_cast<float>(kHeight)},
            .color = {.red = 240, .green = 240, .blue = 244, .alpha = 255},
        },
        veyra::FillRoundedRect{
            .bounds = {.x = 8.0F, .y = 10.0F, .width = 40.0F, .height = 30.0F},
            .radius = 8.0F,
            .color = {.red = 30, .green = 140, .blue = 60, .alpha = 255},
        },
        veyra::FillRect{
            .bounds = {.x = 24.0F, .y = 20.0F, .width = 50.0F, .height = 24.0F},
            .color = {.red = 210, .green = 60, .blue = 40, .alpha = 160},
        },
        veyra::FillRoundedRect{
            .bounds = {.x = 40.0F, .y = 32.0F, .width = 44.0F, .height = 28.0F},
            .radius = 14.0F,
            .color = {.red = 40, .green = 70, .blue = 210, .alpha = 128},
        },
        veyra::FillRoundedRect{
            .bounds = {.x = 4.0F, .y = 50.0F, .width = 60.0F, .height = 16.0F},
            .radius = 0.0F,
            .color = {.red = 230, .green = 180, .blue = 20, .alpha = 255},
        },
        veyra::FillRoundedRect{
            .bounds = {.x = 60.0F, .y = 4.0F, .width = 30.0F, .height = 20.0F},
            .radius = 40.0F,
            .color = {.red = 150, .green = 40, .blue = 170, .alpha = 255},
        },
    };

    const auto scene = std::span<const veyra::DisplayCommand>{storage.data(), storage.size()};

    render_reference(scene, expected, kWidth, kHeight, rooms);

    auto stats = veyra::SdlGpuRenderStats{};

    require(
        veyra::render_sdl_gpu_rects(renderer, scene, actual, stats) == veyra::WindowResult::ready,
        "mixed rect and rounded scene completes"
    );
    require(stats.submitted == 6, "every rect submits");
    require(stats.rect_draws >= 1, "rects draw through the rect pipeline");
    require(stats.rounded_draws >= 1, "rounded rects draw through the rounded pipeline");

    check_parity(renderer, scene, expected, actual, "rounded", 1);
}

void check_translated_rounded(
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
        veyra::PushTranslation{.offset = {.x = 3.25F, .y = -1.5F}},
        veyra::FillRoundedRect{
            .bounds = {.x = 8.0F, .y = 10.0F, .width = 40.0F, .height = 30.0F},
            .radius = 8.0F,
            .color = {.red = 30, .green = 140, .blue = 60, .alpha = 200},
        },
        veyra::PopTranslation{},
        veyra::FillRoundedRect{
            .bounds = {.x = -6.0F, .y = -4.0F, .width = 20.0F, .height = 14.0F},
            .radius = 6.0F,
            .color = {.red = 150, .green = 40, .blue = 170, .alpha = 255},
        },
    };

    const auto scene = std::span<const veyra::DisplayCommand>{storage.data(), storage.size()};

    render_reference(scene, expected, kWidth, kHeight, rooms);
    check_parity(renderer, scene, expected, actual, "rounded-translated", 1);
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

    check_rounded_parity(fixture.renderer, expected, actual, rooms);
    check_translated_rounded(fixture.renderer, expected, actual, rooms);

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
