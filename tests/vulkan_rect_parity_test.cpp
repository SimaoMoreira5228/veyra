#include "veyra/render/software_renderer.hpp"
#include "veyra/render/vulkan_context.hpp"
#include "veyra/render/vulkan_rect_pipeline.hpp"

#include <array>
#include <atomic>
#include <chrono>
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

namespace {

std::atomic<std::size_t> g_allocation_count{0};
std::atomic<bool> g_counting{false};

void note_allocation() noexcept {
    if (g_counting.load(std::memory_order_relaxed)) {
        g_allocation_count.fetch_add(1, std::memory_order_relaxed);
    }
}

}

void* operator new(std::size_t size) {
    note_allocation();

    if (size == 0) {
        size = 1;
    }

    void* result = std::malloc(size);

    if (result == nullptr) {
        throw std::bad_alloc{};
    }

    return result;
}

void operator delete(void* ptr) noexcept {
    std::free(ptr);
}

void operator delete(void* ptr, std::size_t) noexcept {
    std::free(ptr);
}

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

constexpr std::size_t kWidth = 96;
constexpr std::size_t kHeight = 72;
constexpr std::size_t kSmallWidth = 64;
constexpr std::size_t kSmallHeight = 48;
constexpr std::size_t kPixels = kWidth * kHeight;

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

struct SoftwareRooms {
    std::array<veyra::SoftwareClipRect, 1> clips{};
    std::array<veyra::LayoutPoint, 2> translations{};
};

veyra::SoftwareRenderWorkspace workspace_view(SoftwareRooms& rooms) noexcept {
    return veyra::SoftwareRenderWorkspace{
        .clips = rooms.clips,
        .translations = rooms.translations,
        .path_intersections = {},
        .path_windings = {},
        .stroke_coverage = {},
    };
}

void fill_black(std::span<veyra::SoftwarePixel> pixels) noexcept {
    const auto black = veyra::SoftwarePixel{.alpha = 255};

    for (auto& pixel : pixels) {
        pixel = black;
    }
}

using TestClock = std::chrono::steady_clock;

std::uint64_t elapsed_ns(TestClock::time_point begin, TestClock::time_point end) noexcept {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count()
    );
}

void print_device(const veyra::VulkanDeviceInfo& info) {
    const auto api = info.api_version;
    const auto major = (api >> 22U) & 0x3FFU;
    const auto minor = (api >> 12U) & 0x3FFU;
    const auto patch = api & 0xFFFU;

    std::cout << "vulkan device=" << info.device_name << '\n';
    std::cout << "vulkan vendor=0x" << std::hex << info.vendor_id << std::dec;
    std::cout << " product=0x" << std::hex << info.device_id << std::dec << '\n';
    std::cout << "vulkan api=" << major << '.' << minor << '.' << patch << '\n';
    std::cout << "vulkan driver=0x" << std::hex << info.driver_version << std::dec << '\n';
    std::cout << "vulkan discrete=" << (info.discrete ? "yes" : "no");
    std::cout << " layers=" << (info.validation_layers ? "on" : "off");
    std::cout << " icd=" << info.icd_hint << '\n';
}

void render_reference(
    std::span<const veyra::DisplayCommand> commands,
    std::span<veyra::SoftwarePixel> pixels,
    std::size_t width,
    std::size_t height,
    SoftwareRooms& rooms
) {
    fill_black(pixels);

    const auto surface = veyra::SoftwareSurface{
        .width = width,
        .height = height,
        .stride = width,
        .pixels = pixels,
    };

    const auto result = veyra::render_software(commands, surface, workspace_view(rooms));

    require(result == veyra::SoftwareRenderResult::complete, "software reference renders");
}

void check_parity(
    veyra::VulkanContext& context,
    veyra::VulkanRectPipeline& pipeline,
    std::span<const veyra::DisplayCommand> commands,
    std::span<veyra::SoftwarePixel> expected,
    std::span<veyra::SoftwarePixel> actual,
    std::string_view stage
) {
    auto stats = veyra::VulkanRectStats{};

    const auto begin = TestClock::now();
    const auto result = veyra::render_vulkan_rects(context, pipeline, commands, actual, stats);
    const auto render_ns = elapsed_ns(begin, TestClock::now());

    require(result == veyra::VulkanRenderResult::complete, "vulkan submit completes");

    std::cout << "vulkan stage=" << stage << " render_ns=" << render_ns;
    std::cout << " submitted=" << stats.submitted;
    std::cout << " skipped=" << stats.skipped_translucent << '\n';

    require(expected.size() == actual.size(), "parity compares equal extents");

    auto mismatches = std::size_t{};

    for (auto i = std::size_t{}; i < expected.size(); ++i) {
        const auto& want = expected[i];
        const auto& got = actual[i];

        if (want.red != got.red || want.green != got.green || want.blue != got.blue ||
            want.alpha != got.alpha) {
            if (mismatches < 8) {
                std::cout << "vulkan mismatch x=" << (i % context.width)
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
        std::cout << "vulkan mismatched_pixels=" << mismatches << '\n';
    }

    require(mismatches == 0, "vulkan pixels match software byte-exact");
}

void check_rejections(
    veyra::VulkanContext& context,
    veyra::VulkanRectPipeline& pipeline,
    std::span<veyra::SoftwarePixel> scratch
) {
    const std::array<veyra::DisplayCommand, 2> translucent{
        veyra::FillRect{
            .bounds = {.width = 8.0F, .height = 8.0F},
            .color = {.red = 255, .alpha = 128},
        },
        veyra::FillRect{
            .bounds = {.x = 8.0F, .width = 8.0F, .height = 8.0F},
            .color = {.red = 255, .alpha = 255},
        },
    };

    auto stats = veyra::VulkanRectStats{};

    const auto translucent_result =
        veyra::render_vulkan_rects(context, pipeline, translucent, scratch, stats);

    require(
        translucent_result == veyra::VulkanRenderResult::complete,
        "translucent frame completes"
    );
    require(stats.submitted == 1, "opaque rect still submits");
    require(stats.skipped_translucent == 1, "translucent rect is skipped and counted");

    const std::array<veyra::DisplayCommand, 1> stroked{
        veyra::StrokeRect{
            .bounds = {.width = 8.0F, .height = 8.0F},
            .thickness = 1.0F,
            .color = {.red = 255, .alpha = 255},
        },
    };

    const auto stroke_result =
        veyra::render_vulkan_rects(context, pipeline, stroked, scratch, stats);

    require(
        stroke_result == veyra::VulkanRenderResult::unsupported_commands,
        "non-rect commands are rejected, not misrendered"
    );

    const std::array<veyra::DisplayCommand, 2> clipped{
        veyra::PushClip{.bounds = {.width = 8.0F, .height = 8.0F}, .clip_id = 7},
        veyra::PopClip{},
    };

    const auto clip_result = veyra::render_vulkan_rects(context, pipeline, clipped, scratch, stats);

    require(
        clip_result == veyra::VulkanRenderResult::unsupported_commands,
        "clips are rejected, not misrendered"
    );
}

void check_steady_repeats(
    veyra::VulkanContext& context,
    veyra::VulkanRectPipeline& pipeline,
    std::span<const veyra::DisplayCommand> commands,
    std::span<veyra::SoftwarePixel> expected,
    std::span<veyra::SoftwarePixel> actual
) {
    auto stats = veyra::VulkanRectStats{};

    require(
        veyra::render_vulkan_rects(context, pipeline, commands, actual, stats) ==
            veyra::VulkanRenderResult::complete,
        "steady window opens clean"
    );

    g_allocation_count.store(0, std::memory_order_relaxed);
    g_counting.store(true, std::memory_order_relaxed);

    const auto begin = TestClock::now();

    for (auto repeat = 0; repeat < 60; ++repeat) {
        const auto result = veyra::render_vulkan_rects(context, pipeline, commands, actual, stats);

        if (result != veyra::VulkanRenderResult::complete) {
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

    // NOTE: the count below is informational. The library preallocates every
    // submit buffer, but the loader and driver may allocate behind vk calls,
    // so only pixel stability is gated, never the count.
    std::cout << "vulkan repeats=60 total_ns=" << total_ns;
    std::cout << " host_allocations=" << g_allocation_count.load(std::memory_order_relaxed) << '\n';
}

int run() {
    auto context = veyra::VulkanContext{};

    const auto context_result = veyra::create_vulkan_context(context, kWidth, kHeight);

    require(context_result == veyra::VulkanContextResult::ready, "vulkan context comes up");

    print_device(context.info);

    auto pipeline = veyra::VulkanRectPipeline{};

    const auto pipeline_result = veyra::create_vulkan_rect_pipeline(context, pipeline);

    require(pipeline_result == veyra::VulkanRenderResult::complete, "rect pipeline comes up");

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
        "full"
    );

    check_rejections(
        context,
        pipeline,
        std::span<veyra::SoftwarePixel>{actual.data(), kWidth * kHeight}
    );

    check_steady_repeats(
        context,
        pipeline,
        scene,
        std::span<veyra::SoftwarePixel>{expected.data(), kWidth * kHeight},
        std::span<veyra::SoftwarePixel>{actual.data(), kWidth * kHeight}
    );

    const auto resize_result = veyra::resize_vulkan_target(context, kSmallWidth, kSmallHeight);

    require(resize_result == veyra::VulkanContextResult::ready, "offscreen target resizes");

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
        "resized"
    );

    veyra::destroy_vulkan_rect_pipeline(context, pipeline);
    veyra::destroy_vulkan_context(context);

    require(!veyra::vulkan_context_ready(context), "context tears down");

    return 0;
}

}

void* operator new[](std::size_t size) {
    return operator new(size);
}

void operator delete[](void* ptr) noexcept {
    operator delete(ptr);
}

void operator delete[](void* ptr, std::size_t size) noexcept {
    operator delete(ptr, size);
}

int main() {
    try {
        return run();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
