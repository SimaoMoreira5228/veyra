#include "veyra/render/vulkan_rect_pipeline.hpp"

#include "software_renderer_internal.hpp"
#include "vulkan_internal.hpp"

#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <variant>

namespace veyra {
namespace {

bool begin_frame(
    vulkan_detail::ContextState& state,
    vulkan_detail::PipelineState& pipeline
) noexcept {
    if (vkResetCommandBuffer(state.commands, 0) != VK_SUCCESS) {
        return false;
    }

    const auto begin_info = VkCommandBufferBeginInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };

    if (vkBeginCommandBuffer(state.commands, &begin_info) != VK_SUCCESS) {
        return false;
    }

    const auto clear = VkClearValue{
        .color = {.float32 = {0.0F, 0.0F, 0.0F, 1.0F}},
    };

    const auto pass_info = VkRenderPassBeginInfo{
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = pipeline.pass,
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
        pipeline.layout,
        VK_SHADER_STAGE_VERTEX_BIT,
        0,
        sizeof(extent),
        extent.data()
    );

    return true;
}

void draw_chunk(
    vulkan_detail::ContextState& state,
    std::uint32_t first,
    std::uint32_t count
) noexcept {
    vkCmdDraw(state.commands, 6, count, 0, first);
}

bool end_frame(vulkan_detail::ContextState& state) noexcept {
    vkCmdEndRenderPass(state.commands);

    const auto to_copy = VkImageMemoryBarrier{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = state.target.image,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .levelCount = 1,
            .layerCount = 1,
        },
    };

    vkCmdPipelineBarrier(
        state.commands,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &to_copy
    );

    const auto copy = VkBufferImageCopy{
        .imageSubresource =
            {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .layerCount = 1,
            },
        .imageExtent = {
            .width = static_cast<std::uint32_t>(state.width),
            .height = static_cast<std::uint32_t>(state.height),
            .depth = 1,
        },
    };

    vkCmdCopyImageToBuffer(
        state.commands,
        state.target.image,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        state.target.staging,
        1,
        &copy
    );

    if (vkEndCommandBuffer(state.commands) != VK_SUCCESS) {
        return false;
    }

    vkResetFences(state.device, 1, &state.fence);

    const auto submit = VkSubmitInfo{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &state.commands,
    };

    if (vkQueueSubmit(state.queue, 1, &submit, state.fence) != VK_SUCCESS) {
        return false;
    }

    constexpr auto timeout_ns = 10'000'000'000ULL;

    if (vkWaitForFences(state.device, 1, &state.fence, VK_TRUE, timeout_ns) != VK_SUCCESS) {
        return false;
    }

    return true;
}

void abandon_frame(vulkan_detail::ContextState& state) noexcept {
    vkCmdEndRenderPass(state.commands);
    vkEndCommandBuffer(state.commands);
}

enum class DrawPipe : std::uint8_t {
    rect,
    rounded,
};

}

VulkanRenderResult render_vulkan_rects(
    VulkanContext& context,
    VulkanRectPipeline& pipeline,
    std::span<const DisplayCommand> commands,
    std::span<SoftwarePixel> output,
    VulkanRectStats& stats
) noexcept {
    auto* state = vulkan_detail::state_of(context);
    auto* stored = vulkan_detail::pipeline_of(pipeline);

    if (state == nullptr || stored == nullptr || !context.ready || !pipeline.ready) {
        return VulkanRenderResult::invalid_input;
    }

    if (output.size() != state->width * state->height) {
        return VulkanRenderResult::invalid_input;
    }

    if (state->framebuffer == VK_NULL_HANDLE) {
        return VulkanRenderResult::device_error;
    }

    stats = VulkanRectStats{};

    auto* rects = static_cast<vulkan_detail::RectInstance*>(stored->instance_mapped);
    auto* rounded =
        static_cast<vulkan_detail::RoundedRectInstance*>(stored->rounded_instance_mapped);
    auto rect_count = std::size_t{};
    auto rounded_count = std::size_t{};
    auto rect_slot = std::size_t{};
    auto rounded_slot = std::size_t{};
    auto rect_base = std::size_t{};
    auto rounded_base = std::size_t{};
    auto begun = false;
    auto bound = DrawPipe::rect;
    auto bound_valid = false;
    auto translation = LayoutPoint{};
    auto translation_stack = std::array<LayoutPoint, 8>{};
    auto translation_depth = std::size_t{};

    auto ensure_begun = [&]() noexcept {
        if (!begun) {
            if (!begin_frame(*state, *stored)) {
                return false;
            }

            begun = true;
        }

        return true;
    };

    auto draw_pending = [&](DrawPipe pipe, std::size_t first, std::size_t count) noexcept {
        const auto offset = VkDeviceSize{};

        if (!bound_valid || bound != pipe) {
            if (pipe == DrawPipe::rect) {
                vkCmdBindPipeline(
                    state->commands,
                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                    stored->pipeline
                );
                vkCmdBindVertexBuffers(state->commands, 0, 1, &stored->instances, &offset);
            } else {
                vkCmdBindPipeline(
                    state->commands,
                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                    stored->rounded_pipeline
                );
                vkCmdBindVertexBuffers(state->commands, 0, 1, &stored->rounded_instances, &offset);
            }

            bound = pipe;
            bound_valid = true;
        }

        if (pipe == DrawPipe::rect) {
            draw_chunk(
                *state,
                static_cast<std::uint32_t>(first),
                static_cast<std::uint32_t>(count)
            );

            ++stats.rect_draws;
        } else {
            draw_chunk(
                *state,
                static_cast<std::uint32_t>(first),
                static_cast<std::uint32_t>(count)
            );

            ++stats.rounded_draws;
        }

        ++stats.draws;
    };

    for (const auto& command : commands) {
        if (const auto* push = std::get_if<PushTranslation>(&command)) {
            if (translation_depth >= translation_stack.size()) {
                if (begun) {
                    abandon_frame(*state);
                }

                return VulkanRenderResult::insufficient_capacity;
            }

            translation_stack[translation_depth] = translation;
            translation.x += push->offset.x;
            translation.y += push->offset.y;
            ++translation_depth;

            continue;
        }

        if (std::holds_alternative<PopTranslation>(command)) {
            if (translation_depth == 0) {
                if (begun) {
                    abandon_frame(*state);
                }

                return VulkanRenderResult::invalid_input;
            }

            --translation_depth;
            translation = translation_stack[translation_depth];

            continue;
        }

        const auto* fill = std::get_if<FillRect>(&command);
        const auto* rounded_fill = std::get_if<FillRoundedRect>(&command);

        if (fill == nullptr && rounded_fill == nullptr) {
            if (begun) {
                abandon_frame(*state);
            }

            return VulkanRenderResult::unsupported_commands;
        }

        const auto& shape = fill != nullptr ? fill->bounds : rounded_fill->bounds;
        const auto& color = fill != nullptr ? fill->color : rounded_fill->color;

        if (!render_detail::valid_rect(shape)) {
            if (begun) {
                abandon_frame(*state);
            }

            return VulkanRenderResult::invalid_input;
        }

        if (color.alpha == 0) {
            continue;
        }

        const auto translated = LayoutRect{
            .x = shape.x + translation.x,
            .y = shape.y + translation.y,
            .width = shape.width,
            .height = shape.height,
        };

        const auto bounds = render_detail::pixel_bounds(translated, state->width, state->height);

        if (bounds.left >= bounds.right || bounds.top >= bounds.bottom) {
            continue;
        }

        if (stats.submitted == kVulkanMaxRects * kVulkanMaxRectDraws) {
            if (begun) {
                abandon_frame(*state);
            }

            return VulkanRenderResult::insufficient_capacity;
        }

        constexpr auto channel_scale = 1.0F / 255.0F;

        auto pipe = DrawPipe::rect;
        auto radius = 0.0F;

        if (rounded_fill != nullptr) {
            const auto diameter = std::min(
                static_cast<float>(bounds.right - bounds.left),
                static_cast<float>(bounds.bottom - bounds.top)
            );

            radius = std::min(rounded_fill->radius, diameter * 0.5F);

            if (radius > 0.0F) {
                pipe = DrawPipe::rounded;
            }
        }

        if (pipe == DrawPipe::rect && rounded_count != 0) {
            if (!ensure_begun()) {
                return VulkanRenderResult::device_error;
            }

            draw_pending(DrawPipe::rounded, rounded_base, rounded_count);

            rounded_count = 0;
        }

        if (pipe == DrawPipe::rounded && rect_count != 0) {
            if (!ensure_begun()) {
                return VulkanRenderResult::device_error;
            }

            draw_pending(DrawPipe::rect, rect_base, rect_count);

            rect_count = 0;
        }

        if (pipe == DrawPipe::rect) {
            if (rect_count == 0) {
                rect_base = rect_slot;
            }

            const auto slot = rect_slot++;

            rects[slot] = vulkan_detail::RectInstance{
                .min_x = static_cast<float>(bounds.left),
                .min_y = static_cast<float>(bounds.top),
                .max_x = static_cast<float>(bounds.right),
                .max_y = static_cast<float>(bounds.bottom),
                .red = static_cast<float>(color.red) * channel_scale,
                .green = static_cast<float>(color.green) * channel_scale,
                .blue = static_cast<float>(color.blue) * channel_scale,
                .alpha = static_cast<float>(color.alpha) * channel_scale,
            };

            ++rect_count;
        } else {
            if (rounded_count == 0) {
                rounded_base = rounded_slot;
            }

            const auto slot = rounded_slot++;

            rounded[slot] = vulkan_detail::RoundedRectInstance{
                .min_x = static_cast<float>(bounds.left),
                .min_y = static_cast<float>(bounds.top),
                .max_x = static_cast<float>(bounds.right),
                .max_y = static_cast<float>(bounds.bottom),
                .red = static_cast<float>(color.red) * channel_scale,
                .green = static_cast<float>(color.green) * channel_scale,
                .blue = static_cast<float>(color.blue) * channel_scale,
                .alpha = static_cast<float>(color.alpha) * channel_scale,
                .radius = radius,
            };

            ++rounded_count;
        }

        ++stats.submitted;

        if (rect_count == kVulkanMaxRects) {
            if (!ensure_begun()) {
                return VulkanRenderResult::device_error;
            }

            draw_pending(DrawPipe::rect, rect_base, rect_count);

            rect_count = 0;
        }

        if (rounded_count == kVulkanMaxRects) {
            if (!ensure_begun()) {
                return VulkanRenderResult::device_error;
            }

            draw_pending(DrawPipe::rounded, rounded_base, rounded_count);

            rounded_count = 0;
        }
    }

    if (translation_depth != 0) {
        if (begun) {
            abandon_frame(*state);
        }

        return VulkanRenderResult::invalid_input;
    }

    if (!begun) {
        if (!begin_frame(*state, *stored)) {
            return VulkanRenderResult::device_error;
        }

        begun = true;
    }

    if (rect_count != 0) {
        draw_pending(DrawPipe::rect, rect_base, rect_count);
    }

    if (rounded_count != 0) {
        draw_pending(DrawPipe::rounded, rounded_base, rounded_count);
    }

    if (!end_frame(*state)) {
        return VulkanRenderResult::device_error;
    }

    const auto byte_count = state->width * state->height * sizeof(SoftwarePixel);

    std::memcpy(output.data(), state->target.staging_mapped, byte_count);

    return VulkanRenderResult::complete;
}

}
