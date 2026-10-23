#include "allocation_counter.hpp"
#include "veyra/render/sdl_gpu_renderer.hpp"
#include "veyra/render/software_renderer.hpp"

#include "sdl_gpu_parity_helpers.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <new>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr std::size_t kWidth = 96;
constexpr std::size_t kHeight = 72;
constexpr std::size_t kSmallWidth = 64;
constexpr std::size_t kSmallHeight = 48;
constexpr std::size_t kPixels = kWidth * kHeight;

using veyra::sdl_gpu_parity::check_parity;
using veyra::sdl_gpu_parity::elapsed_ns;
using veyra::sdl_gpu_parity::render_reference;
using veyra::sdl_gpu_parity::require;
using veyra::sdl_gpu_parity::SdlGpuFixture;
using veyra::sdl_gpu_parity::SoftwareRooms;
using veyra::sdl_gpu_parity::TestClock;

constexpr std::size_t kSceneCommands = 9;

std::span<const veyra::DisplayCommand>
build_scene(std::array<veyra::DisplayCommand, kSceneCommands>& storage) noexcept {
    storage[0] = veyra::FillRect{
        .bounds = {.width = static_cast<float>(kWidth), .height = static_cast<float>(kHeight)},
        .color = {.red = 12, .green = 20, .blue = 28, .alpha = 255},
    };
    storage[1] = veyra::FillRect{
        .bounds = {.y = 10.0F, .width = static_cast<float>(kWidth), .height = 18.0F},
        .color = {.red = 200, .green = 40, .blue = 30, .alpha = 255},
    };
    storage[2] = veyra::FillRect{
        .bounds = {.x = 8.0F, .y = 20.0F, .width = 40.0F, .height = 30.0F},
        .color = {.red = 30, .green = 140, .blue = 60, .alpha = 255},
    };
    storage[3] = veyra::FillRect{
        .bounds = {.x = 30.0F, .y = 32.0F, .width = 50.0F, .height = 24.0F},
        .color = {.red = 40, .green = 70, .blue = 210, .alpha = 255},
    };
    storage[4] = veyra::PushTranslation{.offset = {.x = 1.5F, .y = 2.25F}};
    storage[5] = veyra::FillRect{
        .bounds = {.x = 10.25F, .y = 44.5F, .width = 22.75F, .height = 9.5F},
        .color = {.red = 230, .green = 180, .blue = 20, .alpha = 255},
    };
    storage[6] = veyra::PopTranslation{};
    storage[7] = veyra::FillRect{
        .bounds = {.x = -12.0F, .y = 56.0F, .width = 200.0F, .height = 40.0F},
        .color = {.red = 150, .green = 40, .blue = 170, .alpha = 255},
    };
    storage[8] = veyra::FillRect{
        .bounds = {.x = 50.0F, .y = 8.0F, .width = 0.0F, .height = 6.0F},
        .color = {.red = 255, .green = 255, .blue = 255, .alpha = 255},
    };

    return std::span<const veyra::DisplayCommand>{storage.data(), storage.size()};
}

void check_chunked_translucent(
    veyra::SdlGpuRenderer& renderer,
    std::span<veyra::SoftwarePixel> expected,
    std::span<veyra::SoftwarePixel> actual,
    SoftwareRooms& rooms
) {
    constexpr auto chunk_commands = std::size_t{300};

    auto storage = std::array<veyra::DisplayCommand, chunk_commands>{};

    for (auto index = std::size_t{}; index < chunk_commands; ++index) {
        const auto opaque = index % 2U == 0U;

        storage[index] = veyra::FillRect{
            .bounds =
                {
                    .x = static_cast<float>((index * 7) % kWidth),
                    .y = static_cast<float>((index * 11) % kHeight),
                    .width = 24.0F,
                    .height = 18.0F,
                },
            .color = {
                .red = static_cast<std::uint8_t>((index * 37) % 256),
                .green = static_cast<std::uint8_t>((index * 91) % 256),
                .blue = static_cast<std::uint8_t>((index * 53) % 256),
                .alpha = static_cast<std::uint8_t>(opaque ? 255 : 128),
            },
        };
    }

    const auto scene = std::span<const veyra::DisplayCommand>{storage.data(), storage.size()};

    render_reference(scene, expected, kWidth, kHeight, rooms);

    auto stats = veyra::SdlGpuRenderStats{};

    require(
        veyra::render_sdl_gpu_rects(renderer, scene, actual, stats) == veyra::WindowResult::ready,
        "oversized translucent scene completes"
    );
    require(stats.submitted == chunk_commands, "every rect submits without chunking");
    require(stats.draws == 1, "one pipe draws in a single run");

    check_parity(renderer, scene, expected, actual, "chunked", 1);
}

void check_capacity_limit(
    veyra::SdlGpuRenderer& renderer,
    std::span<veyra::SoftwarePixel> scratch
) {
    auto storage =
        std::vector<veyra::DisplayCommand>(veyra::kSdlGpuMaxRects * veyra::kSdlGpuMaxDraws + 1);

    for (auto& slot : storage) {
        slot = veyra::FillRect{
            .bounds = {.width = 8.0F, .height = 8.0F},
            .color = {.red = 200, .alpha = 255},
        };
    }

    auto stats = veyra::SdlGpuRenderStats{};

    require(
        veyra::render_sdl_gpu_rects(
            renderer,
            std::span<const veyra::DisplayCommand>(storage),
            scratch,
            stats
        ) == veyra::WindowResult::insufficient_capacity,
        "frames beyond the instance budget are rejected, not clipped"
    );

    auto scene_storage = std::array<veyra::DisplayCommand, kSceneCommands>{};
    const auto scene = build_scene(scene_storage);

    require(
        veyra::render_sdl_gpu_rects(renderer, scene, scratch, stats) == veyra::WindowResult::ready,
        "the pipeline stays usable after a rejection"
    );
    require(stats.submitted == 6, "recovery submits the visible scene rects");
    require(stats.draws == 1, "recovery draws in a single batch");
}

void check_steady_repeats(
    veyra::SdlGpuRenderer& renderer,
    std::span<const veyra::DisplayCommand> commands,
    std::span<veyra::SoftwarePixel> expected,
    std::span<veyra::SoftwarePixel> actual
) {
    auto stats = veyra::SdlGpuRenderStats{};

    require(
        veyra::render_sdl_gpu_rects(renderer, commands, actual, stats) ==
            veyra::WindowResult::ready,
        "steady window opens clean"
    );

    g_allocation_count.store(0, std::memory_order_relaxed);
    g_counting.store(true, std::memory_order_relaxed);

    const auto begin = TestClock::now();

    for (auto repeat = 0; repeat < 60; ++repeat) {
        const auto result = veyra::render_sdl_gpu_rects(renderer, commands, actual, stats);

        if (result != veyra::WindowResult::ready) {
            g_counting.store(false, std::memory_order_relaxed);

            throw std::runtime_error("steady repeats stay complete");
        }

        if (std::memcmp(
                expected.data(),
                actual.data(),
                expected.size() * sizeof(veyra::SoftwarePixel)
            ) != 0) {
            g_counting.store(false, std::memory_order_relaxed);

            throw std::runtime_error("steady repeats stay pixel-identical");
        }
    }

    const auto total_ns = elapsed_ns(begin, TestClock::now());

    g_counting.store(false, std::memory_order_relaxed);

    std::cout << "sdl-gpu repeats=60 total_ns=" << total_ns;
    std::cout << " host_allocations=" << g_allocation_count.load(std::memory_order_relaxed) << '\n';
}

int run() {
    auto fixture = SdlGpuFixture{};

    if (!fixture.setup(kWidth, kHeight)) {
        std::cout << "sdl-gpu device or window unavailable, skipping\n";

        return 77;
    }

    std::cout << "sdl-gpu driver=" << fixture.presenter.device_driver() << '\n';

    auto scene_storage = std::array<veyra::DisplayCommand, kSceneCommands>{};
    const auto scene = build_scene(scene_storage);

    auto rooms = SoftwareRooms{};
    auto expected = std::array<veyra::SoftwarePixel, kPixels>{};
    auto actual = std::array<veyra::SoftwarePixel, kPixels>{};

    {
        const auto begin = TestClock::now();

        render_reference(
            scene,
            std::span<veyra::SoftwarePixel>{expected.data(), kWidth * kHeight},
            kWidth,
            kHeight,
            rooms
        );

        std::cout << "software render_ns=" << elapsed_ns(begin, TestClock::now()) << '\n';
    }

    check_parity(
        fixture.renderer,
        scene,
        std::span<veyra::SoftwarePixel>{expected.data(), kWidth * kHeight},
        std::span<veyra::SoftwarePixel>{actual.data(), kWidth * kHeight},
        "full",
        0
    );

    check_steady_repeats(
        fixture.renderer,
        scene,
        std::span<veyra::SoftwarePixel>{expected.data(), kWidth * kHeight},
        std::span<veyra::SoftwarePixel>{actual.data(), kWidth * kHeight}
    );

    check_chunked_translucent(
        fixture.renderer,
        std::span<veyra::SoftwarePixel>{expected.data(), kWidth * kHeight},
        std::span<veyra::SoftwarePixel>{actual.data(), kWidth * kHeight},
        rooms
    );

    check_capacity_limit(
        fixture.renderer,
        std::span<veyra::SoftwarePixel>{actual.data(), kWidth * kHeight}
    );

    require(
        veyra::resize_sdl_gpu_renderer(fixture.renderer, kSmallWidth, kSmallHeight) ==
            veyra::WindowResult::ready,
        "offscreen target resizes"
    );

    render_reference(
        scene,
        std::span<veyra::SoftwarePixel>{expected.data(), kSmallWidth * kSmallHeight},
        kSmallWidth,
        kSmallHeight,
        rooms
    );

    check_parity(
        fixture.renderer,
        scene,
        std::span<veyra::SoftwarePixel>{expected.data(), kSmallWidth * kSmallHeight},
        std::span<veyra::SoftwarePixel>{actual.data(), kSmallWidth * kSmallHeight},
        "resized",
        0
    );

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
