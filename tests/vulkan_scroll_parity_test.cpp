#include "allocation_counter.hpp"
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
#include <new>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using veyra::vulkan_parity::check_parity;
using veyra::vulkan_parity::render_reference;
using veyra::vulkan_parity::require;
using veyra::vulkan_parity::SoftwareRooms;

constexpr std::size_t kWidth = 96;
constexpr std::size_t kHeight = 72;
constexpr std::size_t kRows = 9;
constexpr std::size_t kRowHeight = 8;

void build_rows(std::span<veyra::DisplayCommand> storage, std::size_t offset) {
    storage[0] = veyra::FillRect{
        .bounds = {.width = static_cast<float>(kWidth), .height = static_cast<float>(kHeight)},
        .color = {.red = 240, .green = 240, .blue = 244, .alpha = 255},
    };

    for (auto row = std::size_t{}; row < kRows; ++row) {
        const auto shade = static_cast<std::uint8_t>(20 + (row + offset) % 9 * 12);

        storage[1 + row] = veyra::FillRect{
            .bounds =
                {
                    .x = 0.0F,
                    .y = static_cast<float>(row * kRowHeight),
                    .width = static_cast<float>(kWidth),
                    .height = static_cast<float>(kRowHeight),
                },
            .color = {.red = shade, .green = 60, .blue = 40, .alpha = 255},
        };
    }
}

void check_scroll_parity(
    veyra::VulkanContext& context,
    veyra::VulkanRectPipeline& pipeline,
    std::span<veyra::SoftwarePixel> expected,
    std::span<veyra::SoftwarePixel> actual,
    SoftwareRooms& rooms
) {
    auto scrolled = std::array<veyra::DisplayCommand, 1 + kRows>{};
    build_rows(scrolled, 0);

    auto stats = veyra::VulkanRectStats{};

    require(
        veyra::render_vulkan_rects(
            context,
            pipeline,
            std::span<const veyra::DisplayCommand>(scrolled),
            actual,
            stats
        ) == veyra::VulkanRenderResult::complete,
        "sync full repaint completes"
    );
    require(stats.submitted == 1 + kRows, "sync submits every row");

    auto shifted = std::array<veyra::DisplayCommand, 1 + kRows>{};
    build_rows(shifted, 1);
    const auto scene = std::span<const veyra::DisplayCommand>{shifted.data(), shifted.size()};

    render_reference(scene, expected, kWidth, kHeight, rooms);

    const std::array<veyra::LayoutRect, 1> exposed{
        veyra::LayoutRect{
            .x = 0.0F,
            .y = static_cast<float>(kHeight - kRowHeight),
            .width = static_cast<float>(kWidth),
            .height = static_cast<float>(kRowHeight),
        },
    };
    const auto damaged = std::span<const veyra::LayoutRect>{exposed.data(), exposed.size()};

    require(
        veyra::render_vulkan_scrolled(
            context,
            pipeline,
            scene,
            actual,
            stats,
            std::span<const veyra::LayoutRect>{},
            0,
            -static_cast<std::int32_t>(kRowHeight)
        ) == veyra::VulkanRenderResult::complete,
        "scrolled rows render on the gpu path"
    );
    require(stats.submitted < 1 + kRows, "retained rows never resubmit");
    require(stats.submitted > 0, "exposed rows submit");

    check_parity(context, pipeline, scene, expected, actual, "scroll-rows", 1, damaged);
}

void check_scroll_fallback(
    veyra::VulkanContext& context,
    veyra::VulkanRectPipeline& pipeline,
    std::span<veyra::SoftwarePixel> expected,
    std::span<veyra::SoftwarePixel> actual,
    SoftwareRooms& rooms
) {
    auto shifted = std::array<veyra::DisplayCommand, 1 + kRows>{};
    build_rows(shifted, 0);
    const auto scene = std::span<const veyra::DisplayCommand>{shifted.data(), shifted.size()};

    render_reference(scene, expected, kWidth, kHeight, rooms);

    auto stats = veyra::VulkanRectStats{};

    require(
        veyra::render_vulkan_scrolled(
            context,
            pipeline,
            scene,
            actual,
            stats,
            std::span<const veyra::LayoutRect>{},
            0,
            -static_cast<std::int32_t>(kHeight)
        ) == veyra::VulkanRenderResult::complete,
        "oversized scroll falls back to a full repaint"
    );
    require(stats.submitted == 1 + kRows, "fallback submits every row");

    check_parity(context, pipeline, scene, expected, actual, "scroll-fallback", 1);
}

void check_scroll_rejections(
    veyra::VulkanContext& context,
    veyra::VulkanRectPipeline& pipeline,
    std::span<veyra::SoftwarePixel> scratch
) {
    auto stats = veyra::VulkanRectStats{};

    const std::array<veyra::DisplayCommand, 1> storage{
        veyra::FillRect{
            .bounds = {.width = static_cast<float>(kWidth), .height = static_cast<float>(kHeight)},
            .color = {.red = 200, .green = 40, .blue = 40, .alpha = 255},
        },
    };
    const auto scene = std::span<const veyra::DisplayCommand>{storage.data(), storage.size()};

    const std::array<veyra::LayoutRect, 1> overlapping{
        veyra::LayoutRect{
            .x = 0.0F,
            .y = 60.0F,
            .width = static_cast<float>(kWidth),
            .height = 12.0F
        },
    };

    require(
        veyra::render_vulkan_scrolled(
            context,
            pipeline,
            scene,
            scratch,
            stats,
            std::span<const veyra::LayoutRect>(overlapping),
            0,
            -static_cast<std::int32_t>(kRowHeight)
        ) == veyra::VulkanRenderResult::invalid_input,
        "damage overlapping the exposed strip is rejected"
    );
}

void check_steady_scroll_allocate_nothing(
    veyra::VulkanContext& context,
    veyra::VulkanRectPipeline& pipeline,
    std::span<veyra::SoftwarePixel> expected,
    std::span<veyra::SoftwarePixel> actual,
    SoftwareRooms& rooms
) {
    auto shifted = std::array<veyra::DisplayCommand, 1 + kRows>{};
    build_rows(shifted, 0);
    const auto scene = std::span<const veyra::DisplayCommand>{shifted.data(), shifted.size()};

    render_reference(scene, expected, kWidth, kHeight, rooms);

    auto stats = veyra::VulkanRectStats{};

    require(
        veyra::render_vulkan_scrolled(
            context,
            pipeline,
            scene,
            actual,
            stats,
            std::span<const veyra::LayoutRect>{},
            0,
            -static_cast<std::int32_t>(kRowHeight)
        ) == veyra::VulkanRenderResult::complete,
        "steady window opens clean"
    );

    check_parity(context, pipeline, scene, expected, actual, "scroll-steady", 1);

    g_allocation_count.store(0, std::memory_order_relaxed);
    g_counting.store(true, std::memory_order_relaxed);

    for (auto repeat = std::size_t{}; repeat < 60; ++repeat) {
        const auto result = veyra::render_vulkan_scrolled(
            context,
            pipeline,
            scene,
            actual,
            stats,
            std::span<const veyra::LayoutRect>{},
            0,
            -static_cast<std::int32_t>(kRowHeight)
        );

        if (result != veyra::VulkanRenderResult::complete) {
            g_counting.store(false, std::memory_order_relaxed);

            throw std::runtime_error("steady scroll repeats stay complete");
        }
    }

    g_counting.store(false, std::memory_order_relaxed);

    std::cout << "vulkan scroll repeats=60 host_allocations="
              << g_allocation_count.load(std::memory_order_relaxed) << '\n';

    require(g_allocation_count.load(std::memory_order_relaxed) == 0, "scrolling allocates nothing");
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
        "scroll-capable pipeline comes up"
    );

    auto rooms = SoftwareRooms{};
    auto expected = std::array<veyra::SoftwarePixel, kWidth * kHeight>{};
    auto actual = std::array<veyra::SoftwarePixel, kWidth * kHeight>{};

    check_scroll_parity(
        context,
        pipeline,
        std::span<veyra::SoftwarePixel>{expected.data(), expected.size()},
        std::span<veyra::SoftwarePixel>{actual.data(), actual.size()},
        rooms
    );

    check_scroll_fallback(
        context,
        pipeline,
        std::span<veyra::SoftwarePixel>{expected.data(), expected.size()},
        std::span<veyra::SoftwarePixel>{actual.data(), actual.size()},
        rooms
    );

    check_scroll_rejections(
        context,
        pipeline,
        std::span<veyra::SoftwarePixel>{actual.data(), actual.size()}
    );

    check_steady_scroll_allocate_nothing(
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
