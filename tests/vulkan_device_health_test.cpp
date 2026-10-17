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

    auto caps = veyra::VulkanCapabilities{};
    require(
        veyra::query_vulkan_capabilities(context, caps) == veyra::VulkanContextResult::ready,
        "a rendering context reports capabilities"
    );
    require(
        caps.timestamp_valid == stats.gpu_timed,
        "the timing capability matches the observed frame timing"
    );

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
    require(
        veyra::resize_vulkan_target(context, 0, kHeight / 2) ==
            veyra::VulkanContextResult::invalid_input,
        "a zero-width resize is rejected"
    );
    require(
        veyra::resize_vulkan_target(context, kWidth / 2, 0) ==
            veyra::VulkanContextResult::invalid_input,
        "a zero-height resize is rejected"
    );

    auto missing = veyra::VulkanContext{};
    require(
        veyra::resize_vulkan_target(missing, kWidth, kHeight) ==
            veyra::VulkanContextResult::invalid_input,
        "resizing a missing context is rejected"
    );
    require(!veyra::vulkan_context_ready(missing), "a missing context never reads ready");
    require(veyra::vulkan_context_ready(context), "a live context reads ready");

    veyra::destroy_vulkan_rect_pipeline(context, pipeline);
    veyra::destroy_vulkan_context(context);

    require(!veyra::vulkan_context_device_lost(context), "torn-down context reads as not lost");
}

void check_capabilities_describe_the_backend() {
    auto context = veyra::VulkanContext{};
    auto caps = veyra::VulkanCapabilities{};

    require(
        veyra::query_vulkan_capabilities(context, caps) ==
            veyra::VulkanContextResult::invalid_input,
        "a missing context has no capabilities"
    );

    require(
        veyra::create_vulkan_context(context, kWidth, kHeight) == veyra::VulkanContextResult::ready,
        "vulkan context comes up"
    );

    require(
        veyra::query_vulkan_capabilities(context, caps) == veyra::VulkanContextResult::ready,
        "a live context reports capabilities"
    );
    require(caps.max_image_dimension > 0, "the device accepts nonzero images");
    require(
        caps.staging_bytes > 0 && caps.max_frame_images > 0 && caps.max_rects_per_draw > 0,
        "the backend profile budgets are nonzero"
    );
    require(
        caps.timestamp_valid && !caps.device_lost,
        "a healthy device times frames and is not lost"
    );

    veyra::destroy_vulkan_context(context);

    require(
        veyra::query_vulkan_capabilities(context, caps) ==
            veyra::VulkanContextResult::invalid_input,
        "a torn-down context has no capabilities"
    );
}

int run() {
    check_healthy_context_reports_not_lost();
    check_capabilities_describe_the_backend();

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
