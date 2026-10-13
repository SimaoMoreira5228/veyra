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
using veyra::vulkan_parity::render_reference;
using veyra::vulkan_parity::require;
using veyra::vulkan_parity::SoftwareRooms;

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

    veyra::destroy_vulkan_rect_pipeline(context, pipeline);
    veyra::destroy_vulkan_context(context);
}

int run() {
    check_e3_shadowed_parity();

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
