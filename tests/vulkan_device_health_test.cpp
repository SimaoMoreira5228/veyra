#include "veyra/render/software_renderer.hpp"
#include "veyra/render/vulkan_context.hpp"
#include "veyra/render/vulkan_rect_pipeline.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

constexpr std::size_t kWidth = 64;
constexpr std::size_t kHeight = 48;

void check_healthy_context_reports_not_lost() {
    auto context = veyra::VulkanContext{};

    require(!veyra::vulkan_context_device_lost(context), "missing context reads as not lost");

    require(
        veyra::create_vulkan_context(context, kWidth, kHeight) == veyra::VulkanContextResult::ready,
        "vulkan context comes up"
    );
    require(!veyra::vulkan_context_device_lost(context), "fresh device is not lost");

    auto pipeline = veyra::VulkanRectPipeline{};

    require(
        veyra::create_vulkan_rect_pipeline(context, pipeline) ==
            veyra::VulkanRenderResult::complete,
        "pipeline comes up"
    );

    const std::array<veyra::DisplayCommand, 1> storage{
        veyra::FillRect{
            .bounds = {.width = static_cast<float>(kWidth), .height = static_cast<float>(kHeight)},
            .color = {.red = 200, .green = 40, .blue = 40, .alpha = 255},
        },
    };
    auto pixels = std::array<veyra::SoftwarePixel, kWidth * kHeight>{};
    auto stats = veyra::VulkanRectStats{};

    require(
        veyra::render_vulkan_rects(
            context,
            pipeline,
            std::span<const veyra::DisplayCommand>(storage),
            std::span<veyra::SoftwarePixel>(pixels),
            stats
        ) == veyra::VulkanRenderResult::complete,
        "healthy device renders"
    );
    require(!veyra::vulkan_context_device_lost(context), "healthy render leaves device healthy");
    require(stats.gpu_timed, "healthy render reports timing");

    const std::array<veyra::DisplayCommand, 1> broken{
        veyra::FillRect{
            .bounds = {.width = -1.0F, .height = 1.0F},
            .color = {.red = 200, .green = 40, .blue = 40, .alpha = 255},
        },
    };

    require(
        veyra::render_vulkan_rects(
            context,
            pipeline,
            std::span<const veyra::DisplayCommand>(broken),
            std::span<veyra::SoftwarePixel>(pixels),
            stats
        ) == veyra::VulkanRenderResult::invalid_input,
        "invalid input still reports invalid input"
    );
    require(
        !veyra::vulkan_context_device_lost(context),
        "rejected input does not mark the device lost"
    );

    require(
        veyra::resize_vulkan_target(context, kWidth / 2, kHeight / 2) ==
            veyra::VulkanContextResult::ready,
        "resize succeeds"
    );
    require(!veyra::vulkan_context_device_lost(context), "resize is not device loss");

    veyra::destroy_vulkan_rect_pipeline(context, pipeline);
    veyra::destroy_vulkan_context(context);

    require(!veyra::vulkan_context_device_lost(context), "torn-down context reads as not lost");
}

int run() {
    check_healthy_context_reports_not_lost();

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
