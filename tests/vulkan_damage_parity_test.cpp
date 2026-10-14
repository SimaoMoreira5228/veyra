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

void render_damaged_reference(
    std::span<const veyra::DisplayCommand> scene,
    std::span<veyra::SoftwarePixel> pixels,
    std::span<const veyra::LayoutRect> damage,
    SoftwareRooms& rooms
) {
    const auto surface = veyra::SoftwareSurface{
        .width = kWidth,
        .height = kHeight,
        .stride = kWidth,
        .pixels = pixels,
    };

    const auto workspace = veyra::SoftwareRenderWorkspace{
        .clips = rooms.clips,
        .translations = rooms.translations,
        .path_intersections = rooms.path_intersections,
        .path_windings = rooms.path_windings,
    };

    require(
        veyra::render_software(scene, surface, workspace, damage) ==
            veyra::SoftwareRenderResult::complete,
        "damaged software reference renders"
    );
}

void require_pixels_match(
    std::span<const veyra::SoftwarePixel> expected,
    std::span<const veyra::SoftwarePixel> actual,
    std::string_view stage
) {
    require(expected.size() == actual.size(), "damage compares equal extents");

    for (auto i = std::size_t{}; i < expected.size(); ++i) {
        const auto& want = expected[i];
        const auto& got = actual[i];

        if (want.red != got.red || want.green != got.green || want.blue != got.blue ||
            want.alpha != got.alpha) {
            std::cout << "damage mismatch x=" << (i % kWidth) << " y=" << (i / kWidth)
                      << " want=" << static_cast<int>(want.red) << ','
                      << static_cast<int>(want.green) << ',' << static_cast<int>(want.blue) << ','
                      << static_cast<int>(want.alpha) << " got=" << static_cast<int>(got.red) << ','
                      << static_cast<int>(got.green) << ',' << static_cast<int>(got.blue) << ','
                      << static_cast<int>(got.alpha) << '\n';

            throw std::runtime_error(std::string(stage) + " pixels match exactly");
        }
    }

    std::cout << "damage stage=" << stage << " pixels match exactly\n";
}

void check_partial_damage_parity(
    veyra::VulkanContext& context,
    veyra::VulkanRectPipeline& pipeline,
    std::span<veyra::SoftwarePixel> expected,
    std::span<veyra::SoftwarePixel> actual,
    SoftwareRooms& rooms
) {
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

    const std::array<veyra::DisplayCommand, 9> storage{
        veyra::FillRect{
            .bounds = {.width = static_cast<float>(kWidth), .height = static_cast<float>(kHeight)},
            .color = {.red = 240, .green = 240, .blue = 244, .alpha = 255},
        },
        veyra::FillRoundedRect{
            .bounds = {.x = 8.0F, .y = 8.0F, .width = 32.0F, .height = 24.0F},
            .radius = 6.0F,
            .color = {.red = 230, .green = 120, .blue = 20, .alpha = 255},
        },
        veyra::PushClip{.bounds = {.x = 48.0F, .y = 8.0F, .width = 40.0F, .height = 32.0F}},
        veyra::FillPath{
            .contours = triangle_contours,
            .color = {.red = 150, .green = 40, .blue = 170, .alpha = 255},
            .fill_rule = veyra::SoftwarePathFillRule::even_odd,
        },
        veyra::StrokeLine{
            .start = {.x = 0.0F, .y = 60.0F},
            .end = {.x = 96.0F, .y = 60.0F},
            .thickness = 3.0F,
            .color = {.red = 20, .green = 20, .blue = 24, .alpha = 255},
        },
        veyra::PopClip{},
        veyra::FillRect{
            .bounds = {.x = 8.0F, .y = 52.0F, .width = 24.0F, .height = 12.0F},
            .color = {.red = 30, .green = 140, .blue = 60, .alpha = 255},
        },
        veyra::StrokeRoundedRect{
            .bounds = {.x = 56.0F, .y = 44.0F, .width = 32.0F, .height = 20.0F},
            .radius = 5.0F,
            .thickness = 2.0F,
            .color = {.red = 230, .green = 180, .blue = 20, .alpha = 255},
        },
        veyra::FillRect{
            .bounds = {.x = 0.0F, .y = 0.0F, .width = 8.0F, .height = 8.0F},
            .color = {.red = 200, .green = 40, .blue = 40, .alpha = 255},
        },
    };

    const auto scene = std::span<const veyra::DisplayCommand>{storage.data(), storage.size()};

    const std::array<veyra::LayoutRect, 2> damage{
        veyra::LayoutRect{.x = 4.0F, .y = 4.0F, .width = 40.0F, .height = 32.0F},
        veyra::LayoutRect{.x = 52.0F, .y = 40.0F, .width = 40.0F, .height = 28.0F},
    };
    const auto damaged = std::span<const veyra::LayoutRect>{damage.data(), damage.size()};

    render_reference(scene, expected, kWidth, kHeight, rooms);

    auto stats = veyra::VulkanRectStats{};

    require(
        veyra::render_vulkan_rects(
            context,
            pipeline,
            scene,
            std::span<veyra::SoftwarePixel>(actual),
            stats
        ) == veyra::VulkanRenderResult::complete,
        "sync full repaint completes"
    );
    require(expected[2 * kWidth + 50].green == 240, "undamaged pixels keep the full repaint");

    render_damaged_reference(scene, expected, damaged, rooms);

    require(
        veyra::render_vulkan_rects(
            context,
            pipeline,
            scene,
            std::span<veyra::SoftwarePixel>(actual),
            stats,
            damaged
        ) == veyra::VulkanRenderResult::complete,
        "damaged scene renders on the gpu path"
    );
    require(stats.submitted < 60, "culled primitives never submit");
    require(stats.submitted > 0, "damaged primitives still submit");

    check_parity(context, pipeline, scene, expected, actual, "damage-partial", 1, damaged);
}

void check_empty_damage_repaints_everything(
    veyra::VulkanContext& context,
    veyra::VulkanRectPipeline& pipeline,
    std::span<veyra::SoftwarePixel> expected,
    std::span<veyra::SoftwarePixel> actual,
    SoftwareRooms& rooms
) {
    const std::array<veyra::DisplayCommand, 1> storage{
        veyra::FillRect{
            .bounds = {.width = static_cast<float>(kWidth), .height = static_cast<float>(kHeight)},
            .color = {.red = 200, .green = 40, .blue = 40, .alpha = 255},
        },
    };
    const auto scene = std::span<const veyra::DisplayCommand>{storage.data(), storage.size()};

    render_reference(scene, expected, kWidth, kHeight, rooms);

    auto stats = veyra::VulkanRectStats{};

    require(
        veyra::render_vulkan_rects(
            context,
            pipeline,
            scene,
            actual,
            stats,
            std::span<const veyra::LayoutRect>{}
        ) == veyra::VulkanRenderResult::complete,
        "empty damage completes"
    );
    require(stats.submitted == 1, "empty damage repaints everything");

    check_parity(
        context,
        pipeline,
        scene,
        expected,
        actual,
        "damage-empty",
        1,
        std::span<const veyra::LayoutRect>{}
    );
}

void check_distant_damage_skips_everything(
    veyra::VulkanContext& context,
    veyra::VulkanRectPipeline& pipeline,
    std::span<veyra::SoftwarePixel> expected,
    std::span<veyra::SoftwarePixel> actual,
    SoftwareRooms& rooms
) {
    const std::array<veyra::DisplayCommand, 1> storage{
        veyra::FillRect{
            .bounds = {.x = 64.0F, .y = 48.0F, .width = 16.0F, .height = 8.0F},
            .color = {.red = 200, .green = 40, .blue = 40, .alpha = 255},
        },
    };
    const auto scene = std::span<const veyra::DisplayCommand>{storage.data(), storage.size()};

    const std::array<veyra::LayoutRect, 1> damage{
        veyra::LayoutRect{.x = 0.0F, .y = 0.0F, .width = 8.0F, .height = 8.0F},
    };
    const auto damaged = std::span<const veyra::LayoutRect>{damage.data(), damage.size()};

    render_reference(scene, expected, kWidth, kHeight, rooms);

    auto stats = veyra::VulkanRectStats{};

    require(
        veyra::render_vulkan_rects(context, pipeline, scene, actual, stats) ==
            veyra::VulkanRenderResult::complete,
        "sync full repaint completes"
    );
    require(stats.submitted == 1, "sync repaint submits the rect");

    render_damaged_reference(scene, expected, damaged, rooms);

    require(
        veyra::render_vulkan_rects(context, pipeline, scene, actual, stats, damaged) ==
            veyra::VulkanRenderResult::complete,
        "distant damage completes"
    );
    require(stats.submitted == 0, "fully culled damage submits nothing");
    require(stats.draws == 0, "fully culled damage draws nothing");

    require_pixels_match(expected, actual, "damage-culled");
}

void check_damage_rejections(
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

    const std::array<veyra::LayoutRect, 1> negative{
        veyra::LayoutRect{.width = -8.0F, .height = 8.0F},
    };

    require(
        veyra::render_vulkan_rects(
            context,
            pipeline,
            scene,
            scratch,
            stats,
            std::span<const veyra::LayoutRect>(negative)
        ) == veyra::VulkanRenderResult::invalid_input,
        "negative damage is rejected"
    );

    const std::array<veyra::LayoutRect, 2> overlapping{
        veyra::LayoutRect{.x = 0.0F, .y = 0.0F, .width = 16.0F, .height = 16.0F},
        veyra::LayoutRect{.x = 8.0F, .y = 8.0F, .width = 16.0F, .height = 16.0F},
    };

    require(
        veyra::render_vulkan_rects(
            context,
            pipeline,
            scene,
            scratch,
            stats,
            std::span<const veyra::LayoutRect>(overlapping)
        ) == veyra::VulkanRenderResult::invalid_input,
        "overlapping damage is rejected"
    );
}

void check_steady_damage_allocate_nothing(
    veyra::VulkanContext& context,
    veyra::VulkanRectPipeline& pipeline,
    std::span<veyra::SoftwarePixel> expected,
    std::span<veyra::SoftwarePixel> actual,
    SoftwareRooms& rooms
) {
    const std::array<veyra::DisplayCommand, 2> storage{
        veyra::FillRect{
            .bounds = {.width = static_cast<float>(kWidth), .height = static_cast<float>(kHeight)},
            .color = {.red = 240, .green = 240, .blue = 244, .alpha = 255},
        },
        veyra::StrokeLine{
            .start = {.x = 0.0F, .y = 60.0F},
            .end = {.x = 96.0F, .y = 60.0F},
            .thickness = 3.0F,
            .color = {.red = 20, .green = 20, .blue = 24, .alpha = 255},
        },
    };
    const auto scene = std::span<const veyra::DisplayCommand>{storage.data(), storage.size()};

    const std::array<veyra::LayoutRect, 1> damage{
        veyra::LayoutRect{.x = 32.0F, .y = 52.0F, .width = 32.0F, .height = 16.0F},
    };
    const auto damaged = std::span<const veyra::LayoutRect>{damage.data(), damage.size()};

    render_reference(scene, expected, kWidth, kHeight, rooms);

    auto stats = veyra::VulkanRectStats{};

    require(
        veyra::render_vulkan_rects(
            context,
            pipeline,
            scene,
            std::span<veyra::SoftwarePixel>(actual),
            stats
        ) == veyra::VulkanRenderResult::complete,
        "sync full repaint completes"
    );

    render_damaged_reference(scene, expected, damaged, rooms);

    require(
        veyra::render_vulkan_rects(
            context,
            pipeline,
            scene,
            std::span<veyra::SoftwarePixel>(actual),
            stats,
            damaged
        ) == veyra::VulkanRenderResult::complete,
        "steady window opens clean"
    );

    check_parity(context, pipeline, scene, expected, actual, "damage-steady", 1, damaged);

    g_allocation_count.store(0, std::memory_order_relaxed);
    g_counting.store(true, std::memory_order_relaxed);

    for (auto repeat = std::size_t{}; repeat < 60; ++repeat) {
        const auto result = veyra::render_vulkan_rects(
            context,
            pipeline,
            scene,
            std::span<veyra::SoftwarePixel>(actual),
            stats,
            damaged
        );

        if (result != veyra::VulkanRenderResult::complete) {
            g_counting.store(false, std::memory_order_relaxed);

            throw std::runtime_error("steady damage repeats stay complete");
        }
    }

    g_counting.store(false, std::memory_order_relaxed);

    std::cout << "vulkan damage repeats=60 host_allocations="
              << g_allocation_count.load(std::memory_order_relaxed) << '\n';

    require(
        g_allocation_count.load(std::memory_order_relaxed) == 0,
        "damage draws allocate nothing"
    );
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
        "damage-capable pipeline comes up"
    );

    auto rooms = SoftwareRooms{};
    auto expected = std::array<veyra::SoftwarePixel, kWidth * kHeight>{};
    auto actual = std::array<veyra::SoftwarePixel, kWidth * kHeight>{};

    check_partial_damage_parity(
        context,
        pipeline,
        std::span<veyra::SoftwarePixel>{expected.data(), expected.size()},
        std::span<veyra::SoftwarePixel>{actual.data(), actual.size()},
        rooms
    );

    check_empty_damage_repaints_everything(
        context,
        pipeline,
        std::span<veyra::SoftwarePixel>{expected.data(), expected.size()},
        std::span<veyra::SoftwarePixel>{actual.data(), actual.size()},
        rooms
    );

    check_distant_damage_skips_everything(
        context,
        pipeline,
        std::span<veyra::SoftwarePixel>{expected.data(), expected.size()},
        std::span<veyra::SoftwarePixel>{actual.data(), actual.size()},
        rooms
    );

    check_damage_rejections(
        context,
        pipeline,
        std::span<veyra::SoftwarePixel>{actual.data(), actual.size()}
    );

    check_steady_damage_allocate_nothing(
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
