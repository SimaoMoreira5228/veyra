#include "vulkan_frame_batch.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace veyra {
namespace {

void begin_pass(
    vulkan_detail::ContextState& state,
    vulkan_detail::PipelineState& stored,
    VkRenderPass pass
) noexcept {
    const auto clear = VkClearValue{
        .color = {.float32 = {0.0F, 0.0F, 0.0F, 1.0F}},
    };

    const auto pass_info = VkRenderPassBeginInfo{
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = pass,
        .framebuffer = state.framebuffer,
        .renderArea =
            {
                .extent =
                    {
                        .width = static_cast<std::uint32_t>(state.width),
                        .height = static_cast<std::uint32_t>(state.height),
                    },
            },
        .clearValueCount = 1,
        .pClearValues = &clear,
    };

    vkCmdBeginRenderPass(state.commands, &pass_info, VK_SUBPASS_CONTENTS_INLINE);

    const auto viewport = VkViewport{
        .width = static_cast<float>(state.width),
        .height = static_cast<float>(state.height),
        .maxDepth = 1.0F,
    };

    vkCmdSetViewport(state.commands, 0, 1, &viewport);

    const auto scissor = VkRect2D{
        .extent = {
            .width = static_cast<std::uint32_t>(state.width),
            .height = static_cast<std::uint32_t>(state.height),
        },
    };

    vkCmdSetScissor(state.commands, 0, 1, &scissor);

    const auto extent =
        std::array<float, 2>{static_cast<float>(state.width), static_cast<float>(state.height)};

    vkCmdPushConstants(
        state.commands,
        stored.layout,
        VK_SHADER_STAGE_VERTEX_BIT,
        0,
        sizeof(extent),
        extent.data()
    );
}

void draw_chunk(
    vulkan_detail::ContextState& state,
    std::uint32_t first,
    std::uint32_t count
) noexcept {
    vkCmdDraw(state.commands, 6, count, 0, first);
}

void abandon_frame(
    vulkan_detail::ContextState& state,
    vulkan_detail::PipelineState& stored,
    bool pass_open
) noexcept {
    if (pass_open) {
        vkCmdEndRenderPass(state.commands);
    }

    vkEndCommandBuffer(state.commands);
    vulkan_detail::evict_frame_images(state, stored);
}

}

FrameBatch::FrameBatch(
    vulkan_detail::ContextState& state,
    vulkan_detail::PipelineState& stored,
    VulkanRectStats& stats,
    VkRenderPass pass
) noexcept : state(state), stored(stored), stats(stats), pass(pass) {
    current_clip = SoftwareClipRect{
        .left = 0,
        .top = 0,
        .right = state.width,
        .bottom = state.height,
    };

    instance_bases[static_cast<std::size_t>(DrawPipe::rect)] = stored.instance_mapped;
    instance_strides[static_cast<std::size_t>(DrawPipe::rect)] =
        sizeof(vulkan_detail::RectInstance);
    instance_bases[static_cast<std::size_t>(DrawPipe::rounded)] = stored.rounded_instance_mapped;
    instance_strides[static_cast<std::size_t>(DrawPipe::rounded)] =
        sizeof(vulkan_detail::RoundedRectInstance);
    instance_bases[static_cast<std::size_t>(DrawPipe::image)] = stored.image_instance_mapped;
    instance_strides[static_cast<std::size_t>(DrawPipe::image)] =
        sizeof(vulkan_detail::ImageInstance);
    instance_bases[static_cast<std::size_t>(DrawPipe::mask)] = stored.mask_instance_mapped;
    instance_strides[static_cast<std::size_t>(DrawPipe::mask)] =
        sizeof(vulkan_detail::MaskInstance);
    instance_bases[static_cast<std::size_t>(DrawPipe::stroke)] = stored.stroke_instance_mapped;
    instance_strides[static_cast<std::size_t>(DrawPipe::stroke)] =
        sizeof(vulkan_detail::StrokeInstance);
    instance_bases[static_cast<std::size_t>(DrawPipe::rounded_stroke)] =
        stored.rounded_stroke_instance_mapped;
    instance_strides[static_cast<std::size_t>(DrawPipe::rounded_stroke)] =
        sizeof(vulkan_detail::RoundedStrokeInstance);
    instance_bases[static_cast<std::size_t>(DrawPipe::glyph)] = stored.glyph_instance_mapped;
    instance_strides[static_cast<std::size_t>(DrawPipe::glyph)] =
        sizeof(vulkan_detail::GlyphInstance);
}

void FrameBatch::ensure_pass() noexcept {
    if (!pass_open) {
        begin_pass(state, stored, pass);
        pass_open = true;
    }
}

void FrameBatch::abandon() noexcept {
    abandon_frame(state, stored, pass_open);
}

void FrameBatch::flush_pending() noexcept {
    if (!pending_valid) {
        return;
    }

    ensure_pass();

    draw_run(
        pending_pipe,
        run_base,
        pipe_counts[static_cast<std::size_t>(pending_pipe)],
        pending_set
    );

    pipe_counts[static_cast<std::size_t>(pending_pipe)] = 0;
    pending_valid = false;
}

void FrameBatch::flush_unless(DrawPipe pipe, VkDescriptorSet set) noexcept {
    if (pending_valid && (pending_pipe != pipe || pending_set != set)) {
        ensure_pass();

        draw_run(
            pending_pipe,
            run_base,
            pipe_counts[static_cast<std::size_t>(pending_pipe)],
            pending_set
        );

        pipe_counts[static_cast<std::size_t>(pending_pipe)] = 0;
        pending_valid = false;
    }
}

void FrameBatch::begin_region(SoftwareClipRect clip) noexcept {
    flush_pending();

    for (auto index = std::size_t{}; index < kVulkanPipeCount; ++index) {
        pipe_counts[index] = 0;
    }

    current_clip = clip;
    scissor_dirty = true;
}
void FrameBatch::set_clip(SoftwareClipRect clip) noexcept {
    flush_pending();

    current_clip = clip;
    scissor_dirty = true;
}

void FrameBatch::draw_run(
    DrawPipe pipe,
    std::size_t first,
    std::size_t count,
    VkDescriptorSet set
) noexcept {
    const auto offset = VkDeviceSize{};

    if (scissor_dirty) {
        const auto scissor = VkRect2D{
            .offset =
                {
                    .x = static_cast<std::int32_t>(current_clip.left),
                    .y = static_cast<std::int32_t>(current_clip.top),
                },
            .extent = {
                .width = static_cast<std::uint32_t>(current_clip.right - current_clip.left),
                .height = static_cast<std::uint32_t>(current_clip.bottom - current_clip.top),
            },
        };

        vkCmdSetScissor(state.commands, 0, 1, &scissor);

        scissor_dirty = false;
    }

    if (!bound_valid || bound != pipe) {
        if (pipe == DrawPipe::rect) {
            vkCmdBindPipeline(state.commands, VK_PIPELINE_BIND_POINT_GRAPHICS, stored.pipeline);
            vkCmdBindVertexBuffers(state.commands, 0, 1, &stored.instances, &offset);
        } else if (pipe == DrawPipe::rounded) {
            vkCmdBindPipeline(
                state.commands,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                stored.rounded_pipeline
            );
            vkCmdBindVertexBuffers(state.commands, 0, 1, &stored.rounded_instances, &offset);
        } else if (pipe == DrawPipe::image) {
            vkCmdBindPipeline(
                state.commands,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                stored.image_pipeline
            );
            vkCmdBindVertexBuffers(state.commands, 0, 1, &stored.image_instances, &offset);
        } else if (pipe == DrawPipe::mask) {
            vkCmdBindPipeline(
                state.commands,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                stored.mask_pipeline
            );
            vkCmdBindVertexBuffers(state.commands, 0, 1, &stored.mask_instances, &offset);
        } else if (pipe == DrawPipe::stroke) {
            vkCmdBindPipeline(
                state.commands,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                stored.stroke_pipeline
            );
            vkCmdBindVertexBuffers(state.commands, 0, 1, &stored.stroke_instances, &offset);
        } else if (pipe == DrawPipe::rounded_stroke) {
            vkCmdBindPipeline(
                state.commands,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                stored.rounded_stroke_pipeline
            );
            vkCmdBindVertexBuffers(state.commands, 0, 1, &stored.rounded_stroke_instances, &offset);
        } else {
            vkCmdBindPipeline(
                state.commands,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                stored.glyph_pipeline
            );
            vkCmdBindVertexBuffers(state.commands, 0, 1, &stored.glyph_instances, &offset);
        }

        bound = pipe;
        bound_valid = true;
        bound_set = VkDescriptorSet{VK_NULL_HANDLE};
    }

    if ((pipe == DrawPipe::image || pipe == DrawPipe::mask || pipe == DrawPipe::glyph) &&
        set != bound_set) {
        auto layout = stored.glyph_layout;

        if (pipe == DrawPipe::image) {
            layout = stored.image_layout;
        } else if (pipe == DrawPipe::mask) {
            layout = stored.mask_layout;
        }

        vkCmdBindDescriptorSets(
            state.commands,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            layout,
            0,
            1,
            &set,
            0,
            nullptr
        );

        bound_set = set;
    }

    draw_chunk(state, static_cast<std::uint32_t>(first), static_cast<std::uint32_t>(count));

    if (pipe == DrawPipe::rect) {
        ++stats.rect_draws;
    } else if (pipe == DrawPipe::rounded) {
        ++stats.rounded_draws;
    } else if (pipe == DrawPipe::image) {
        ++stats.image_draws;
    } else if (pipe == DrawPipe::mask) {
        ++stats.mask_draws;
    } else if (pipe == DrawPipe::stroke) {
        ++stats.stroke_draws;
    } else if (pipe == DrawPipe::rounded_stroke) {
        ++stats.rounded_stroke_draws;
    } else {
        ++stats.glyph_draws;
    }

    ++stats.draws;
}
}
