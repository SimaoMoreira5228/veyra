#ifndef VEYRA_SRC_RENDER_VULKAN_FRAME_BATCH_HPP
#define VEYRA_SRC_RENDER_VULKAN_FRAME_BATCH_HPP

#include "veyra/render/software_renderer.hpp"
#include "veyra/render/vulkan_rect_pipeline.hpp"
#include "vulkan_internal.hpp"

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace veyra {

enum class DrawPipe : std::uint8_t {
    rect,
    rounded,
    image,
    mask,
    stroke,
    rounded_stroke,
    glyph,
};

constexpr std::size_t kVulkanPipeCount = 7;

struct FrameBatch {
    vulkan_detail::ContextState& state;
    vulkan_detail::PipelineState& stored;
    VulkanRectStats& stats;
    std::size_t pipe_slots[kVulkanPipeCount]{};
    std::size_t pipe_counts[kVulkanPipeCount]{};
    void* instance_bases[kVulkanPipeCount]{};
    std::size_t instance_strides[kVulkanPipeCount]{};
    bool pass_open{};
    bool bound_valid{};
    DrawPipe bound{DrawPipe::rect};
    VkDescriptorSet bound_set{};
    bool pending_valid{};
    DrawPipe pending_pipe{DrawPipe::rect};
    VkDescriptorSet pending_set{};
    std::size_t run_base{};
    bool scissor_dirty{true};
    SoftwareClipRect current_clip{};
    VkRenderPass pass{};

    FrameBatch(
        vulkan_detail::ContextState& state,
        vulkan_detail::PipelineState& stored,
        VulkanRectStats& stats,
        VkRenderPass pass
    ) noexcept;

    void ensure_pass() noexcept;
    void abandon() noexcept;
    void flush_pending() noexcept;
    void begin_region(SoftwareClipRect clip) noexcept;
    void flush_unless(DrawPipe pipe, VkDescriptorSet set) noexcept;
    void set_clip(SoftwareClipRect clip) noexcept;
    void
    draw_run(DrawPipe pipe, std::size_t first, std::size_t count, VkDescriptorSet set) noexcept;

    template <typename Instance>
    VulkanRenderResult emit(DrawPipe pipe, VkDescriptorSet set, const Instance& instance) noexcept {
        if (stats.submitted == kVulkanMaxRects * kVulkanMaxRectDraws) {
            abandon();

            return VulkanRenderResult::insufficient_capacity;
        }

        const auto index = static_cast<std::size_t>(pipe);

        flush_unless(pipe, set);

        if (!pending_valid) {
            pending_pipe = pipe;
            pending_set = set;
            run_base = pipe_slots[index];
            pending_valid = true;
        }

        const auto slot = pipe_slots[index]++;

        std::memcpy(
            static_cast<std::uint8_t*>(instance_bases[index]) + slot * instance_strides[index],
            &instance,
            sizeof(Instance)
        );

        ++pipe_counts[index];
        ++stats.submitted;

        if (pipe_counts[index] == kVulkanMaxRects) {
            ensure_pass();

            draw_run(pipe, run_base, pipe_counts[index], set);

            pipe_counts[index] = 0;
            pending_valid = false;
        }

        return VulkanRenderResult::complete;
    }
};

}

#endif
