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

enum class VulkanRenderResult : std::uint8_t {
    complete,
    invalid_input,
    insufficient_capacity,
    unsupported_commands,
    device_error,
};

struct VulkanRectStats {
    std::size_t submitted{};
    std::size_t draws{};
    std::size_t rect_draws{};
    std::size_t rounded_draws{};
};

struct VulkanRectPipeline {
    void* pipeline_state{};
    bool ready{};
};

VulkanRenderResult
create_vulkan_rect_pipeline(VulkanContext& context, VulkanRectPipeline& pipeline) noexcept;

void destroy_vulkan_rect_pipeline(VulkanContext& context, VulkanRectPipeline& pipeline) noexcept;

VulkanRenderResult render_vulkan_rects(
    VulkanContext& context,
    VulkanRectPipeline& pipeline,
    std::span<const DisplayCommand> commands,
    std::span<SoftwarePixel> output,
    VulkanRectStats& stats
) noexcept;

}

#endif
