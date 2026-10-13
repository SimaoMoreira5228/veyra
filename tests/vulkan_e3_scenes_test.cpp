#include "veyra/render/e3_scenes.hpp"
#include "veyra/render/software_renderer.hpp"
#include "veyra/render/vulkan_context.hpp"
#include "veyra/render/vulkan_rect_pipeline.hpp"

#include "vulkan_parity_helpers.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using veyra::vulkan_parity::check_parity;
using veyra::vulkan_parity::elapsed_ns;
using veyra::vulkan_parity::render_reference;
using veyra::vulkan_parity::require;
using veyra::vulkan_parity::SoftwareRooms;
using veyra::vulkan_parity::TestClock;

constexpr std::uint64_t kE3Seed = 0x9E3779B97F4A7C15ULL;

void check_e3_shadowed_parity() {
    constexpr auto width = veyra::kE3Width;
    constexpr auto height = veyra::kE3Height;

    auto context = veyra::VulkanContext{};

    require(
        veyra::create_vulkan_context(context, width, height) == veyra::VulkanContextResult::ready,
        "wide offscreen target comes up"
    );

    auto pipeline = veyra::VulkanRectPipeline{};

    require(
        veyra::create_vulkan_rect_pipeline(context, pipeline) ==
            veyra::VulkanRenderResult::complete,
        "dual-pipeline rect pipeline comes up"
    );

    auto unified = veyra::VulkanRectPipeline{};

    require(
        veyra::create_vulkan_unified_pipeline(context, unified) ==
            veyra::VulkanRenderResult::complete,
        "unified pipeline comes up"
    );

    const auto needed = veyra::e3_max_commands(veyra::E3Scene::shadowed_boxes_600);
    auto commands = std::vector<veyra::DisplayCommand>(needed);
    auto scratch = veyra::E3Scratch{};

    const auto built = veyra::e3_build_scene(
        veyra::E3Scene::shadowed_boxes_600,
        kE3Seed,
        0,
        std::span<veyra::DisplayCommand>(commands),
        scratch
    );

    require(built.status == veyra::E3BuildStatus::complete, "e3 shadow scene builds");

    auto expected = std::vector<veyra::SoftwarePixel>(width * height, veyra::SoftwarePixel{});
    auto actual = std::vector<veyra::SoftwarePixel>(width * height);
    auto rooms = SoftwareRooms{};

    render_reference(
        std::span<const veyra::DisplayCommand>(commands),
        std::span<veyra::SoftwarePixel>(expected),
        width,
        height,
        rooms
    );

    auto stats = veyra::VulkanRectStats{};

    require(
        veyra::render_vulkan_rects(
            context,
            pipeline,
            std::span<const veyra::DisplayCommand>(commands),
            std::span<veyra::SoftwarePixel>(actual),
            stats
        ) == veyra::VulkanRenderResult::complete,
        "e3 shadow scene renders on the gpu path"
    );
    require(stats.submitted == needed, "every shadow command submits");
    require(stats.rect_draws >= 1, "shadow cards batch through rect draws");
    require(stats.rounded_draws >= 1, "shadow shapes batch through rounded draws");

    check_parity(
        context,
        pipeline,
        std::span<const veyra::DisplayCommand>(commands),
        std::span<veyra::SoftwarePixel>(expected),
        std::span<veyra::SoftwarePixel>(actual),
        "e3-shadowed",
        2
    );

    auto unified_stats = veyra::VulkanRectStats{};
    auto unified_begin = TestClock::now();

    require(
        veyra::render_vulkan_rects(
            context,
            unified,
            std::span<const veyra::DisplayCommand>(commands),
            std::span<veyra::SoftwarePixel>(actual),
            unified_stats
        ) == veyra::VulkanRenderResult::complete,
        "e3 shadow scene renders through the unified pipeline"
    );

    const auto unified_ns = elapsed_ns(unified_begin, TestClock::now());

    require(unified_stats.submitted == needed, "unified submits every shadow command");
    require(unified_stats.rect_draws == 0, "unified draws nothing through the rect pipeline");
    require(unified_stats.rounded_draws <= 6, "unified batches the scene into a handful of draws");
    require(unified_stats.draws < stats.draws, "unified needs fewer draws than specialized runs");

    check_parity(
        context,
        unified,
        std::span<const veyra::DisplayCommand>(commands),
        std::span<veyra::SoftwarePixel>(expected),
        std::span<veyra::SoftwarePixel>(actual),
        "e3-shadowed-unified",
        2
    );

    std::cout << "vulkan unified_ns=" << unified_ns;
    std::cout << " unified_draws=" << unified_stats.draws << '\n';

    veyra::destroy_vulkan_rect_pipeline(context, unified);
    veyra::destroy_vulkan_rect_pipeline(context, pipeline);
    veyra::destroy_vulkan_context(context);
}

void check_e3_overlays_parity() {
    constexpr auto width = veyra::kE3Width;
    constexpr auto height = veyra::kE3Height;

    auto context = veyra::VulkanContext{};

    require(
        veyra::create_vulkan_context(context, width, height) == veyra::VulkanContextResult::ready,
        "wide offscreen target comes up"
    );

    auto pipeline = veyra::VulkanRectPipeline{};

    require(
        veyra::create_vulkan_rect_pipeline(context, pipeline) ==
            veyra::VulkanRenderResult::complete,
        "image-capable pipeline comes up"
    );

    const auto needed = veyra::e3_max_commands(veyra::E3Scene::translucent_overlays);
    auto commands = std::vector<veyra::DisplayCommand>(needed);
    auto scratch = veyra::E3Scratch{};

    const auto built = veyra::e3_build_scene(
        veyra::E3Scene::translucent_overlays,
        kE3Seed,
        0,
        std::span<veyra::DisplayCommand>(commands),
        scratch
    );

    require(built.status == veyra::E3BuildStatus::complete, "e3 overlay scene builds");

    auto expected = std::vector<veyra::SoftwarePixel>(width * height, veyra::SoftwarePixel{});
    auto actual = std::vector<veyra::SoftwarePixel>(width * height);
    auto rooms = SoftwareRooms{};

    render_reference(
        std::span<const veyra::DisplayCommand>(commands),
        std::span<veyra::SoftwarePixel>(expected),
        width,
        height,
        rooms
    );

    auto stats = veyra::VulkanRectStats{};

    require(
        veyra::render_vulkan_rects(
            context,
            pipeline,
            std::span<const veyra::DisplayCommand>(commands),
            std::span<veyra::SoftwarePixel>(actual),
            stats
        ) == veyra::VulkanRenderResult::complete,
        "e3 overlay scene renders on the gpu path"
    );
    require(stats.submitted == needed, "every overlay command submits");
    require(stats.image_draws >= 1, "sampled images draw through the image pipeline");

    check_parity(
        context,
        pipeline,
        std::span<const veyra::DisplayCommand>(commands),
        std::span<veyra::SoftwarePixel>(expected),
        std::span<veyra::SoftwarePixel>(actual),
        "e3-overlays",
        2
    );

    veyra::destroy_vulkan_rect_pipeline(context, pipeline);
    veyra::destroy_vulkan_context(context);
}

void check_e3_rowscroll_parity() {
    constexpr auto width = veyra::kE3Width;
    constexpr auto height = veyra::kE3Height;
    constexpr std::size_t kCulledOverscanCommands = 3;

    auto context = veyra::VulkanContext{};

    require(
        veyra::create_vulkan_context(context, width, height) == veyra::VulkanContextResult::ready,
        "wide offscreen target comes up"
    );

    auto pipeline = veyra::VulkanRectPipeline{};

    require(
        veyra::create_vulkan_rect_pipeline(context, pipeline) ==
            veyra::VulkanRenderResult::complete,
        "mask-capable pipeline comes up"
    );

    const auto needed = veyra::e3_max_commands(veyra::E3Scene::row_scroll_1000);
    auto commands = std::vector<veyra::DisplayCommand>(needed);
    auto scratch = veyra::E3Scratch{};

    const auto built = veyra::e3_build_scene(
        veyra::E3Scene::row_scroll_1000,
        kE3Seed,
        3,
        std::span<veyra::DisplayCommand>(commands),
        scratch
    );

    require(built.status == veyra::E3BuildStatus::complete, "e3 row scene builds");

    auto expected = std::vector<veyra::SoftwarePixel>(width * height, veyra::SoftwarePixel{});
    auto actual = std::vector<veyra::SoftwarePixel>(width * height);
    auto rooms = SoftwareRooms{};

    render_reference(
        std::span<const veyra::DisplayCommand>(commands),
        std::span<veyra::SoftwarePixel>(expected),
        width,
        height,
        rooms
    );

    auto stats = veyra::VulkanRectStats{};

    require(
        veyra::render_vulkan_rects(
            context,
            pipeline,
            std::span<const veyra::DisplayCommand>(commands),
            std::span<veyra::SoftwarePixel>(actual),
            stats
        ) == veyra::VulkanRenderResult::complete,
        "e3 row scene renders on the gpu path"
    );
    require(
        stats.submitted == needed - kCulledOverscanCommands,
        "only the fully offscreen overscan row skips"
    );
    require(stats.mask_draws >= 1, "glyph runs batch through the mask pipeline");

    check_parity(
        context,
        pipeline,
        std::span<const veyra::DisplayCommand>(commands),
        std::span<veyra::SoftwarePixel>(expected),
        std::span<veyra::SoftwarePixel>(actual),
        "e3-rowscroll",
        2
    );

    veyra::destroy_vulkan_rect_pipeline(context, pipeline);
    veyra::destroy_vulkan_context(context);
}

int run() {
    check_e3_shadowed_parity();
    check_e3_overlays_parity();
    check_e3_rowscroll_parity();

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
