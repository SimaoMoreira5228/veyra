#include "allocation_counter.hpp"
#include "veyra/render/noga_rect.hpp"
#include "veyra/render/software_renderer.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
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

using TestClock = std::chrono::steady_clock;

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

std::uint64_t elapsed_ns(TestClock::time_point begin, TestClock::time_point end) noexcept {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count()
    );
}

struct SoftwareRooms {
    std::array<veyra::SoftwareClipRect, 2> clips{};
    std::array<veyra::LayoutPoint, 2> translations{};
    std::array<double, 512> path_intersections{};
    std::array<std::int8_t, 512> path_windings{};
};

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

void render_reference(
    std::span<const veyra::DisplayCommand> commands,
    std::span<veyra::SoftwarePixel> pixels,
    std::size_t width,
    std::size_t height,
    SoftwareRooms& rooms
) {
    const auto black = veyra::SoftwarePixel{.alpha = 255};

    for (auto& pixel : pixels) {
        pixel = black;
    }

    const auto surface = veyra::SoftwareSurface{
        .width = width,
        .height = height,
        .stride = width,
        .pixels = pixels,
    };

    const auto workspace = veyra::SoftwareRenderWorkspace{
        .clips = rooms.clips,
        .translations = rooms.translations,
        .path_intersections = rooms.path_intersections,
        .path_windings = rooms.path_windings,
    };

    const auto result = veyra::render_software(commands, surface, workspace);

    require(result == veyra::SoftwareRenderResult::complete, "software reference renders");
}

void print_device(const veyra::NogaDeviceInfo& info) {
    std::cout << "noga device=" << info.device_name << '\n';
    std::cout << "noga indirect_mesh_draw=" << (info.indirect_mesh_draw ? "yes" : "no") << '\n';
    std::cout << "noga timestamp_period_ns=" << info.timestamp_period_ns << '\n';
    std::cout << "noga queue_count=" << info.queue_count << '\n';
}

void check_parity(
    veyra::NogaContext& context,
    veyra::NogaRectPipeline& pipeline,
    std::span<const veyra::DisplayCommand> commands,
    std::span<veyra::SoftwarePixel> expected,
    std::span<veyra::SoftwarePixel> actual,
    std::string_view stage,
    std::uint64_t tolerance
) {
    auto stats = veyra::NogaRectStats{};

    const auto begin = TestClock::now();
    const auto result = veyra::render_noga_rects(context, pipeline, commands, actual, stats);
    const auto render_ns = elapsed_ns(begin, TestClock::now());

    require(result == veyra::NogaRenderResult::complete, "noga submit completes");
    require(expected.size() == actual.size(), "parity compares equal extents");

    auto mismatches = std::size_t{};
    auto max_error = 0;

    const auto within_tolerance = [&max_error, tolerance](std::uint8_t want, std::uint8_t got) {
        const auto difference = static_cast<int>(want) - static_cast<int>(got);

        max_error = std::max(max_error, difference >= 0 ? difference : -difference);

        return difference >= -static_cast<int>(tolerance) &&
            difference <= static_cast<int>(tolerance);
    };

    for (auto i = std::size_t{}; i < expected.size(); ++i) {
        const auto& want = expected[i];
        const auto& got = actual[i];

        if (!within_tolerance(want.red, got.red) || !within_tolerance(want.green, got.green) ||
            !within_tolerance(want.blue, got.blue) || !within_tolerance(want.alpha, got.alpha)) {
            if (mismatches < 8) {
                std::cout << "noga mismatch x=" << (i % context.width)
                          << " y=" << (i / context.width);
                std::cout << " want=" << static_cast<int>(want.red) << ','
                          << static_cast<int>(want.green) << ',' << static_cast<int>(want.blue)
                          << ',' << static_cast<int>(want.alpha);
                std::cout << " got=" << static_cast<int>(got.red) << ','
                          << static_cast<int>(got.green) << ',' << static_cast<int>(got.blue) << ','
                          << static_cast<int>(got.alpha) << '\n';
            }

            ++mismatches;
        }
    }

    if (mismatches != 0) {
        std::cout << "noga mismatched_pixels=" << mismatches << '\n';
    }

    require(mismatches == 0, "noga pixels match software within tolerance");

    std::cout << "noga stage=" << stage << " render_ns=" << render_ns;
    std::cout << " submitted=" << stats.submitted << " draws=" << stats.draws;
    std::cout << " max_abs_error=" << max_error << '\n';
}

void check_steady_repeats(
    veyra::NogaContext& context,
    veyra::NogaRectPipeline& pipeline,
    std::span<const veyra::DisplayCommand> commands,
    std::span<veyra::SoftwarePixel> expected,
    std::span<veyra::SoftwarePixel> actual
) {
    auto stats = veyra::NogaRectStats{};

    require(
        veyra::render_noga_rects(context, pipeline, commands, actual, stats) ==
            veyra::NogaRenderResult::complete,
        "steady window opens clean"
    );

    g_allocation_count.store(0, std::memory_order_relaxed);
    g_counting.store(true, std::memory_order_relaxed);

    const auto begin = TestClock::now();

    for (auto repeat = 0; repeat < 60; ++repeat) {
        const auto result = veyra::render_noga_rects(context, pipeline, commands, actual, stats);

        if (result != veyra::NogaRenderResult::complete) {
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

    // NOTE: the count below is informational. The pilot preallocates every
    // frame heap, but the loader and driver may allocate behind the API, so
    // only pixel stability is gated, never the count.
    std::cout << "noga repeats=60 total_ns=" << total_ns;
    std::cout << " host_allocations=" << g_allocation_count.load(std::memory_order_relaxed) << '\n';
}

void check_translucent_tiles(
    veyra::NogaContext& context,
    veyra::NogaRectPipeline& pipeline,
    std::span<veyra::SoftwarePixel> expected,
    std::span<veyra::SoftwarePixel> actual,
    SoftwareRooms& rooms
) {
    // NOTE: tiles are disjoint, so the result cannot depend on raster order.
    // Overlapping translucent rects are left out on purpose: mesh workgroups
    // have no primitive-order guarantee, unlike ordered classic draws.
    constexpr auto columns = std::size_t{8};
    constexpr auto rows = std::size_t{6};
    constexpr auto count = columns * rows;

    auto storage = std::array<veyra::DisplayCommand, count + 1>{};

    storage[0] = veyra::FillRect{
        .bounds = {.width = static_cast<float>(kWidth), .height = static_cast<float>(kHeight)},
        .color = {.red = 12, .green = 20, .blue = 28, .alpha = 255},
    };

    for (auto index = std::size_t{}; index < count; ++index) {
        const auto opaque = index % 2U == 0U;

        storage[index + 1] = veyra::FillRect{
            .bounds =
                {
                    .x = static_cast<float>((index % columns) * 12),
                    .y = static_cast<float>((index / columns) * 12),
                    .width = 10.0F,
                    .height = 10.0F,
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
    check_parity(context, pipeline, scene, expected, actual, "translucent", 1);
}

void check_capacity_limit(
    veyra::NogaContext& context,
    veyra::NogaRectPipeline& pipeline,
    std::span<veyra::SoftwarePixel> scratch
) {
    auto storage = std::vector<veyra::DisplayCommand>(veyra::kNogaMaxRects + 1);

    for (auto& slot : storage) {
        slot = veyra::FillRect{
            .bounds = {.width = 8.0F, .height = 8.0F},
            .color = {.red = 200, .alpha = 255},
        };
    }

    auto stats = veyra::NogaRectStats{};

    require(
        veyra::render_noga_rects(
            context,
            pipeline,
            std::span<const veyra::DisplayCommand>(storage),
            scratch,
            stats
        ) == veyra::NogaRenderResult::insufficient_capacity,
        "frames beyond the instance budget are rejected, not clipped"
    );

    auto scene_storage = std::array<veyra::DisplayCommand, kSceneCommands>{};
    const auto scene = build_scene(scene_storage);

    require(
        veyra::render_noga_rects(context, pipeline, scene, scratch, stats) ==
            veyra::NogaRenderResult::complete,
        "the pipeline stays usable after a rejection"
    );
    require(stats.submitted == 6, "recovery submits the visible scene rects");
    require(stats.draws == 1, "recovery draws in a single batch");
}

void check_unsupported_kind(
    veyra::NogaContext& context,
    veyra::NogaRectPipeline& pipeline,
    std::span<veyra::SoftwarePixel> scratch
) {
    auto storage = std::array<veyra::DisplayCommand, 1>{};

    storage[0] = veyra::FillRoundedRect{
        .bounds = {.width = 8.0F, .height = 8.0F},
        .radius = 2.0F,
        .color = {.red = 200, .alpha = 255},
    };

    auto stats = veyra::NogaRectStats{};

    require(
        veyra::render_noga_rects(
            context,
            pipeline,
            std::span<const veyra::DisplayCommand>(storage),
            scratch,
            stats
        ) == veyra::NogaRenderResult::unsupported_commands,
        "non-rect kinds report instead of rendering"
    );
}

int run() {
    auto context = veyra::NogaContext{};

    const auto context_result = veyra::create_noga_context(context, kWidth, kHeight);

    require(context_result == veyra::NogaContextResult::ready, "noga context comes up");

    print_device(context.info);

    auto pipeline = veyra::NogaRectPipeline{};

    const auto pipeline_result = veyra::create_noga_rect_pipeline(context, pipeline);

    require(pipeline_result == veyra::NogaRenderResult::complete, "rect pipeline comes up");

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
        context,
        pipeline,
        scene,
        std::span<veyra::SoftwarePixel>{expected.data(), kWidth * kHeight},
        std::span<veyra::SoftwarePixel>{actual.data(), kWidth * kHeight},
        "full",
        0
    );

    check_steady_repeats(
        context,
        pipeline,
        scene,
        std::span<veyra::SoftwarePixel>{expected.data(), kWidth * kHeight},
        std::span<veyra::SoftwarePixel>{actual.data(), kWidth * kHeight}
    );

    check_capacity_limit(
        context,
        pipeline,
        std::span<veyra::SoftwarePixel>{actual.data(), kWidth * kHeight}
    );

    check_translucent_tiles(
        context,
        pipeline,
        std::span<veyra::SoftwarePixel>{expected.data(), kWidth * kHeight},
        std::span<veyra::SoftwarePixel>{actual.data(), kWidth * kHeight},
        rooms
    );

    check_unsupported_kind(
        context,
        pipeline,
        std::span<veyra::SoftwarePixel>{actual.data(), kWidth * kHeight}
    );

    const auto resize_result = veyra::resize_noga_target(context, kSmallWidth, kSmallHeight);

    require(resize_result == veyra::NogaContextResult::ready, "offscreen target resizes");

    render_reference(
        scene,
        std::span<veyra::SoftwarePixel>{expected.data(), kSmallWidth * kSmallHeight},
        kSmallWidth,
        kSmallHeight,
        rooms
    );

    check_parity(
        context,
        pipeline,
        scene,
        std::span<veyra::SoftwarePixel>{expected.data(), kSmallWidth * kSmallHeight},
        std::span<veyra::SoftwarePixel>{actual.data(), kSmallWidth * kSmallHeight},
        "resized",
        0
    );

    veyra::destroy_noga_rect_pipeline(context, pipeline);
    veyra::destroy_noga_context(context);

    require(!veyra::noga_context_ready(context), "context tears down");

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
