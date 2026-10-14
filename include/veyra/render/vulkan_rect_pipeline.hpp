#ifndef VEYRA_RENDER_VULKAN_RECT_PIPELINE_HPP
#define VEYRA_RENDER_VULKAN_RECT_PIPELINE_HPP

#include "veyra/render/software_renderer.hpp"
#include "veyra/render/vulkan_context.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace veyra {

constexpr std::size_t kVulkanMaxRects = 256;
constexpr std::size_t kVulkanMaxRectDraws = 16;
constexpr std::size_t kVulkanMaxFrameImages = 64;

enum class VulkanRenderResult : std::uint8_t {
    complete,
    invalid_input,
    insufficient_capacity,
    unsupported_commands,
    device_error,
    device_lost,
};

struct VulkanRectStats {
    std::size_t submitted{};
    std::size_t draws{};
    std::size_t rect_draws{};
    std::size_t rounded_draws{};
    std::size_t image_draws{};
    std::size_t mask_draws{};
    std::size_t stroke_draws{};
    std::size_t rounded_stroke_draws{};
    std::size_t glyph_draws{};
    std::uint64_t gpu_ns{};
    bool gpu_timed{};
};

struct VulkanRectPipeline {
    void* pipeline_state{};
    bool ready{};
};

VulkanRenderResult
create_vulkan_rect_pipeline(VulkanContext& context, VulkanRectPipeline& pipeline) noexcept;

VulkanRenderResult
create_vulkan_unified_pipeline(VulkanContext& context, VulkanRectPipeline& pipeline) noexcept;

void destroy_vulkan_rect_pipeline(VulkanContext& context, VulkanRectPipeline& pipeline) noexcept;

VulkanRenderResult render_vulkan_rects(
    VulkanContext& context,
    VulkanRectPipeline& pipeline,
    std::span<const DisplayCommand> commands,
    std::span<SoftwarePixel> output,
    VulkanRectStats& stats
) noexcept;

VulkanRenderResult render_vulkan_rects(
    VulkanContext& context,
    VulkanRectPipeline& pipeline,
    std::span<const DisplayCommand> commands,
    std::span<SoftwarePixel> output,
    VulkanRectStats& stats,
    std::span<const LayoutRect> damage
) noexcept;

VulkanRenderResult render_vulkan_scrolled(
    VulkanContext& context,
    VulkanRectPipeline& pipeline,
    std::span<const DisplayCommand> commands,
    std::span<SoftwarePixel> output,
    VulkanRectStats& stats,
    std::span<const LayoutRect> damage,
    std::int32_t scroll_x,
    std::int32_t scroll_y
) noexcept;

}

#endif
