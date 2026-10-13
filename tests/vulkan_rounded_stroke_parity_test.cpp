#include "veyra/render/software_renderer.hpp"
#include "veyra/render/vulkan_context.hpp"
#include "veyra/render/vulkan_rect_pipeline.hpp"

#include "vulkan_parity_helpers.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
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

void* operator new[](std::size_t size) {
    return operator new(size);
}

void operator delete[](void* ptr) noexcept {
    operator delete(ptr);
}

void operator delete[](void* ptr, std::size_t size) noexcept {
    operator delete(ptr, size);
}

namespace {

using veyra::vulkan_parity::check_parity;
using veyra::vulkan_parity::render_reference;
using veyra::vulkan_parity::require;
using veyra::vulkan_parity::SoftwareRooms;

constexpr std::size_t kWidth = 96;
constexpr std::size_t kHeight = 72;

void check_band_parity(
    veyra::VulkanContext& context,
    veyra::VulkanRectPipeline& pipeline,
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

    auto stats = veyra::VulkanRectStats{};

    require(
        veyra::render_vulkan_rects(context, pipeline, scene, actual, stats) ==
            veyra::VulkanRenderResult::complete,
        "bands, a lowered fill, and two skips complete"
    );
    require(stats.submitted == 5, "zero-width and zero-size bands never submit");
    require(stats.rounded_stroke_draws >= 1, "bands draw through the band pipeline");
    require(stats.rounded_draws >= 1, "full-width bands lower to fills");

    check_parity(context, pipeline, scene, expected, actual, "rounded-stroke", 2);
}

void check_band_rejections(
    veyra::VulkanContext& context,
    veyra::VulkanRectPipeline& pipeline,
    std::span<veyra::SoftwarePixel> scratch
) {
    auto stats = veyra::VulkanRectStats{};

    const auto nan = std::numeric_limits<float>::quiet_NaN();

    const std::array<veyra::DisplayCommand, 1> negative_thickness{
        veyra::StrokeRoundedRect{
            .bounds = {.x = 0.0F, .y = 0.0F, .width = 8.0F, .height = 8.0F},
            .radius = 2.0F,
            .thickness = -1.0F,
            .color = {.red = 255, .alpha = 255},
        },
    };

    require(
        veyra::render_vulkan_rects(
            context,
            pipeline,
            std::span<const veyra::DisplayCommand>(negative_thickness),
            scratch,
            stats
        ) == veyra::VulkanRenderResult::invalid_input,
        "negative band thickness is rejected"
    );

    const std::array<veyra::DisplayCommand, 1> negative_radius{
        veyra::StrokeRoundedRect{
            .bounds = {.x = 0.0F, .y = 0.0F, .width = 8.0F, .height = 8.0F},
            .radius = -1.0F,
            .thickness = 2.0F,
            .color = {.red = 255, .alpha = 255},
        },
    };

    require(
        veyra::render_vulkan_rects(
            context,
            pipeline,
            std::span<const veyra::DisplayCommand>(negative_radius),
            scratch,
            stats
        ) == veyra::VulkanRenderResult::invalid_input,
        "negative band radius is rejected"
    );

    const std::array<veyra::DisplayCommand, 1> indefinite{
        veyra::StrokeRoundedRect{
            .bounds = {.x = 0.0F, .y = 0.0F, .width = 8.0F, .height = 8.0F},
            .radius = 2.0F,
            .thickness = nan,
            .color = {.red = 255, .alpha = 255},
        },
    };

    require(
        veyra::render_vulkan_rects(
            context,
            pipeline,
            std::span<const veyra::DisplayCommand>(indefinite),
            scratch,
            stats
        ) == veyra::VulkanRenderResult::invalid_input,
        "NaN band thickness is rejected"
    );

    const std::array<veyra::DisplayCommand, 1> unbounded{
        veyra::StrokeRoundedRect{
            .bounds = {.x = std::numeric_limits<float>::infinity(), .width = 8.0F, .height = 8.0F},
            .radius = 2.0F,
            .thickness = 2.0F,
            .color = {.red = 255, .alpha = 255},
        },
    };

    require(
        veyra::render_vulkan_rects(
            context,
            pipeline,
            std::span<const veyra::DisplayCommand>(unbounded),
            scratch,
            stats
        ) == veyra::VulkanRenderResult::invalid_input,
        "non-finite band bounds are rejected"
    );
}

void check_steady_bands_allocate_nothing(
    veyra::VulkanContext& context,
    veyra::VulkanRectPipeline& pipeline,
    std::span<veyra::SoftwarePixel> expected,
    std::span<veyra::SoftwarePixel> actual,
    SoftwareRooms& rooms
) {
    const std::array<veyra::DisplayCommand, 1> storage{
        veyra::StrokeRoundedRect{
            .bounds = {.x = 8.0F, .y = 8.0F, .width = 64.0F, .height = 32.0F},
            .radius = 8.0F,
            .thickness = 4.0F,
            .color = {.red = 30, .green = 140, .blue = 60, .alpha = 255},
        },
    };

    const auto scene = std::span<const veyra::DisplayCommand>{storage.data(), storage.size()};

    render_reference(scene, expected, kWidth, kHeight, rooms);

    auto stats = veyra::VulkanRectStats{};

    require(
        veyra::render_vulkan_rects(context, pipeline, scene, actual, stats) ==
            veyra::VulkanRenderResult::complete,
        "steady window opens clean"
    );

    g_allocation_count.store(0, std::memory_order_relaxed);
    g_counting.store(true, std::memory_order_relaxed);

    for (auto repeat = std::size_t{}; repeat < 60; ++repeat) {
        const auto result = veyra::render_vulkan_rects(context, pipeline, scene, actual, stats);

        if (result != veyra::VulkanRenderResult::complete) {
            g_counting.store(false, std::memory_order_relaxed);

            throw std::runtime_error("steady band repeats stay complete");
        }
    }

    g_counting.store(false, std::memory_order_relaxed);

    std::cout << "vulkan rounded-stroke repeats=60 host_allocations="
              << g_allocation_count.load(std::memory_order_relaxed) << '\n';

    require(g_allocation_count.load(std::memory_order_relaxed) == 0, "bands allocate nothing");
}

int run() {
    auto context = veyra::VulkanContext{};

    require(
        veyra::create_vulkan_context(context, kWidth, kHeight) == veyra::VulkanContextResult::ready,
        "vulkan context comes up"
    );

    auto pipeline = veyra::VulkanRectPipeline{};

    require(
        veyra::create_vulkan_rect_pipeline(context, pipeline) ==
            veyra::VulkanRenderResult::complete,
        "band-capable pipeline comes up"
    );

    auto rooms = SoftwareRooms{};
    auto expected = std::array<veyra::SoftwarePixel, kWidth * kHeight>{};
    auto actual = std::array<veyra::SoftwarePixel, kWidth * kHeight>{};

    check_band_parity(
        context,
        pipeline,
        std::span<veyra::SoftwarePixel>{expected.data(), expected.size()},
        std::span<veyra::SoftwarePixel>{actual.data(), actual.size()},
        rooms
    );

    check_band_rejections(
        context,
        pipeline,
        std::span<veyra::SoftwarePixel>{actual.data(), actual.size()}
    );

    check_steady_bands_allocate_nothing(
        context,
        pipeline,
        std::span<veyra::SoftwarePixel>{expected.data(), expected.size()},
        std::span<veyra::SoftwarePixel>{actual.data(), actual.size()},
        rooms
    );

    veyra::destroy_vulkan_rect_pipeline(context, pipeline);
    veyra::destroy_vulkan_context(context);

    require(!veyra::vulkan_context_ready(context), "context tears down");

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
