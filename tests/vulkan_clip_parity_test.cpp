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
#include <limits>
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

void check_clip_parity(
    veyra::VulkanContext& context,
    veyra::VulkanRectPipeline& pipeline,
    std::span<veyra::SoftwarePixel> expected,
    std::span<veyra::SoftwarePixel> actual,
    SoftwareRooms& rooms
) {
    auto image_pixels = std::array<veyra::SoftwarePixel, 4>{};

    for (auto& pixel : image_pixels) {
        pixel = veyra::SoftwarePixel{.red = 200, .alpha = 255};
    }

    auto tile = veyra::SoftwareImage{
        .width = 2,
        .height = 2,
        .stride = 2,
        .pixels = image_pixels,
        .generation = 61,
    };

    auto mask_coverage = std::array<std::uint8_t, 16>{};

    for (auto index = std::size_t{}; index < mask_coverage.size(); ++index) {
        mask_coverage[index] = static_cast<std::uint8_t>(96 + (index * 37) % 160);
    }

    const auto mask = veyra::SoftwareMask{
        .width = 4,
        .height = 4,
        .stride = 4,
        .coverage = mask_coverage,
        .generation = 62,
    };

    const std::array<veyra::SoftwarePathSegment, 3> triangle_segments{
        veyra::SoftwareLineTo{.end = {.x = 88.0F, .y = 64.0F}},
        veyra::SoftwareLineTo{.end = {.x = 56.0F, .y = 64.0F}},
        veyra::SoftwareLineTo{.end = {.x = 72.0F, .y = 48.0F}},
    };
    const std::array<veyra::SoftwarePathContour, 1> triangle_contours{
        veyra::SoftwarePathContour{
            .start = {.x = 72.0F, .y = 48.0F},
            .segments = triangle_segments,
        },
    };

    const std::array<veyra::DisplayCommand, 28> storage{
        veyra::FillRect{
            .bounds = {.width = static_cast<float>(kWidth), .height = static_cast<float>(kHeight)},
            .color = {.red = 240, .green = 240, .blue = 244, .alpha = 255},
        },
        veyra::PushClip{.bounds = {.x = 16.0F, .y = 16.0F, .width = 48.0F, .height = 32.0F}},
        veyra::FillRect{
            .bounds = {.x = 8.0F, .y = 8.0F, .width = 80.0F, .height = 56.0F},
            .color = {.red = 200, .green = 40, .blue = 40, .alpha = 255},
        },
        veyra::PushClip{.bounds = {.x = 24.0F, .y = 24.0F, .width = 32.0F, .height = 16.0F}},
        veyra::FillRect{
            .bounds = {.width = static_cast<float>(kWidth), .height = static_cast<float>(kHeight)},
            .color = {.red = 40, .green = 70, .blue = 210, .alpha = 255},
        },
        veyra::PopClip{},
        veyra::PopClip{},
        veyra::PushTranslation{.offset = {.x = 4.0F, .y = 4.0F}},
        veyra::PushClip{.bounds = {.x = 60.0F, .y = 40.0F, .width = 20.0F, .height = 20.0F}},
        veyra::FillRect{
            .bounds = {.width = static_cast<float>(kWidth), .height = static_cast<float>(kHeight)},
            .color = {.red = 30, .green = 140, .blue = 60, .alpha = 255},
        },
        veyra::PopClip{},
        veyra::PopTranslation{},
        veyra::PushClip{.bounds = {.x = 60.0F, .y = 12.0F, .width = 24.0F, .height = 16.0F}},
        veyra::FillRoundedRect{
            .bounds = {.x = 56.0F, .y = 8.0F, .width = 32.0F, .height = 24.0F},
            .radius = 6.0F,
            .color = {.red = 230, .green = 120, .blue = 20, .alpha = 255},
        },
        veyra::PopClip{},
        veyra::PushClip{.bounds = {.x = 32.0F, .y = 52.0F, .width = 32.0F, .height = 16.0F}},
        veyra::StrokeLine{
            .start = {.x = 0.0F, .y = 60.0F},
            .end = {.x = 96.0F, .y = 60.0F},
            .thickness = 3.0F,
            .color = {.red = 20, .green = 20, .blue = 24, .alpha = 255},
        },
        veyra::StrokeRoundedRect{
            .bounds = {.x = 28.0F, .y = 50.0F, .width = 40.0F, .height = 20.0F},
            .radius = 5.0F,
            .thickness = 2.0F,
            .color = {.red = 230, .green = 180, .blue = 20, .alpha = 255},
        },
        veyra::FillPath{
            .contours = triangle_contours,
            .color = {.red = 150, .green = 40, .blue = 170, .alpha = 255},
            .fill_rule = veyra::SoftwarePathFillRule::even_odd,
        },
        veyra::DrawMask{
            .x = 32.0F,
            .y = 52.0F,
            .mask = &mask,
            .color = {.red = 230, .green = 232, .blue = 240, .alpha = 255},
        },
        veyra::PopClip{},
        veyra::PushClip{.bounds = {.x = 64.0F, .y = 48.0F, .width = 16.0F, .height = 8.0F}},
        veyra::DrawImage{
            .source = {.width = 2.0F, .height = 2.0F},
            .destination = {.x = 64.0F, .y = 44.0F, .width = 16.0F, .height = 16.0F},
            .image = &tile,
            .sampling = veyra::SoftwareImageSampling::nearest,
            .opacity = 255,
        },
        veyra::PopClip{},
        veyra::PushClip{.bounds = {.x = 80.0F, .y = 60.0F, .width = 8.0F, .height = 4.0F}},
        veyra::FillRect{
            .bounds = {.x = 0.0F, .y = 0.0F, .width = 8.0F, .height = 8.0F},
            .color = {.red = 200, .green = 40, .blue = 40, .alpha = 255},
        },
        veyra::PopClip{},
    };

    const auto scene = std::span<const veyra::DisplayCommand>{storage.data(), storage.size()};

    render_reference(scene, expected, kWidth, kHeight, rooms);

    require(expected[20 * kWidth + 20].green < 100, "outer clip keeps the red block inside");
    require(expected[20 * kWidth + 70].green > 100, "outer clip cuts the red block outside");
    require(expected[30 * kWidth + 40].red < 100, "nested clip keeps the blue block inside");
    require(
        expected[20 * kWidth + 40].red > 100,
        "nested clip cuts the blue block outside the inner rect"
    );
    require(expected[50 * kWidth + 80].red < 100, "translated clip lands on the translated bounds");
    require(expected[45 * kWidth + 62].red > 100, "translated clip cuts the untranslated corner");
    require(expected[4 * kWidth + 4].red > 100, "fully clipped rects leave the background alone");

    auto stats = veyra::VulkanRectStats{};

    require(
        veyra::render_vulkan_rects(context, pipeline, scene, actual, stats) ==
            veyra::VulkanRenderResult::complete,
        "nested, translated, and per-pipe clips complete"
    );
    require(stats.submitted == 17, "fully clipped rects never submit");

    check_parity(context, pipeline, scene, expected, actual, "clip", 1);
}

void check_clip_rejections(
    veyra::VulkanContext& context,
    veyra::VulkanRectPipeline& pipeline,
    std::span<veyra::SoftwarePixel> scratch
) {
    auto stats = veyra::VulkanRectStats{};

    const auto nan = std::numeric_limits<float>::quiet_NaN();

    const std::array<veyra::DisplayCommand, 1> negative{
        veyra::PushClip{.bounds = {.width = -8.0F, .height = 8.0F}, .clip_id = 1},
    };

    require(
        veyra::render_vulkan_rects(
            context,
            pipeline,
            std::span<const veyra::DisplayCommand>(negative),
            scratch,
            stats
        ) == veyra::VulkanRenderResult::invalid_input,
        "negative clip bounds are rejected"
    );

    const std::array<veyra::DisplayCommand, 1> indefinite{
        veyra::PushClip{.bounds = {.x = nan, .width = 8.0F, .height = 8.0F}, .clip_id = 2},
    };

    require(
        veyra::render_vulkan_rects(
            context,
            pipeline,
            std::span<const veyra::DisplayCommand>(indefinite),
            scratch,
            stats
        ) == veyra::VulkanRenderResult::invalid_input,
        "NaN clip bounds are rejected"
    );

    const std::array<veyra::DisplayCommand, 1> popped{
        veyra::PopClip{},
    };

    require(
        veyra::render_vulkan_rects(
            context,
            pipeline,
            std::span<const veyra::DisplayCommand>(popped),
            scratch,
            stats
        ) == veyra::VulkanRenderResult::invalid_input,
        "popping an empty clip stack is rejected"
    );

    const std::array<veyra::DisplayCommand, 1> unclosed{
        veyra::PushClip{.bounds = {.width = 8.0F, .height = 8.0F}, .clip_id = 3},
    };

    require(
        veyra::render_vulkan_rects(
            context,
            pipeline,
            std::span<const veyra::DisplayCommand>(unclosed),
            scratch,
            stats
        ) == veyra::VulkanRenderResult::invalid_input,
        "leaving a clip open is rejected"
    );

    auto deep = std::array<veyra::DisplayCommand, 9>{};

    for (auto& command : deep) {
        command = veyra::PushClip{.bounds = {.width = 8.0F, .height = 8.0F}, .clip_id = 4};
    }

    require(
        veyra::render_vulkan_rects(
            context,
            pipeline,
            std::span<const veyra::DisplayCommand>(deep),
            scratch,
            stats
        ) == veyra::VulkanRenderResult::insufficient_capacity,
        "clips past the stack bound report capacity"
    );
}

void check_steady_clips_allocate_nothing(
    veyra::VulkanContext& context,
    veyra::VulkanRectPipeline& pipeline,
    std::span<veyra::SoftwarePixel> expected,
    std::span<veyra::SoftwarePixel> actual,
    SoftwareRooms& rooms
) {
    const std::array<veyra::DisplayCommand, 4> storage{
        veyra::PushClip{.bounds = {.x = 8.0F, .y = 8.0F, .width = 64.0F, .height = 32.0F}},
        veyra::StrokeRoundedRect{
            .bounds = {.x = 8.0F, .y = 8.0F, .width = 64.0F, .height = 32.0F},
            .radius = 8.0F,
            .thickness = 4.0F,
            .color = {.red = 30, .green = 140, .blue = 60, .alpha = 255},
        },
        veyra::PopClip{},
        veyra::FillRect{
            .bounds = {.width = static_cast<float>(kWidth), .height = static_cast<float>(kHeight)},
            .color = {.red = 240, .green = 240, .blue = 244, .alpha = 255},
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

            throw std::runtime_error("steady clip repeats stay complete");
        }
    }

    g_counting.store(false, std::memory_order_relaxed);

    std::cout << "vulkan clip repeats=60 host_allocations="
              << g_allocation_count.load(std::memory_order_relaxed) << '\n';

    require(g_allocation_count.load(std::memory_order_relaxed) == 0, "clips allocate nothing");
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
        "clip-capable pipeline comes up"
    );

    auto rooms = SoftwareRooms{};
    auto expected = std::array<veyra::SoftwarePixel, kWidth * kHeight>{};
    auto actual = std::array<veyra::SoftwarePixel, kWidth * kHeight>{};

    check_clip_parity(
        context,
        pipeline,
        std::span<veyra::SoftwarePixel>{expected.data(), expected.size()},
        std::span<veyra::SoftwarePixel>{actual.data(), actual.size()},
        rooms
    );

    check_clip_rejections(
        context,
        pipeline,
        std::span<veyra::SoftwarePixel>{actual.data(), actual.size()}
    );

    check_steady_clips_allocate_nothing(
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
