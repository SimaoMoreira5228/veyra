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
#include <vector>

namespace {

using veyra::vulkan_parity::check_parity;
using veyra::vulkan_parity::render_reference;
using veyra::vulkan_parity::require;
using veyra::vulkan_parity::SoftwareRooms;
using veyra::vulkan_parity::TestClock;

constexpr std::size_t kWidth = 96;
constexpr std::size_t kHeight = 72;

void check_mask_parity(
    veyra::VulkanContext& context,
    veyra::VulkanRectPipeline& pipeline,
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

    auto stats = veyra::VulkanRectStats{};

    require(
        veyra::render_vulkan_rects(context, pipeline, scene, actual, stats) ==
            veyra::VulkanRenderResult::complete,
        "mixed rect and mask scene completes"
    );
    require(stats.submitted == 4, "alpha-zero and offscreen masks never submit");
    require(stats.mask_draws >= 1, "glyph runs draw through the mask pipeline");

    check_parity(context, pipeline, scene, expected, actual, "mask", 2);
}

void check_mask_edge_parity(
    veyra::VulkanContext& context,
    veyra::VulkanRectPipeline& pipeline,
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

    auto stats = veyra::VulkanRectStats{};

    require(
        veyra::render_vulkan_rects(context, pipeline, scene, actual, stats) ==
            veyra::VulkanRenderResult::complete,
        "negative and edge-clipped masks complete"
    );

    check_parity(context, pipeline, scene, expected, actual, "mask-edge", 2);
}

void check_unified_masks(
    veyra::VulkanContext& context,
    std::span<veyra::SoftwarePixel> expected,
    std::span<veyra::SoftwarePixel> actual,
    SoftwareRooms& rooms
) {
    auto unified = veyra::VulkanRectPipeline{};

    require(
        veyra::create_vulkan_unified_pipeline(context, unified) ==
            veyra::VulkanRenderResult::complete,
        "unified pipeline comes up for masks"
    );

    auto coverage = std::array<std::uint8_t, 72>{};

    for (auto index = std::size_t{}; index < coverage.size(); ++index) {
        coverage[index] = static_cast<std::uint8_t>(96 + (index * 37) % 160);
    }

    const auto tile = veyra::SoftwareMask{
        .width = 6,
        .height = 12,
        .stride = 6,
        .coverage = coverage,
        .generation = 76,
    };

    const std::array<veyra::DisplayCommand, 2> storage{
        veyra::FillRect{
            .bounds = {.width = static_cast<float>(kWidth), .height = static_cast<float>(kHeight)},
            .color = {.red = 240, .green = 240, .blue = 244, .alpha = 255},
        },
        veyra::DrawMask{
            .x = 8.0F,
            .y = 10.0F,
            .mask = &tile,
            .color = {.red = 30, .green = 31, .blue = 38, .alpha = 255},
        },
    };

    const auto scene = std::span<const veyra::DisplayCommand>{storage.data(), storage.size()};

    render_reference(scene, expected, kWidth, kHeight, rooms);

    auto stats = veyra::VulkanRectStats{};

    require(
        veyra::render_vulkan_rects(context, unified, scene, actual, stats) ==
            veyra::VulkanRenderResult::complete,
        "masks bypass the unified rect routing"
    );
    require(stats.mask_draws == 1, "unified still batches the mask run");

    check_parity(context, unified, scene, expected, actual, "mask-unified", 1);

    veyra::destroy_vulkan_rect_pipeline(context, unified);
}

void check_mask_rejections(
    veyra::VulkanContext& context,
    veyra::VulkanRectPipeline& pipeline,
    std::span<veyra::SoftwarePixel> scratch
) {
    auto stats = veyra::VulkanRectStats{};

    const std::array<veyra::DisplayCommand, 1> missing{
        veyra::DrawMask{
            .x = 8.0F,
            .y = 8.0F,
            .mask = nullptr,
            .color = {.red = 200, .alpha = 255},
        },
    };

    require(
        veyra::render_vulkan_rects(
            context,
            pipeline,
            std::span<const veyra::DisplayCommand>(missing),
            scratch,
            stats
        ) == veyra::VulkanRenderResult::invalid_input,
        "maskless draws are rejected"
    );

    auto short_coverage = std::array<std::uint8_t, 4>{255, 255, 255, 255};

    const auto short_tile = veyra::SoftwareMask{
        .width = 4,
        .height = 4,
        .stride = 2,
        .coverage = short_coverage,
        .generation = 73,
    };

    const std::array<veyra::DisplayCommand, 1> truncated{
        veyra::DrawMask{
            .x = 8.0F,
            .y = 8.0F,
            .mask = &short_tile,
            .color = {.red = 200, .alpha = 255},
        },
    };

    require(
        veyra::render_vulkan_rects(
            context,
            pipeline,
            std::span<const veyra::DisplayCommand>(truncated),
            scratch,
            stats
        ) == veyra::VulkanRenderResult::invalid_input,
        "short mask storage is rejected"
    );
}

void check_steady_masks_allocate_nothing(
    veyra::VulkanContext& context,
    veyra::VulkanRectPipeline& pipeline,
    std::span<veyra::SoftwarePixel> expected,
    std::span<veyra::SoftwarePixel> actual,
    SoftwareRooms& rooms
) {
    auto coverage = std::array<std::uint8_t, 72>{};

    for (auto index = std::size_t{}; index < coverage.size(); ++index) {
        coverage[index] = static_cast<std::uint8_t>(96 + (index * 37) % 160);
    }

    const auto tile = veyra::SoftwareMask{
        .width = 6,
        .height = 12,
        .stride = 6,
        .coverage = coverage,
        .generation = 74,
    };

    const std::array<veyra::DisplayCommand, 1> storage{
        veyra::DrawMask{
            .x = 8.0F,
            .y = 10.0F,
            .mask = &tile,
            .color = {.red = 30, .green = 31, .blue = 38, .alpha = 255},
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

            throw std::runtime_error("steady mask repeats stay complete");
        }
    }

    g_counting.store(false, std::memory_order_relaxed);

    std::cout << "vulkan mask repeats=60 host_allocations="
              << g_allocation_count.load(std::memory_order_relaxed) << '\n';

    require(g_allocation_count.load(std::memory_order_relaxed) == 0, "cached masks allocate");
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
        "mask-capable pipeline comes up"
    );

    auto rooms = SoftwareRooms{};
    auto expected = std::array<veyra::SoftwarePixel, kWidth * kHeight>{};
    auto actual = std::array<veyra::SoftwarePixel, kWidth * kHeight>{};

    check_mask_parity(
        context,
        pipeline,
        std::span<veyra::SoftwarePixel>{expected.data(), expected.size()},
        std::span<veyra::SoftwarePixel>{actual.data(), actual.size()},
        rooms
    );

    check_mask_rejections(
        context,
        pipeline,
        std::span<veyra::SoftwarePixel>{actual.data(), actual.size()}
    );

    check_mask_edge_parity(
        context,
        pipeline,
        std::span<veyra::SoftwarePixel>{expected.data(), expected.size()},
        std::span<veyra::SoftwarePixel>{actual.data(), actual.size()},
        rooms
    );

    check_unified_masks(
        context,
        std::span<veyra::SoftwarePixel>{expected.data(), expected.size()},
        std::span<veyra::SoftwarePixel>{actual.data(), actual.size()},
        rooms
    );

    check_steady_masks_allocate_nothing(
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
