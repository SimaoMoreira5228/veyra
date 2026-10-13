#include "veyra/render/software_renderer.hpp"
#include "veyra/render/vulkan_context.hpp"
#include "veyra/render/vulkan_rect_pipeline.hpp"

#include "vulkan_parity_helpers.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <new>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

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

constexpr std::size_t kWidth = 96;
constexpr std::size_t kHeight = 72;
constexpr std::size_t kSmallWidth = 64;
constexpr std::size_t kSmallHeight = 48;
constexpr std::size_t kPixels = kWidth * kHeight;

using veyra::vulkan_parity::check_parity;
using veyra::vulkan_parity::elapsed_ns;
using veyra::vulkan_parity::render_reference;
using veyra::vulkan_parity::require;
using veyra::vulkan_parity::SoftwareRooms;
using veyra::vulkan_parity::TestClock;

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
    require(stats.submitted == 2, "translucent rects submit instead of skipping");
    require(stats.draws == 1, "one draw covers the pair");

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
        stroke_result == veyra::VulkanRenderResult::complete,
        "rect strokes lower to fills instead of rejecting"
    );
    require(stats.submitted == 4, "an 8x8 outline lowers to four bars");

    const std::array<veyra::DisplayCommand, 1> pathed{
        veyra::FillPath{
            .contours = {},
            .color = {.red = 255, .alpha = 255},
            .fill_rule = veyra::SoftwarePathFillRule::even_odd,
        },
    };

    const auto path_result = veyra::render_vulkan_rects(context, pipeline, pathed, scratch, stats);

    require(
        path_result == veyra::VulkanRenderResult::unsupported_commands,
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

void check_chunked_translucent(
    veyra::VulkanContext& context,
    veyra::VulkanRectPipeline& pipeline,
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

    auto stats = veyra::VulkanRectStats{};

    require(
        veyra::render_vulkan_rects(context, pipeline, scene, actual, stats) ==
            veyra::VulkanRenderResult::complete,
        "oversized translucent scene completes"
    );
    require(stats.submitted == chunk_commands, "every rect submits across chunks");
    require(stats.draws == 2, "two draws cover three hundred rects");

    check_parity(context, pipeline, scene, expected, actual, "chunked", 1);
}

void check_full_budget(
    veyra::VulkanContext& context,
    veyra::VulkanRectPipeline& pipeline,
    std::span<veyra::SoftwarePixel> expected,
    std::span<veyra::SoftwarePixel> actual,
    SoftwareRooms& rooms
) {
    constexpr auto budget = veyra::kVulkanMaxRects * veyra::kVulkanMaxRectDraws;

    auto storage = std::vector<veyra::DisplayCommand>(budget);

    for (auto index = std::size_t{}; index < budget; ++index) {
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
                .alpha = 255,
            },
        };
    }

    const auto scene = std::span<const veyra::DisplayCommand>{storage.data(), storage.size()};

    render_reference(scene, expected, kWidth, kHeight, rooms);

    auto stats = veyra::VulkanRectStats{};

    require(
        veyra::render_vulkan_rects(context, pipeline, scene, actual, stats) ==
            veyra::VulkanRenderResult::complete,
        "a frame exactly at budget completes"
    );
    require(stats.submitted == budget, "budget frames submit every rect");
    require(stats.draws == veyra::kVulkanMaxRectDraws, "budget frames fill every draw");

    check_parity(context, pipeline, scene, expected, actual, "budget", 0);
}

void check_rounded_parity(
    veyra::VulkanContext& context,
    veyra::VulkanRectPipeline& pipeline,
    std::span<veyra::SoftwarePixel> expected,
    std::span<veyra::SoftwarePixel> actual,
    SoftwareRooms& rooms
) {
    const std::array<veyra::DisplayCommand, 6> storage{
        veyra::FillRect{
            .bounds = {.width = static_cast<float>(kWidth), .height = static_cast<float>(kHeight)},
            .color = {.red = 240, .green = 240, .blue = 244, .alpha = 255},
        },
        veyra::FillRoundedRect{
            .bounds = {.x = 8.0F, .y = 10.0F, .width = 40.0F, .height = 30.0F},
            .radius = 8.0F,
            .color = {.red = 30, .green = 140, .blue = 60, .alpha = 255},
        },
        veyra::FillRect{
            .bounds = {.x = 24.0F, .y = 20.0F, .width = 50.0F, .height = 24.0F},
            .color = {.red = 210, .green = 60, .blue = 40, .alpha = 160},
        },
        veyra::FillRoundedRect{
            .bounds = {.x = 40.0F, .y = 32.0F, .width = 44.0F, .height = 28.0F},
            .radius = 14.0F,
            .color = {.red = 40, .green = 70, .blue = 210, .alpha = 128},
        },
        veyra::FillRoundedRect{
            .bounds = {.x = 4.0F, .y = 50.0F, .width = 60.0F, .height = 16.0F},
            .radius = 0.0F,
            .color = {.red = 230, .green = 180, .blue = 20, .alpha = 255},
        },
        veyra::FillRoundedRect{
            .bounds = {.x = 60.0F, .y = 4.0F, .width = 30.0F, .height = 20.0F},
            .radius = 40.0F,
            .color = {.red = 150, .green = 40, .blue = 170, .alpha = 255},
        },
    };

    const auto scene = std::span<const veyra::DisplayCommand>{storage.data(), storage.size()};

    render_reference(scene, expected, kWidth, kHeight, rooms);

    auto stats = veyra::VulkanRectStats{};

    require(
        veyra::render_vulkan_rects(context, pipeline, scene, actual, stats) ==
            veyra::VulkanRenderResult::complete,
        "mixed rect and rounded scene completes"
    );
    require(stats.submitted == 6, "every rect submits");
    require(stats.rect_draws >= 1, "rects draw through the rect pipeline");
    require(stats.rounded_draws >= 1, "rounded rects draw through the rounded pipeline");

    check_parity(context, pipeline, scene, expected, actual, "rounded", 1);
}

void check_unified_strokes(
    veyra::VulkanContext& context,
    std::span<veyra::SoftwarePixel> expected,
    std::span<veyra::SoftwarePixel> actual,
    SoftwareRooms& rooms
) {
    auto unified = veyra::VulkanRectPipeline{};

    require(
        veyra::create_vulkan_unified_pipeline(context, unified) ==
            veyra::VulkanRenderResult::complete,
        "unified pipeline comes up for strokes"
    );

    const std::array<veyra::DisplayCommand, 2> storage{
        veyra::FillRect{
            .bounds = {.x = 8.0F, .y = 8.0F, .width = 32.0F, .height = 24.0F},
            .color = {.red = 30, .green = 140, .blue = 60, .alpha = 255},
        },
        veyra::StrokeRect{
            .bounds = {.x = 8.0F, .y = 40.0F, .width = 32.0F, .height = 16.0F},
            .thickness = 2.0F,
            .color = {.red = 210, .green = 60, .blue = 40, .alpha = 255},
        },
    };

    const auto scene = std::span<const veyra::DisplayCommand>{storage.data(), storage.size()};

    render_reference(scene, expected, kWidth, kHeight, rooms);

    auto stats = veyra::VulkanRectStats{};

    require(
        veyra::render_vulkan_rects(context, unified, scene, actual, stats) ==
            veyra::VulkanRenderResult::complete,
        "strokes complete through the unified pipeline"
    );
    require(stats.rect_draws >= 1, "unified strokes batch through the rect pipe");
    require(stats.rounded_draws >= 1, "unified fills batch through the rounded pipe");

    check_parity(context, unified, scene, expected, actual, "unified-stroke", 1);

    veyra::destroy_vulkan_rect_pipeline(context, unified);
}

void check_unified_integer_parity(
    veyra::VulkanContext& context,
    std::span<veyra::SoftwarePixel> expected,
    std::span<veyra::SoftwarePixel> actual,
    SoftwareRooms& rooms
) {
    auto unified = veyra::VulkanRectPipeline{};

    require(
        veyra::create_vulkan_unified_pipeline(context, unified) ==
            veyra::VulkanRenderResult::complete,
        "unified pipeline comes up for plain rects"
    );

    const std::array<veyra::DisplayCommand, 3> storage{
        veyra::FillRect{
            .bounds = {.width = static_cast<float>(kWidth), .height = static_cast<float>(kHeight)},
            .color = {.red = 240, .green = 240, .blue = 244, .alpha = 255},
        },
        veyra::FillRect{
            .bounds = {.x = 10.0F, .y = 12.0F, .width = 40.0F, .height = 30.0F},
            .color = {.red = 30, .green = 140, .blue = 60, .alpha = 200},
        },
        veyra::FillRect{
            .bounds = {.x = -4.0F, .y = 50.0F, .width = 40.0F, .height = 30.0F},
            .color = {.red = 210, .green = 60, .blue = 40, .alpha = 255},
        },
    };

    const auto scene = std::span<const veyra::DisplayCommand>{storage.data(), storage.size()};

    render_reference(scene, expected, kWidth, kHeight, rooms);

    auto stats = veyra::VulkanRectStats{};

    require(
        veyra::render_vulkan_rects(context, unified, scene, actual, stats) ==
            veyra::VulkanRenderResult::complete,
        "integer rects complete through the unified pipeline"
    );
    require(stats.rect_draws == 0, "unified draws no plain rects");

    check_parity(context, unified, scene, expected, actual, "unified-integer", 1);

    veyra::destroy_vulkan_rect_pipeline(context, unified);
}

void check_stroke_parity(
    veyra::VulkanContext& context,
    veyra::VulkanRectPipeline& pipeline,
    std::span<veyra::SoftwarePixel> expected,
    std::span<veyra::SoftwarePixel> actual,
    SoftwareRooms& rooms
) {
    const std::array<veyra::DisplayCommand, 6> storage{
        veyra::FillRect{
            .bounds = {.width = static_cast<float>(kWidth), .height = static_cast<float>(kHeight)},
            .color = {.red = 240, .green = 240, .blue = 244, .alpha = 255},
        },
        veyra::StrokeRect{
            .bounds = {.x = 8.0F, .y = 10.0F, .width = 40.0F, .height = 24.0F},
            .thickness = 3.0F,
            .color = {.red = 30, .green = 140, .blue = 60, .alpha = 255},
        },
        veyra::StrokeRect{
            .bounds = {.x = 20.0F, .y = 20.0F, .width = 50.0F, .height = 24.0F},
            .thickness = 2.0F,
            .color = {.red = 210, .green = 60, .blue = 40, .alpha = 160},
        },
        veyra::StrokeRect{
            .bounds = {.x = -4.0F, .y = 44.0F, .width = 24.0F, .height = 20.0F},
            .thickness = 10.0F,
            .color = {.red = 40, .green = 70, .blue = 210, .alpha = 255},
        },
        veyra::StrokeRect{
            .bounds = {.x = 60.0F, .y = 8.0F, .width = 28.0F, .height = 16.0F},
            .thickness = 20.0F,
            .color = {.red = 150, .green = 40, .blue = 170, .alpha = 255},
        },
        veyra::StrokeRect{
            .bounds = {.x = 50.0F, .y = 50.0F, .width = 12.0F, .height = 12.0F},
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
        "stroked rects lower to fills"
    );

    check_parity(context, pipeline, scene, expected, actual, "stroke", 1);

    const std::array<veyra::DisplayCommand, 1> negative{
        veyra::StrokeRect{
            .bounds = {.width = 8.0F, .height = 8.0F},
            .thickness = -1.0F,
            .color = {.red = 255, .alpha = 255},
        },
    };

    require(
        veyra::render_vulkan_rects(
            context,
            pipeline,
            std::span<const veyra::DisplayCommand>(negative),
            actual,
            stats
        ) == veyra::VulkanRenderResult::invalid_input,
        "negative thickness is rejected"
    );
}

void check_stroke_edges(
    veyra::VulkanContext& context,
    veyra::VulkanRectPipeline& pipeline,
    std::span<veyra::SoftwarePixel> expected,
    std::span<veyra::SoftwarePixel> actual,
    SoftwareRooms& rooms
) {
    const auto nan = std::numeric_limits<float>::quiet_NaN();
    const auto inf = std::numeric_limits<float>::infinity();

    const std::array<veyra::DisplayCommand, 8> storage{
        veyra::FillRect{
            .bounds = {.width = static_cast<float>(kWidth), .height = static_cast<float>(kHeight)},
            .color = {.red = 240, .green = 240, .blue = 244, .alpha = 255},
        },
        veyra::StrokeRect{
            .bounds = {.x = 10.5F, .y = 10.0F, .width = 30.0F, .height = 20.0F},
            .thickness = 3.0F,
            .color = {.red = 30, .green = 140, .blue = 60, .alpha = 255},
        },
        veyra::PushTranslation{.offset = {.x = 4.25F, .y = -3.5F}},
        veyra::StrokeRect{
            .bounds = {.x = 40.0F, .y = 30.0F, .width = 24.0F, .height = 16.0F},
            .thickness = 2.0F,
            .color = {.red = 210, .green = 60, .blue = 40, .alpha = 200},
        },
        veyra::PopTranslation{},
        veyra::StrokeRect{
            .bounds = {.x = 60.0F, .y = 50.0F, .width = 12.0F, .height = 12.0F},
            .thickness = 1.0F,
            .color = {.red = 150, .green = 40, .blue = 170, .alpha = 0},
        },
        veyra::StrokeRect{
            .bounds = {.width = 0.0F, .height = 8.0F},
            .thickness = 1.0F,
            .color = {.red = 150, .green = 40, .blue = 170, .alpha = 255},
        },
        veyra::StrokeRect{
            .bounds = {.x = 70.0F, .y = 50.0F, .width = 0.0F, .height = 0.0F},
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
        "fractional, translated, and empty strokes complete"
    );

    check_parity(context, pipeline, scene, expected, actual, "stroke-edge", 1);

    const std::array<veyra::DisplayCommand, 1> negative{
        veyra::StrokeRect{
            .bounds = {.width = 8.0F, .height = 8.0F},
            .thickness = -1.0F,
            .color = {.red = 255, .alpha = 255},
        },
    };

    require(
        veyra::render_vulkan_rects(
            context,
            pipeline,
            std::span<const veyra::DisplayCommand>(negative),
            actual,
            stats
        ) == veyra::VulkanRenderResult::invalid_input,
        "negative thickness is rejected"
    );

    const std::array<veyra::DisplayCommand, 1> not_a_number{
        veyra::StrokeRect{
            .bounds = {.width = 8.0F, .height = 8.0F},
            .thickness = nan,
            .color = {.red = 255, .alpha = 255},
        },
    };

    require(
        veyra::render_vulkan_rects(
            context,
            pipeline,
            std::span<const veyra::DisplayCommand>(not_a_number),
            actual,
            stats
        ) == veyra::VulkanRenderResult::invalid_input,
        "NaN thickness is rejected"
    );

    const std::array<veyra::DisplayCommand, 1> infinite{
        veyra::StrokeRect{
            .bounds = {.width = 8.0F, .height = 8.0F},
            .thickness = inf,
            .color = {.red = 255, .alpha = 255},
        },
    };

    require(
        veyra::render_vulkan_rects(
            context,
            pipeline,
            std::span<const veyra::DisplayCommand>(infinite),
            actual,
            stats
        ) == veyra::VulkanRenderResult::invalid_input,
        "infinite thickness is rejected"
    );
}

void check_clipped_rounded_parity(
    veyra::VulkanContext& context,
    veyra::VulkanRectPipeline& pipeline,
    std::span<veyra::SoftwarePixel> expected,
    std::span<veyra::SoftwarePixel> actual,
    SoftwareRooms& rooms
) {
    const std::array<veyra::DisplayCommand, 4> storage{
        veyra::FillRect{
            .bounds = {.width = static_cast<float>(kWidth), .height = static_cast<float>(kHeight)},
            .color = {.red = 240, .green = 240, .blue = 244, .alpha = 255},
        },
        veyra::FillRoundedRect{
            .bounds = {.x = 10.5F, .y = 12.0F, .width = 40.0F, .height = 30.0F},
            .radius = 8.0F,
            .color = {.red = 30, .green = 140, .blue = 60, .alpha = 255},
        },
        veyra::FillRoundedRect{
            .bounds = {.x = -8.0F, .y = 56.0F, .width = 40.0F, .height = 30.0F},
            .radius = 10.0F,
            .color = {.red = 210, .green = 60, .blue = 40, .alpha = 200},
        },
        veyra::FillRoundedRect{
            .bounds = {.x = 70.0F, .y = 60.0F, .width = 40.0F, .height = 30.0F},
            .radius = 10.0F,
            .color = {.red = 40, .green = 70, .blue = 210, .alpha = 200},
        },
    };

    const auto scene = std::span<const veyra::DisplayCommand>{storage.data(), storage.size()};

    render_reference(scene, expected, kWidth, kHeight, rooms);

    auto stats = veyra::VulkanRectStats{};

    require(
        veyra::render_vulkan_rects(context, pipeline, scene, actual, stats) ==
            veyra::VulkanRenderResult::complete,
        "fractional and clipped rounded rects complete"
    );

    check_parity(context, pipeline, scene, expected, actual, "rounded-clipped", 2);
}

void check_stroke_capacity(
    veyra::VulkanContext& context,
    veyra::VulkanRectPipeline& pipeline,
    std::span<veyra::SoftwarePixel> scratch
) {
    auto storage =
        std::vector<veyra::DisplayCommand>(veyra::kVulkanMaxRects * veyra::kVulkanMaxRectDraws);

    for (auto& slot : storage) {
        slot = veyra::FillRect{
            .bounds = {.width = 1.0F, .height = 1.0F},
            .color = {.red = 200, .alpha = 255},
        };
    }

    storage.back() = veyra::StrokeRect{
        .bounds = {.width = 8.0F, .height = 8.0F},
        .thickness = 1.0F,
        .color = {.red = 200, .alpha = 255},
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
        "stroke expansion past the budget reports instead of truncating"
    );

    auto expected = std::array<veyra::SoftwarePixel, kWidth * kHeight>{};
    auto actual = std::array<veyra::SoftwarePixel, kWidth * kHeight>{};
    auto rooms = SoftwareRooms{};

    auto split_storage = std::vector<veyra::DisplayCommand>(254);

    for (auto& slot : split_storage) {
        slot = veyra::FillRect{
            .bounds = {.width = 1.0F, .height = 1.0F},
            .color = {.red = 200, .alpha = 255},
        };
    }

    split_storage.push_back(
        veyra::StrokeRect{
            .bounds = {.x = 8.0F, .y = 8.0F, .width = 8.0F, .height = 8.0F},
            .thickness = 2.0F,
            .color = {.red = 200, .alpha = 255},
        }
    );

    const auto split_scene = std::span<const veyra::DisplayCommand>(split_storage);

    render_reference(split_scene, expected, kWidth, kHeight, rooms);

    require(
        veyra::render_vulkan_rects(context, pipeline, split_scene, actual, stats) ==
            veyra::VulkanRenderResult::complete,
        "a stroke straddling the chunk boundary completes"
    );
    require(stats.submitted == 258, "fills plus lowered bars all submit");
    require(stats.draws == 2, "the chunk boundary splits into two draws");

    check_parity(context, pipeline, split_scene, expected, actual, "stroke-chunked", 0);
}

void check_capacity_limit(
    veyra::VulkanContext& context,
    veyra::VulkanRectPipeline& pipeline,
    std::span<veyra::SoftwarePixel> scratch
) {
    auto storage =
        std::vector<veyra::DisplayCommand>(veyra::kVulkanMaxRects * veyra::kVulkanMaxRectDraws + 1);

    for (auto& slot : storage) {
        slot = veyra::FillRect{
            .bounds = {.width = 8.0F, .height = 8.0F},
            .color = {.red = 200, .alpha = 255},
        };
    }

    auto stats = veyra::VulkanRectStats{};

    require(
        veyra::render_vulkan_rects(
            context,
            pipeline,
            std::span<const veyra::DisplayCommand>(storage),
            scratch,
            stats
        ) == veyra::VulkanRenderResult::insufficient_capacity,
        "frames beyond the instance budget are rejected, not clipped"
    );

    auto scene_storage = std::array<veyra::DisplayCommand, kSceneCommands>{};
    const auto scene = build_scene(scene_storage);

    require(
        veyra::render_vulkan_rects(context, pipeline, scene, scratch, stats) ==
            veyra::VulkanRenderResult::complete,
        "the pipeline stays usable after a rejection"
    );
    require(stats.submitted == 6, "recovery submits the visible scene rects");
    require(stats.draws == 1, "recovery draws in a single batch");
}

void check_translated_bounds(
    veyra::VulkanContext& context,
    veyra::VulkanRectPipeline& pipeline,
    std::span<veyra::SoftwarePixel> scratch,
    SoftwareRooms& rooms
) {
    auto stats = veyra::VulkanRectStats{};
    const auto nan = std::numeric_limits<float>::quiet_NaN();

    const std::array<veyra::DisplayCommand, 2> nan_offset{
        veyra::PushTranslation{.offset = {.x = nan, .y = 0.0F}},
        veyra::FillRect{
            .bounds = {.width = 8.0F, .height = 8.0F},
            .color = {.red = 255, .alpha = 255},
        },
    };

    require(
        veyra::render_vulkan_rects(
            context,
            pipeline,
            std::span<const veyra::DisplayCommand>(nan_offset),
            scratch,
            stats
        ) == veyra::VulkanRenderResult::invalid_input,
        "NaN translation offsets are rejected"
    );

    const auto huge = std::numeric_limits<float>::max();

    const std::array<veyra::DisplayCommand, 2> overflow{
        veyra::PushTranslation{.offset = {.x = huge, .y = 0.0F}},
        veyra::FillRect{
            .bounds = {.x = huge, .width = 8.0F, .height = 8.0F},
            .color = {.red = 255, .alpha = 255},
        },
    };

    require(
        veyra::render_vulkan_rects(
            context,
            pipeline,
            std::span<const veyra::DisplayCommand>(overflow),
            scratch,
            stats
        ) == veyra::VulkanRenderResult::invalid_input,
        "overflowing translations are rejected"
    );

    auto expected = std::array<veyra::SoftwarePixel, kWidth * kHeight>{};
    auto actual = std::array<veyra::SoftwarePixel, kWidth * kHeight>{};

    const std::array<veyra::DisplayCommand, 3> shifted{
        veyra::PushTranslation{.offset = {.x = 4.0F, .y = 4.0F}},
        veyra::FillRect{
            .bounds = {.width = 8.0F, .height = 8.0F},
            .color = {.red = 255, .alpha = 255},
        },
        veyra::PopTranslation{},
    };

    const auto shifted_span =
        std::span<const veyra::DisplayCommand>{shifted.data(), shifted.size()};

    render_reference(shifted_span, expected, kWidth, kHeight, rooms);

    require(
        veyra::render_vulkan_rects(context, pipeline, shifted_span, actual, stats) ==
            veyra::VulkanRenderResult::complete,
        "finite translations render"
    );

    check_parity(context, pipeline, shifted_span, expected, actual, "translated", 0);
}

void check_frame_boundaries(
    veyra::VulkanContext& context,
    veyra::VulkanRectPipeline& pipeline,
    std::span<veyra::SoftwarePixel> scratch
) {
    const std::array<veyra::DisplayCommand, 0> empty{};

    auto stats = veyra::VulkanRectStats{};

    require(
        veyra::render_vulkan_rects(
            context,
            pipeline,
            std::span<const veyra::DisplayCommand>{empty.data(), empty.size()},
            scratch,
            stats
        ) == veyra::VulkanRenderResult::complete,
        "empty scenes clear instead of failing"
    );
    require(stats.submitted == 0, "empty scenes submit nothing");
    require(stats.draws == 0, "empty scenes draw nothing");

    for (const auto& pixel : scratch) {
        require(
            pixel.red == 0 && pixel.green == 0 && pixel.blue == 0 && pixel.alpha == 255,
            "cleared pixels match the clear color"
        );
    }

    const std::array<veyra::DisplayCommand, 1> unbalanced{
        veyra::PushTranslation{.offset = {.x = 1.0F}},
    };

    require(
        veyra::render_vulkan_rects(context, pipeline, unbalanced, scratch, stats) ==
            veyra::VulkanRenderResult::invalid_input,
        "unbalanced translation is rejected"
    );

    const std::array<veyra::DisplayCommand, 1> popped{
        veyra::PopTranslation{},
    };

    require(
        veyra::render_vulkan_rects(context, pipeline, popped, scratch, stats) ==
            veyra::VulkanRenderResult::invalid_input,
        "popping an empty translation stack is rejected"
    );

    auto undersized = std::span<veyra::SoftwarePixel>{scratch.data(), scratch.size() - 1};

    auto scene_storage = std::array<veyra::DisplayCommand, kSceneCommands>{};
    const auto scene = build_scene(scene_storage);

    require(
        veyra::render_vulkan_rects(context, pipeline, scene, undersized, stats) ==
            veyra::VulkanRenderResult::invalid_input,
        "output extents must match the target"
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
        "full",
        0
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

    check_rounded_parity(
        context,
        pipeline,
        std::span<veyra::SoftwarePixel>{expected.data(), kWidth * kHeight},
        std::span<veyra::SoftwarePixel>{actual.data(), kWidth * kHeight},
        rooms
    );

    check_clipped_rounded_parity(
        context,
        pipeline,
        std::span<veyra::SoftwarePixel>{expected.data(), kWidth * kHeight},
        std::span<veyra::SoftwarePixel>{actual.data(), kWidth * kHeight},
        rooms
    );

    check_stroke_parity(
        context,
        pipeline,
        std::span<veyra::SoftwarePixel>{expected.data(), kWidth * kHeight},
        std::span<veyra::SoftwarePixel>{actual.data(), kWidth * kHeight},
        rooms
    );

    check_stroke_edges(
        context,
        pipeline,
        std::span<veyra::SoftwarePixel>{expected.data(), kWidth * kHeight},
        std::span<veyra::SoftwarePixel>{actual.data(), kWidth * kHeight},
        rooms
    );

    check_unified_integer_parity(
        context,
        std::span<veyra::SoftwarePixel>{expected.data(), kWidth * kHeight},
        std::span<veyra::SoftwarePixel>{actual.data(), kWidth * kHeight},
        rooms
    );

    check_unified_strokes(
        context,
        std::span<veyra::SoftwarePixel>{expected.data(), kWidth * kHeight},
        std::span<veyra::SoftwarePixel>{actual.data(), kWidth * kHeight},
        rooms
    );

    check_chunked_translucent(
        context,
        pipeline,
        std::span<veyra::SoftwarePixel>{expected.data(), kWidth * kHeight},
        std::span<veyra::SoftwarePixel>{actual.data(), kWidth * kHeight},
        rooms
    );

    check_capacity_limit(
        context,
        pipeline,
        std::span<veyra::SoftwarePixel>{actual.data(), kWidth * kHeight}
    );

    check_stroke_capacity(
        context,
        pipeline,
        std::span<veyra::SoftwarePixel>{actual.data(), kWidth * kHeight}
    );

    check_frame_boundaries(
        context,
        pipeline,
        std::span<veyra::SoftwarePixel>{actual.data(), kWidth * kHeight}
    );

    check_translated_bounds(
        context,
        pipeline,
        std::span<veyra::SoftwarePixel>{actual.data(), kWidth * kHeight},
        rooms
    );

    check_full_budget(
        context,
        pipeline,
        std::span<veyra::SoftwarePixel>{expected.data(), kWidth * kHeight},
        std::span<veyra::SoftwarePixel>{actual.data(), kWidth * kHeight},
        rooms
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
        "resized",
        0
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
