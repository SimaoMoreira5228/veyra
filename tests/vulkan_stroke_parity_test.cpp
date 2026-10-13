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

void check_line_parity(
    veyra::VulkanContext& context,
    veyra::VulkanRectPipeline& pipeline,
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

    auto stats = veyra::VulkanRectStats{};

    require(
        veyra::render_vulkan_rects(context, pipeline, scene, actual, stats) ==
            veyra::VulkanRenderResult::complete,
        "lines and a zero-width stroke complete"
    );
    require(stats.submitted == 4, "zero-width strokes never submit");
    require(stats.stroke_draws >= 1, "segments draw through the stroke pipeline");

    check_parity(context, pipeline, scene, expected, actual, "stroke-line", 2);
}

void check_polyline_parity(
    veyra::VulkanContext& context,
    veyra::VulkanRectPipeline& pipeline,
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

    auto stats = veyra::VulkanRectStats{};

    require(
        veyra::render_vulkan_rects(context, pipeline, scene, actual, stats) ==
            veyra::VulkanRenderResult::complete,
        "open and closed polylines complete"
    );
    require(stats.submitted == 8, "polylines submit one instance per segment");
    require(stats.stroke_draws >= 1, "segments draw through the stroke pipeline");

    check_parity(context, pipeline, scene, expected, actual, "stroke-polyline", 2);
}

void check_stroke_rejections(
    veyra::VulkanContext& context,
    veyra::VulkanRectPipeline& pipeline,
    std::span<veyra::SoftwarePixel> scratch
) {
    auto stats = veyra::VulkanRectStats{};

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
        veyra::render_vulkan_rects(
            context,
            pipeline,
            std::span<const veyra::DisplayCommand>(negative),
            scratch,
            stats
        ) == veyra::VulkanRenderResult::invalid_input,
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
        veyra::render_vulkan_rects(
            context,
            pipeline,
            std::span<const veyra::DisplayCommand>(indefinite),
            scratch,
            stats
        ) == veyra::VulkanRenderResult::invalid_input,
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
        veyra::render_vulkan_rects(
            context,
            pipeline,
            std::span<const veyra::DisplayCommand>(unbounded),
            scratch,
            stats
        ) == veyra::VulkanRenderResult::invalid_input,
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
        veyra::render_vulkan_rects(
            context,
            pipeline,
            std::span<const veyra::DisplayCommand>(degenerate),
            scratch,
            stats
        ) == veyra::VulkanRenderResult::invalid_input,
        "single-point polylines are rejected"
    );
}

void check_steady_strokes_allocate_nothing(
    veyra::VulkanContext& context,
    veyra::VulkanRectPipeline& pipeline,
    std::span<veyra::SoftwarePixel> expected,
    std::span<veyra::SoftwarePixel> actual,
    SoftwareRooms& rooms
) {
    auto points = std::array<veyra::LayoutPoint, 4>{
        veyra::LayoutPoint{.x = 8.0F, .y = 8.0F},
        veyra::LayoutPoint{.x = 32.0F, .y = 24.0F},
        veyra::LayoutPoint{.x = 56.0F, .y = 8.0F},
        veyra::LayoutPoint{.x = 80.0F, .y = 24.0F},
    };

    const std::array<veyra::DisplayCommand, 1> storage{
        veyra::StrokePolyline{
            .points = points,
            .thickness = 2.0F,
            .color = {.red = 30, .green = 140, .blue = 60, .alpha = 255},
            .closed = false,
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

            throw std::runtime_error("steady stroke repeats stay complete");
        }
    }

    g_counting.store(false, std::memory_order_relaxed);

    std::cout << "vulkan stroke repeats=60 host_allocations="
              << g_allocation_count.load(std::memory_order_relaxed) << '\n';

    require(g_allocation_count.load(std::memory_order_relaxed) == 0, "strokes allocate nothing");
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
        "capsule-capable pipeline comes up"
    );

    auto rooms = SoftwareRooms{};
    auto expected = std::array<veyra::SoftwarePixel, kWidth * kHeight>{};
    auto actual = std::array<veyra::SoftwarePixel, kWidth * kHeight>{};

    check_line_parity(
        context,
        pipeline,
        std::span<veyra::SoftwarePixel>{expected.data(), expected.size()},
        std::span<veyra::SoftwarePixel>{actual.data(), actual.size()},
        rooms
    );

    check_polyline_parity(
        context,
        pipeline,
        std::span<veyra::SoftwarePixel>{expected.data(), expected.size()},
        std::span<veyra::SoftwarePixel>{actual.data(), actual.size()},
        rooms
    );

    check_stroke_rejections(
        context,
        pipeline,
        std::span<veyra::SoftwarePixel>{actual.data(), actual.size()}
    );

    check_steady_strokes_allocate_nothing(
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
