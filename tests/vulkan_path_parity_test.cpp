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

void check_path_parity(
    veyra::VulkanContext& context,
    veyra::VulkanRectPipeline& pipeline,
    std::span<veyra::SoftwarePixel> expected,
    std::span<veyra::SoftwarePixel> actual,
    SoftwareRooms& rooms
) {
    const std::array<veyra::SoftwarePathSegment, 3> outer_segments{
        veyra::SoftwareLineTo{.end = {.x = 60.0F, .y = 10.0F}},
        veyra::SoftwareLineTo{.end = {.x = 60.0F, .y = 40.0F}},
        veyra::SoftwareLineTo{.end = {.x = 10.0F, .y = 40.0F}},
    };
    const std::array<veyra::SoftwarePathSegment, 3> hole_segments{
        veyra::SoftwareLineTo{.end = {.x = 45.0F, .y = 18.0F}},
        veyra::SoftwareLineTo{.end = {.x = 45.0F, .y = 32.0F}},
        veyra::SoftwareLineTo{.end = {.x = 25.0F, .y = 32.0F}},
    };
    const std::array<veyra::SoftwarePathContour, 2> holed_contours{
        veyra::SoftwarePathContour{
            .start = {.x = 10.0F, .y = 10.0F},
            .segments = outer_segments,
        },
        veyra::SoftwarePathContour{
            .start = {.x = 25.0F, .y = 18.0F},
            .segments = hole_segments,
        },
    };

    const std::array<veyra::SoftwarePathSegment, 3> union_left_segments{
        veyra::SoftwareLineTo{.end = {.x = 34.0F, .y = 44.0F}},
        veyra::SoftwareLineTo{.end = {.x = 34.0F, .y = 64.0F}},
        veyra::SoftwareLineTo{.end = {.x = 10.0F, .y = 64.0F}},
    };
    const std::array<veyra::SoftwarePathSegment, 3> union_right_segments{
        veyra::SoftwareLineTo{.end = {.x = 48.0F, .y = 44.0F}},
        veyra::SoftwareLineTo{.end = {.x = 48.0F, .y = 64.0F}},
        veyra::SoftwareLineTo{.end = {.x = 24.0F, .y = 64.0F}},
    };
    const std::array<veyra::SoftwarePathContour, 2> union_contours{
        veyra::SoftwarePathContour{
            .start = {.x = 10.0F, .y = 44.0F},
            .segments = union_left_segments,
        },
        veyra::SoftwarePathContour{
            .start = {.x = 24.0F, .y = 44.0F},
            .segments = union_right_segments,
        },
    };

    const std::array<veyra::SoftwarePathSegment, 3> dome_segments{
        veyra::SoftwareLineTo{.end = {.x = 90.0F, .y = 20.0F}},
        veyra::SoftwareQuadraticTo{
            .control = {.x = 75.0F, .y = 5.0F},
            .end = {.x = 60.0F, .y = 20.0F},
        },
        veyra::SoftwareLineTo{.end = {.x = 60.0F, .y = 40.0F}},
    };
    const std::array<veyra::SoftwarePathContour, 1> dome_contours{
        veyra::SoftwarePathContour{
            .start = {.x = 90.0F, .y = 40.0F},
            .segments = dome_segments,
        },
    };

    const std::array<veyra::SoftwarePathSegment, 3> blob_segments{
        veyra::SoftwareCubicTo{
            .control_1 = {.x = 66.0F, .y = 44.0F},
            .control_2 = {.x = 84.0F, .y = 44.0F},
            .end = {.x = 88.0F, .y = 56.0F},
        },
        veyra::SoftwareLineTo{.end = {.x = 88.0F, .y = 64.0F}},
        veyra::SoftwareLineTo{.end = {.x = 56.0F, .y = 64.0F}},
    };
    const std::array<veyra::SoftwarePathContour, 1> blob_contours{
        veyra::SoftwarePathContour{
            .start = {.x = 56.0F, .y = 52.0F},
            .segments = blob_segments,
        },
    };

    const std::array<veyra::DisplayCommand, 7> storage{
        veyra::FillRect{
            .bounds = {.width = static_cast<float>(kWidth), .height = static_cast<float>(kHeight)},
            .color = {.red = 240, .green = 240, .blue = 244, .alpha = 255},
        },
        veyra::FillPath{
            .contours = holed_contours,
            .color = {.red = 30, .green = 140, .blue = 60, .alpha = 255},
            .fill_rule = veyra::SoftwarePathFillRule::even_odd,
        },
        veyra::FillPath{
            .contours = union_contours,
            .color = {.red = 40, .green = 70, .blue = 210, .alpha = 255},
            .fill_rule = veyra::SoftwarePathFillRule::non_zero,
        },
        veyra::PushTranslation{.offset = {.x = 0.0F, .y = 0.0F}},
        veyra::FillPath{
            .contours = dome_contours,
            .color = {.red = 230, .green = 120, .blue = 20, .alpha = 255},
            .fill_rule = veyra::SoftwarePathFillRule::even_odd,
        },
        veyra::PopTranslation{},
        veyra::FillPath{
            .contours = blob_contours,
            .color = {.red = 150, .green = 40, .blue = 170, .alpha = 160},
            .fill_rule = veyra::SoftwarePathFillRule::even_odd,
        },
    };

    const auto scene = std::span<const veyra::DisplayCommand>{storage.data(), storage.size()};

    render_reference(scene, expected, kWidth, kHeight, rooms);

    require(
        expected[25 * kWidth + 35].red > 100,
        "even-odd hole stays background inside the inner contour"
    );
    require(expected[20 * kWidth + 20].red < 100, "even-odd frame fills between the contours");
    require(expected[54 * kWidth + 29].red < 100, "non-zero union fills the same-winding overlap");

    auto stats = veyra::VulkanRectStats{};

    require(
        veyra::render_vulkan_rects(context, pipeline, scene, actual, stats) ==
            veyra::VulkanRenderResult::complete,
        "rect, hole, union, dome, and blob paths complete"
    );
    require(stats.submitted == 150, "paths lower to one rect per scanline span");
    require(stats.rect_draws >= 1, "spans draw through the rect pipeline");

    check_parity(context, pipeline, scene, expected, actual, "path", 1);
}

void check_path_skips(
    veyra::VulkanContext& context,
    veyra::VulkanRectPipeline& pipeline,
    std::span<veyra::SoftwarePixel> scratch
) {
    const std::array<veyra::SoftwarePathSegment, 1> segments{
        veyra::SoftwareLineTo{.end = {.x = 8.0F, .y = 8.0F}},
    };
    const std::array<veyra::SoftwarePathContour, 1> contours{
        veyra::SoftwarePathContour{
            .start = {.x = 0.0F, .y = 0.0F},
            .segments = segments,
        },
    };
    const std::array<veyra::DisplayCommand, 1> storage{
        veyra::FillPath{
            .contours = contours,
            .color = {.red = 255, .alpha = 0},
            .fill_rule = veyra::SoftwarePathFillRule::even_odd,
        },
    };

    auto stats = veyra::VulkanRectStats{};

    require(
        veyra::render_vulkan_rects(
            context,
            pipeline,
            std::span<const veyra::DisplayCommand>(storage),
            scratch,
            stats
        ) == veyra::VulkanRenderResult::complete,
        "transparent paths complete"
    );
    require(stats.submitted == 0, "transparent paths never submit");
}

void check_path_rejections(
    veyra::VulkanContext& context,
    veyra::VulkanRectPipeline& pipeline,
    std::span<veyra::SoftwarePixel> scratch
) {
    auto stats = veyra::VulkanRectStats{};

    const std::array<veyra::DisplayCommand, 1> empty{
        veyra::FillPath{
            .contours = std::span<const veyra::SoftwarePathContour>{},
            .color = {.red = 255, .alpha = 255},
            .fill_rule = veyra::SoftwarePathFillRule::even_odd,
        },
    };

    require(
        veyra::render_vulkan_rects(
            context,
            pipeline,
            std::span<const veyra::DisplayCommand>(empty),
            scratch,
            stats
        ) == veyra::VulkanRenderResult::invalid_input,
        "contourless paths are rejected"
    );

    const std::array<veyra::SoftwarePathSegment, 1> segments{
        veyra::SoftwareLineTo{.end = {.x = 8.0F, .y = 8.0F}},
    };
    const std::array<veyra::SoftwarePathContour, 1> contours{
        veyra::SoftwarePathContour{
            .start = {.x = 0.0F, .y = 0.0F},
            .segments = segments,
        },
    };
    const std::array<veyra::DisplayCommand, 1> bad_rule{
        veyra::FillPath{
            .contours = contours,
            .color = {.red = 255, .alpha = 255},
            .fill_rule = static_cast<veyra::SoftwarePathFillRule>(7),
        },
    };

    require(
        veyra::render_vulkan_rects(
            context,
            pipeline,
            std::span<const veyra::DisplayCommand>(bad_rule),
            scratch,
            stats
        ) == veyra::VulkanRenderResult::invalid_input,
        "unknown fill rules are rejected"
    );

    const std::array<veyra::SoftwarePathSegment, 1> wild_segments{
        veyra::SoftwareLineTo{.end = {.x = std::numeric_limits<float>::infinity(), .y = 8.0F}},
    };
    const std::array<veyra::SoftwarePathContour, 1> wild_contours{
        veyra::SoftwarePathContour{
            .start = {.x = 0.0F, .y = 0.0F},
            .segments = wild_segments,
        },
    };
    const std::array<veyra::DisplayCommand, 1> wild{
        veyra::FillPath{
            .contours = wild_contours,
            .color = {.red = 255, .alpha = 255},
            .fill_rule = veyra::SoftwarePathFillRule::even_odd,
        },
    };

    require(
        veyra::render_vulkan_rects(
            context,
            pipeline,
            std::span<const veyra::DisplayCommand>(wild),
            scratch,
            stats
        ) == veyra::VulkanRenderResult::invalid_input,
        "non-finite path points are rejected"
    );

    const auto huge = std::numeric_limits<float>::max();
    const std::array<veyra::SoftwarePathSegment, 1> far_segments{
        veyra::SoftwareLineTo{.end = {.x = huge, .y = 1.0F}},
    };
    const std::array<veyra::SoftwarePathContour, 1> far_contours{
        veyra::SoftwarePathContour{
            .start = {.x = 0.0F, .y = 0.0F},
            .segments = far_segments,
        },
    };
    const std::array<veyra::DisplayCommand, 3> overflowing{
        veyra::PushTranslation{.offset = {.x = huge, .y = 0.0F}},
        veyra::FillPath{
            .contours = far_contours,
            .color = {.red = 255, .alpha = 255},
            .fill_rule = veyra::SoftwarePathFillRule::even_odd,
        },
        veyra::PopTranslation{},
    };

    require(
        veyra::render_vulkan_rects(
            context,
            pipeline,
            std::span<const veyra::DisplayCommand>(overflowing),
            scratch,
            stats
        ) == veyra::VulkanRenderResult::invalid_input,
        "paths overflowing to infinity are rejected"
    );
}

void check_path_capacity(
    veyra::VulkanContext& context,
    veyra::VulkanRectPipeline& pipeline,
    std::span<veyra::SoftwarePixel> scratch
) {
    auto zigzag_points = std::array<veyra::LayoutPoint, 301>{};

    for (auto index = std::size_t{}; index < zigzag_points.size(); ++index) {
        zigzag_points[index] = veyra::LayoutPoint{
            .x = static_cast<float>(index) * 0.25F,
            .y = index % 2 == 0 ? 10.0F : 20.0F,
        };
    }

    auto zigzag_segments = std::array<veyra::SoftwarePathSegment, 300>{};

    for (auto index = std::size_t{}; index < zigzag_segments.size(); ++index) {
        zigzag_segments[index] = veyra::SoftwareLineTo{.end = zigzag_points[index + 1]};
    }

    const std::array<veyra::SoftwarePathContour, 1> zigzag_contours{
        veyra::SoftwarePathContour{
            .start = zigzag_points[0],
            .segments = zigzag_segments,
        },
    };
    const std::array<veyra::DisplayCommand, 1> storage{
        veyra::FillPath{
            .contours = zigzag_contours,
            .color = {.red = 255, .alpha = 255},
            .fill_rule = veyra::SoftwarePathFillRule::even_odd,
        },
    };

    auto stats = veyra::VulkanRectStats{};

    require(
        veyra::render_vulkan_rects(
            context,
            pipeline,
            std::span<const veyra::DisplayCommand>(storage),
            scratch,
            stats
        ) == veyra::VulkanRenderResult::insufficient_capacity,
        "paths beyond the scratch bound report capacity"
    );
}

void check_steady_paths_allocate_nothing(
    veyra::VulkanContext& context,
    veyra::VulkanRectPipeline& pipeline,
    std::span<veyra::SoftwarePixel> expected,
    std::span<veyra::SoftwarePixel> actual,
    SoftwareRooms& rooms
) {
    const std::array<veyra::SoftwarePathSegment, 2> segments{
        veyra::SoftwareQuadraticTo{
            .control = {.x = 48.0F, .y = 8.0F},
            .end = {.x = 80.0F, .y = 32.0F},
        },
        veyra::SoftwareLineTo{.end = {.x = 16.0F, .y = 32.0F}},
    };
    const std::array<veyra::SoftwarePathContour, 1> contours{
        veyra::SoftwarePathContour{
            .start = {.x = 16.0F, .y = 32.0F},
            .segments = segments,
        },
    };
    const std::array<veyra::DisplayCommand, 1> storage{
        veyra::FillPath{
            .contours = contours,
            .color = {.red = 30, .green = 140, .blue = 60, .alpha = 255},
            .fill_rule = veyra::SoftwarePathFillRule::even_odd,
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

            throw std::runtime_error("steady path repeats stay complete");
        }
    }

    g_counting.store(false, std::memory_order_relaxed);

    std::cout << "vulkan path repeats=60 host_allocations="
              << g_allocation_count.load(std::memory_order_relaxed) << '\n';

    require(g_allocation_count.load(std::memory_order_relaxed) == 0, "paths allocate nothing");
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
        "path-capable pipeline comes up"
    );

    auto rooms = SoftwareRooms{};
    auto expected = std::array<veyra::SoftwarePixel, kWidth * kHeight>{};
    auto actual = std::array<veyra::SoftwarePixel, kWidth * kHeight>{};

    check_path_parity(
        context,
        pipeline,
        std::span<veyra::SoftwarePixel>{expected.data(), expected.size()},
        std::span<veyra::SoftwarePixel>{actual.data(), actual.size()},
        rooms
    );

    check_path_skips(
        context,
        pipeline,
        std::span<veyra::SoftwarePixel>{actual.data(), actual.size()}
    );

    check_path_rejections(
        context,
        pipeline,
        std::span<veyra::SoftwarePixel>{actual.data(), actual.size()}
    );

    check_path_capacity(
        context,
        pipeline,
        std::span<veyra::SoftwarePixel>{actual.data(), actual.size()}
    );

    check_steady_paths_allocate_nothing(
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
