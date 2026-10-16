#ifndef VEYRA_SRC_RENDER_VULKAN_FRAME_TRANSFER_HPP
#define VEYRA_SRC_RENDER_VULKAN_FRAME_TRANSFER_HPP

#include "veyra/render/software_renderer.hpp"
#include "veyra/render/vulkan_rect_pipeline.hpp"
#include "vulkan_frame_batch.hpp"
#include "vulkan_internal.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace veyra::vulkan_detail {

bool begin_frame_commands(ContextState& state) noexcept;

VulkanRenderResult end_frame_transfer(
    ContextState& state,
    std::uint64_t& gpu_ns,
    std::span<const LayoutRect> damage,
    const SoftwareClipRect* exposed,
    std::size_t exposed_count,
    bool full_copy
) noexcept;

VulkanRenderResult retain_scrolled_image(
    ContextState& state,
    FrameBatch& batch,
    std::span<const LayoutRect> damage,
    std::int32_t scroll_x,
    std::int32_t scroll_y,
    std::array<SoftwareClipRect, 2>& exposed,
    std::size_t& exposed_count
) noexcept;

void copy_frame_output(
    ContextState& state,
    std::span<SoftwarePixel> output,
    SoftwareClipRect bounds
) noexcept;

}

#endif
