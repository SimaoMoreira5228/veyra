#include "veyra/render/vulkan_rect_pipeline.hpp"

#include "software_renderer_internal.hpp"
#include "vulkan_internal.hpp"

#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <variant>

namespace veyra {
namespace {

bool begin_commands(vulkan_detail::ContextState& state) noexcept {
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

    return true;
}

void begin_pass(
    vulkan_detail::ContextState& state,
    vulkan_detail::PipelineState& pipeline
) noexcept {
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

std::size_t stroke_rect_bars(
    const LayoutRect& bounds,
    float thickness,
    std::size_t width,
    std::size_t height,
    LayoutRect* bars
) noexcept {
    const auto outer = render_detail::pixel_bounds(bounds, width, height);

    if (outer.left >= outer.right || outer.top >= outer.bottom) {
        return 0;
    }

    const auto minimum =
        std::min(static_cast<double>(bounds.width), static_cast<double>(bounds.height));
    const auto inset = std::min(static_cast<double>(thickness), minimum * 0.5);

    if (inset * 2.0 >= minimum) {
        bars[0] = LayoutRect{
            .x = static_cast<float>(outer.left),
            .y = static_cast<float>(outer.top),
            .width = static_cast<float>(outer.right - outer.left),
            .height = static_cast<float>(outer.bottom - outer.top),
        };

        return 1;
    }

    const auto inner = render_detail::intersect(
        outer,
        render_detail::pixel_interior_bounds(
            static_cast<double>(bounds.x) + inset,
            static_cast<double>(bounds.y) + inset,
            static_cast<double>(bounds.width) - inset * 2.0,
            static_cast<double>(bounds.height) - inset * 2.0,
            width,
            height
        )
    );

    if (inner.left >= inner.right || inner.top >= inner.bottom) {
        bars[0] = LayoutRect{
            .x = static_cast<float>(outer.left),
            .y = static_cast<float>(outer.top),
            .width = static_cast<float>(outer.right - outer.left),
            .height = static_cast<float>(outer.bottom - outer.top),
        };

        return 1;
    }

    bars[0] = LayoutRect{
        .x = static_cast<float>(outer.left),
        .y = static_cast<float>(outer.top),
        .width = static_cast<float>(outer.right - outer.left),
        .height = static_cast<float>(inner.top - outer.top),
    };
    bars[1] = LayoutRect{
        .x = static_cast<float>(outer.left),
        .y = static_cast<float>(inner.bottom),
        .width = static_cast<float>(outer.right - outer.left),
        .height = static_cast<float>(outer.bottom - inner.bottom),
    };
    bars[2] = LayoutRect{
        .x = static_cast<float>(outer.left),
        .y = static_cast<float>(inner.top),
        .width = static_cast<float>(inner.left - outer.left),
        .height = static_cast<float>(inner.bottom - inner.top),
    };
    bars[3] = LayoutRect{
        .x = static_cast<float>(inner.right),
        .y = static_cast<float>(inner.top),
        .width = static_cast<float>(outer.right - inner.right),
        .height = static_cast<float>(inner.bottom - inner.top),
    };

    return 4;
}

enum class DrawPipe : std::uint8_t {
    rect,
    rounded,
    image,
    mask,
    stroke,
    rounded_stroke,
};

constexpr std::size_t kVulkanPipeCount = 6;

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

    if (!begin_commands(*state)) {
        return VulkanRenderResult::device_error;
    }

    stored->staging_used = 0;
    stored->frame_seq += 1;

    {
        const auto uploaded = vulkan_detail::upload_textures(*state, *stored, commands);

        if (uploaded != VulkanRenderResult::complete) {
            abandon_frame(*state, *stored, false);
            return uploaded;
        }
    }

    auto* rects = static_cast<vulkan_detail::RectInstance*>(stored->instance_mapped);
    auto* rounded =
        static_cast<vulkan_detail::RoundedRectInstance*>(stored->rounded_instance_mapped);
    auto* images = static_cast<vulkan_detail::ImageInstance*>(stored->image_instance_mapped);
    auto* masks = static_cast<vulkan_detail::MaskInstance*>(stored->mask_instance_mapped);
    auto* strokes = static_cast<vulkan_detail::StrokeInstance*>(stored->stroke_instance_mapped);
    auto* rounded_strokes =
        static_cast<vulkan_detail::RoundedStrokeInstance*>(stored->rounded_stroke_instance_mapped);
    std::size_t pipe_counts[kVulkanPipeCount] = {};
    std::size_t pipe_slots[kVulkanPipeCount] = {};
    auto pass_open = false;
    auto bound_valid = false;
    auto bound = DrawPipe::rect;
    auto bound_set = VkDescriptorSet{VK_NULL_HANDLE};
    auto pending_valid = false;
    auto pending_pipe = DrawPipe::rect;
    auto pending_set = VkDescriptorSet{VK_NULL_HANDLE};
    auto run_base = std::size_t{};
    auto translation = LayoutPoint{};
    auto translation_stack = std::array<LayoutPoint, 8>{};
    auto translation_depth = std::size_t{};

    const auto pipe_index = [](DrawPipe pipe) noexcept {
        return static_cast<std::size_t>(pipe);
    };

    auto ensure_pass = [&]() noexcept {
        if (!pass_open) {
            begin_pass(*state, *stored);
            pass_open = true;
        }
    };

    auto draw_pending = [&](DrawPipe pipe,
                            std::size_t first,
                            std::size_t count,
                            VkDescriptorSet set) noexcept {
        const auto offset = VkDeviceSize{};

        if (!bound_valid || bound != pipe) {
            if (pipe == DrawPipe::rect) {
                vkCmdBindPipeline(
                    state->commands,
                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                    stored->pipeline
                );
                vkCmdBindVertexBuffers(state->commands, 0, 1, &stored->instances, &offset);
            } else if (pipe == DrawPipe::rounded) {
                vkCmdBindPipeline(
                    state->commands,
                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                    stored->rounded_pipeline
                );
                vkCmdBindVertexBuffers(state->commands, 0, 1, &stored->rounded_instances, &offset);
            } else if (pipe == DrawPipe::image) {
                vkCmdBindPipeline(
                    state->commands,
                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                    stored->image_pipeline
                );
                vkCmdBindVertexBuffers(state->commands, 0, 1, &stored->image_instances, &offset);
            } else if (pipe == DrawPipe::mask) {
                vkCmdBindPipeline(
                    state->commands,
                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                    stored->mask_pipeline
                );
                vkCmdBindVertexBuffers(state->commands, 0, 1, &stored->mask_instances, &offset);
            } else if (pipe == DrawPipe::stroke) {
                vkCmdBindPipeline(
                    state->commands,
                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                    stored->stroke_pipeline
                );
                vkCmdBindVertexBuffers(state->commands, 0, 1, &stored->stroke_instances, &offset);
            } else {
                vkCmdBindPipeline(
                    state->commands,
                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                    stored->rounded_stroke_pipeline
                );
                vkCmdBindVertexBuffers(
                    state->commands,
                    0,
                    1,
                    &stored->rounded_stroke_instances,
                    &offset
                );
            }

            bound = pipe;
            bound_valid = true;
            bound_set = VkDescriptorSet{VK_NULL_HANDLE};
        }

        if ((pipe == DrawPipe::image || pipe == DrawPipe::mask) && set != bound_set) {
            vkCmdBindDescriptorSets(
                state->commands,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                pipe == DrawPipe::image ? stored->image_layout : stored->mask_layout,
                0,
                1,
                &set,
                0,
                nullptr
            );

            bound_set = set;
        }

        draw_chunk(*state, static_cast<std::uint32_t>(first), static_cast<std::uint32_t>(count));

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
        } else {
            ++stats.rounded_stroke_draws;
        }

        ++stats.draws;
    };

    for (const auto& command : commands) {
        if (const auto* push = std::get_if<PushTranslation>(&command)) {
            if (translation_depth >= translation_stack.size()) {
                abandon_frame(*state, *stored, pass_open);

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
                abandon_frame(*state, *stored, pass_open);

                return VulkanRenderResult::invalid_input;
            }

            --translation_depth;
            translation = translation_stack[translation_depth];

            continue;
        }

        const auto* fill = std::get_if<FillRect>(&command);
        const auto* rounded_fill = std::get_if<FillRoundedRect>(&command);
        const auto* drawn = std::get_if<DrawImage>(&command);
        const auto* masked = std::get_if<DrawMask>(&command);
        const auto* stroked = std::get_if<StrokeRect>(&command);
        const auto* rounded_stroked = std::get_if<StrokeRoundedRect>(&command);
        const auto* lined = std::get_if<StrokeLine>(&command);
        const auto* polylined = std::get_if<StrokePolyline>(&command);

        if (fill == nullptr && rounded_fill == nullptr && drawn == nullptr && masked == nullptr &&
            stroked == nullptr && rounded_stroked == nullptr && lined == nullptr &&
            polylined == nullptr) {
            abandon_frame(*state, *stored, pass_open);

            return VulkanRenderResult::unsupported_commands;
        }

        auto pipe = DrawPipe::rect;
        auto set = VkDescriptorSet{VK_NULL_HANDLE};
        auto radius = 0.0F;
        auto opacity = 1.0F;
        auto is_stroke = false;
        auto thickness = 0.0F;
        LayoutRect shape{};
        DisplayColor color{};

        if (drawn != nullptr) {
            const auto* image = drawn->image;

            if (image == nullptr || !render_detail::valid_image(*image) ||
                !render_detail::valid_image_placement(*drawn)) {
                abandon_frame(*state, *stored, pass_open);

                return VulkanRenderResult::invalid_input;
            }

            if (drawn->opacity == 0) {
                continue;
            }

            const auto found = vulkan_detail::lookup_image(*stored, *image, drawn->sampling);

            if (found == VK_NULL_HANDLE) {
                abandon_frame(*state, *stored, pass_open);

                return VulkanRenderResult::insufficient_capacity;
            }

            pipe = DrawPipe::image;
            set = found;
            opacity = static_cast<float>(drawn->opacity) * (1.0F / 255.0F);
            shape = drawn->destination;
        } else if (masked != nullptr) {
            const auto* mask = masked->mask;

            if (mask == nullptr || mask->stride < mask->width ||
                mask->coverage.size() < mask->height * mask->stride) {
                abandon_frame(*state, *stored, pass_open);

                return VulkanRenderResult::invalid_input;
            }

            if (masked->color.alpha == 0) {
                continue;
            }

            const auto found = vulkan_detail::lookup_mask(*stored, *mask);

            if (found == VK_NULL_HANDLE) {
                abandon_frame(*state, *stored, pass_open);

                return VulkanRenderResult::insufficient_capacity;
            }

            pipe = DrawPipe::mask;
            set = found;
            color = masked->color;
            shape = LayoutRect{
                .x = masked->x,
                .y = masked->y,
                .width = static_cast<float>(mask->width),
                .height = static_cast<float>(mask->height),
            };
        } else if (stroked != nullptr) {
            shape = stroked->bounds;
            color = stroked->color;

            if (!render_detail::valid_rect(shape)) {
                abandon_frame(*state, *stored, pass_open);

                return VulkanRenderResult::invalid_input;
            }

            if (!std::isfinite(stroked->thickness) || stroked->thickness < 0.0F) {
                abandon_frame(*state, *stored, pass_open);

                return VulkanRenderResult::invalid_input;
            }

            if (color.alpha == 0 || stroked->thickness == 0.0F) {
                continue;
            }

            pipe = DrawPipe::rect;
            is_stroke = true;
            thickness = stroked->thickness;
        } else if (lined != nullptr || polylined != nullptr) {
            const auto thickness = lined != nullptr ? lined->thickness : polylined->thickness;
            const auto& color = lined != nullptr ? lined->color : polylined->color;

            if (!std::isfinite(thickness) || thickness < 0.0F) {
                abandon_frame(*state, *stored, pass_open);

                return VulkanRenderResult::invalid_input;
            }

            if (color.alpha == 0 || thickness == 0.0F) {
                continue;
            }

            if (polylined != nullptr && polylined->points.size() < 2) {
                abandon_frame(*state, *stored, pass_open);

                return VulkanRenderResult::invalid_input;
            }

            pipe = DrawPipe::stroke;
            is_stroke = false;

            auto emit_segment = [&](LayoutPoint start, LayoutPoint end) -> VulkanRenderResult {
                const auto translated_start = LayoutPoint{
                    .x = start.x + translation.x,
                    .y = start.y + translation.y,
                };
                const auto translated_end = LayoutPoint{
                    .x = end.x + translation.x,
                    .y = end.y + translation.y,
                };

                if (!std::isfinite(translated_start.x) || !std::isfinite(translated_start.y) ||
                    !std::isfinite(translated_end.x) || !std::isfinite(translated_end.y)) {
                    abandon_frame(*state, *stored, pass_open);

                    return VulkanRenderResult::invalid_input;
                }

                const auto fringe = static_cast<double>(thickness) * 0.5 + 0.5;
                const auto quad = LayoutRect{
                    .x =
                        static_cast<float>(std::min(translated_start.x, translated_end.x) - fringe),
                    .y =
                        static_cast<float>(std::min(translated_start.y, translated_end.y) - fringe),
                    .width = static_cast<float>(
                        std::abs(translated_end.x - translated_start.x) + fringe * 2.0
                    ),
                    .height = static_cast<float>(
                        std::abs(translated_end.y - translated_start.y) + fringe * 2.0
                    ),
                };

                const auto bounds = render_detail::pixel_bounds(quad, state->width, state->height);

                if (bounds.left >= bounds.right || bounds.top >= bounds.bottom) {
                    return VulkanRenderResult::complete;
                }

                if (stats.submitted == kVulkanMaxRects * kVulkanMaxRectDraws) {
                    abandon_frame(*state, *stored, pass_open);

                    return VulkanRenderResult::insufficient_capacity;
                }

                if (pending_valid && (pending_pipe != DrawPipe::stroke || set != pending_set)) {
                    ensure_pass();

                    draw_pending(
                        pending_pipe,
                        run_base,
                        pipe_counts[pipe_index(pending_pipe)],
                        pending_set
                    );

                    pipe_counts[pipe_index(pending_pipe)] = 0;
                    pending_valid = false;
                }

                if (!pending_valid) {
                    pending_pipe = DrawPipe::stroke;
                    pending_set = VkDescriptorSet{};
                    run_base = pipe_slots[pipe_index(DrawPipe::stroke)];
                    pending_valid = true;
                }

                constexpr auto channel_scale = 1.0F / 255.0F;
                const auto slot = pipe_slots[pipe_index(DrawPipe::stroke)]++;

                strokes[slot] = vulkan_detail::StrokeInstance{
                    .min_x = static_cast<float>(bounds.left),
                    .min_y = static_cast<float>(bounds.top),
                    .max_x = static_cast<float>(bounds.right),
                    .max_y = static_cast<float>(bounds.bottom),
                    .red = static_cast<float>(color.red) * channel_scale,
                    .green = static_cast<float>(color.green) * channel_scale,
                    .blue = static_cast<float>(color.blue) * channel_scale,
                    .alpha = static_cast<float>(color.alpha) * channel_scale,
                    .start_x = translated_start.x,
                    .start_y = translated_start.y,
                    .end_x = translated_end.x,
                    .end_y = translated_end.y,
                    .thickness = thickness,
                };

                ++pipe_counts[pipe_index(DrawPipe::stroke)];
                ++stats.submitted;

                if (pipe_counts[pipe_index(DrawPipe::stroke)] == kVulkanMaxRects) {
                    ensure_pass();

                    draw_pending(
                        DrawPipe::stroke,
                        run_base,
                        pipe_counts[pipe_index(DrawPipe::stroke)],
                        VkDescriptorSet{}
                    );

                    pipe_counts[pipe_index(DrawPipe::stroke)] = 0;
                    pending_valid = false;
                }

                return VulkanRenderResult::complete;
            };

            if (lined != nullptr) {
                const auto emitted = emit_segment(lined->start, lined->end);

                if (emitted != VulkanRenderResult::complete) {
                    return emitted;
                }
            } else {
                const auto point_count = polylined->points.size();
                const auto segment_total = polylined->closed ? point_count : point_count - 1;

                for (std::size_t segment = 0; segment < segment_total; ++segment) {
                    const auto emitted = emit_segment(
                        polylined->points[segment],
                        polylined->points[(segment + 1) % point_count]
                    );

                    if (emitted != VulkanRenderResult::complete) {
                        return emitted;
                    }
                }
            }

            continue;
        } else if (rounded_stroked != nullptr) {
            shape = rounded_stroked->bounds;
            color = rounded_stroked->color;

            if (!render_detail::valid_rect(shape)) {
                abandon_frame(*state, *stored, pass_open);

                return VulkanRenderResult::invalid_input;
            }

            if (!std::isfinite(rounded_stroked->thickness) || rounded_stroked->thickness < 0.0F ||
                !std::isfinite(rounded_stroked->radius) || rounded_stroked->radius < 0.0F) {
                abandon_frame(*state, *stored, pass_open);

                return VulkanRenderResult::invalid_input;
            }

            if (color.alpha == 0 || rounded_stroked->thickness == 0.0F) {
                continue;
            }

            const auto minimum_dimension = std::min(shape.width, shape.height);

            if (minimum_dimension == 0.0F) {
                continue;
            }

            if (rounded_stroked->thickness >= minimum_dimension) {
                radius = std::min(rounded_stroked->radius, minimum_dimension * 0.5F);
                pipe = (radius > 0.0F || stored->unified) ? DrawPipe::rounded : DrawPipe::rect;
            } else {
                const auto half_thickness = rounded_stroked->thickness * 0.5F;
                const auto clamped_radius =
                    std::min(rounded_stroked->radius, minimum_dimension * 0.5F);
                thickness = rounded_stroked->thickness;
                radius = std::max(0.0F, clamped_radius - half_thickness);
                pipe = DrawPipe::rounded_stroke;
            }
        } else {
            shape = fill != nullptr ? fill->bounds : rounded_fill->bounds;
            color = fill != nullptr ? fill->color : rounded_fill->color;

            if (!render_detail::valid_rect(shape)) {
                abandon_frame(*state, *stored, pass_open);

                return VulkanRenderResult::invalid_input;
            }

            if (color.alpha == 0) {
                continue;
            }

            if (rounded_fill != nullptr || stored->unified) {
                const auto diameter =
                    std::min(static_cast<float>(shape.width), static_cast<float>(shape.height));

                radius = rounded_fill != nullptr ? std::min(rounded_fill->radius, diameter * 0.5F)
                                                 : 0.0F;

                if (radius > 0.0F || stored->unified) {
                    pipe = DrawPipe::rounded;
                }
            }
        }

        constexpr auto channel_scale = 1.0F / 255.0F;

        const auto translated = LayoutRect{
            .x = shape.x + translation.x,
            .y = shape.y + translation.y,
            .width = shape.width,
            .height = shape.height,
        };

        if (!render_detail::valid_rect(translated)) {
            abandon_frame(*state, *stored, pass_open);

            return VulkanRenderResult::invalid_input;
        }

        if (is_stroke) {
            if (pending_valid && pending_pipe != DrawPipe::rect) {
                ensure_pass();

                draw_pending(
                    pending_pipe,
                    run_base,
                    pipe_counts[pipe_index(pending_pipe)],
                    pending_set
                );

                pipe_counts[pipe_index(pending_pipe)] = 0;
                pending_valid = false;
            }

            const auto rect_index = pipe_index(DrawPipe::rect);
            LayoutRect bars[4]{};

            const auto bar_total =
                stroke_rect_bars(translated, thickness, state->width, state->height, bars);

            for (auto bar = std::size_t{}; bar < bar_total; ++bar) {
                const auto bar_bounds =
                    render_detail::pixel_bounds(bars[bar], state->width, state->height);

                if (bar_bounds.left >= bar_bounds.right || bar_bounds.top >= bar_bounds.bottom) {
                    continue;
                }

                if (stats.submitted == kVulkanMaxRects * kVulkanMaxRectDraws) {
                    abandon_frame(*state, *stored, pass_open);

                    return VulkanRenderResult::insufficient_capacity;
                }

                if (!pending_valid) {
                    pending_pipe = DrawPipe::rect;
                    pending_set = VkDescriptorSet{};
                    run_base = pipe_slots[rect_index];
                    pending_valid = true;
                }

                const auto slot = pipe_slots[rect_index]++;

                rects[slot] = vulkan_detail::RectInstance{
                    .min_x = static_cast<float>(bar_bounds.left),
                    .min_y = static_cast<float>(bar_bounds.top),
                    .max_x = static_cast<float>(bar_bounds.right),
                    .max_y = static_cast<float>(bar_bounds.bottom),
                    .red = static_cast<float>(color.red) * channel_scale,
                    .green = static_cast<float>(color.green) * channel_scale,
                    .blue = static_cast<float>(color.blue) * channel_scale,
                    .alpha = static_cast<float>(color.alpha) * channel_scale,
                };

                ++pipe_counts[rect_index];
                ++stats.submitted;

                if (pipe_counts[rect_index] == kVulkanMaxRects) {
                    ensure_pass();

                    draw_pending(DrawPipe::rect, run_base, pipe_counts[rect_index], set);

                    pipe_counts[rect_index] = 0;
                    pending_valid = false;
                }
            }

            continue;
        }

        const auto bounds = render_detail::pixel_bounds(translated, state->width, state->height);

        if (bounds.left >= bounds.right || bounds.top >= bounds.bottom) {
            continue;
        }

        if (drawn != nullptr) {
            const auto source_right = drawn->source.x + drawn->source.width;
            const auto source_bottom = drawn->source.y + drawn->source.height;

            if (drawn->source.width <= 0.0F || drawn->source.height <= 0.0F ||
                source_right <= 0.0F || source_bottom <= 0.0F) {
                continue;
            }
        }

        if (stats.submitted == kVulkanMaxRects * kVulkanMaxRectDraws) {
            abandon_frame(*state, *stored, pass_open);

            return VulkanRenderResult::insufficient_capacity;
        }

        const auto index = pipe_index(pipe);

        if (pending_valid && (pipe != pending_pipe || set != pending_set)) {
            ensure_pass();

            draw_pending(
                pending_pipe,
                run_base,
                pipe_counts[pipe_index(pending_pipe)],
                pending_set
            );

            pipe_counts[pipe_index(pending_pipe)] = 0;
            pending_valid = false;
        }

        if (!pending_valid) {
            pending_pipe = pipe;
            pending_set = set;
            run_base = pipe_slots[index];
            pending_valid = true;
        }

        if (pipe == DrawPipe::rect) {
            const auto slot = pipe_slots[index]++;

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
        } else if (pipe == DrawPipe::rounded) {
            const auto slot = pipe_slots[index]++;

            rounded[slot] = vulkan_detail::RoundedRectInstance{
                .min_x = static_cast<float>(bounds.left),
                .min_y = static_cast<float>(bounds.top),
                .max_x = static_cast<float>(bounds.right),
                .max_y = static_cast<float>(bounds.bottom),
                .red = static_cast<float>(color.red) * channel_scale,
                .green = static_cast<float>(color.green) * channel_scale,
                .blue = static_cast<float>(color.blue) * channel_scale,
                .alpha = static_cast<float>(color.alpha) * channel_scale,
                .center_x = translated.x + translated.width * 0.5F,
                .center_y = translated.y + translated.height * 0.5F,
                .half_x = translated.width * 0.5F,
                .half_y = translated.height * 0.5F,
                .radius = radius,
            };
        } else if (pipe == DrawPipe::image) {
            const auto& image = *drawn->image;
            const auto image_width = static_cast<float>(image.width);
            const auto image_height = static_cast<float>(image.height);
            const auto scale_x = drawn->source.width / translated.width;
            const auto scale_y = drawn->source.height / translated.height;
            const auto nearest_bias =
                drawn->sampling == veyra::SoftwareImageSampling::nearest ? 0.001F : 0.0F;
            const auto uv_bias_x = nearest_bias / image_width;
            const auto uv_bias_y = nearest_bias / image_height;
            const auto slot = pipe_slots[index]++;

            images[slot] = vulkan_detail::ImageInstance{
                .min_x = static_cast<float>(bounds.left),
                .min_y = static_cast<float>(bounds.top),
                .max_x = static_cast<float>(bounds.right),
                .max_y = static_cast<float>(bounds.bottom),
                .min_u =
                    (drawn->source.x + (static_cast<float>(bounds.left) - translated.x) * scale_x) /
                        image_width +
                    uv_bias_x,
                .min_v = (drawn->source.y + static_cast<float>(bounds.top) - translated.y) *
                        scale_y / image_height +
                    uv_bias_y,
                .max_u = (drawn->source.x +
                          (static_cast<float>(bounds.right) - translated.x) * scale_x) /
                        image_width +
                    uv_bias_x,
                .max_v = (drawn->source.y + static_cast<float>(bounds.bottom) - translated.y) *
                        scale_y / image_height +
                    uv_bias_y,
                .opacity = opacity,
            };
        } else if (pipe == DrawPipe::mask) {
            const auto& mask = *masked->mask;
            const auto slot = pipe_slots[index]++;

            const auto start_x = std::clamp(
                std::floor(
                    static_cast<double>(bounds.left) + 0.5 - static_cast<double>(translated.x)
                ),
                0.0,
                static_cast<double>(mask.width - 1)
            );
            const auto start_y = std::clamp(
                std::floor(
                    static_cast<double>(bounds.top) + 0.5 - static_cast<double>(translated.y)
                ),
                0.0,
                static_cast<double>(mask.height - 1)
            );

            masks[slot] = vulkan_detail::MaskInstance{
                .min_x = static_cast<float>(bounds.left),
                .min_y = static_cast<float>(bounds.top),
                .max_x = static_cast<float>(bounds.right),
                .max_y = static_cast<float>(bounds.bottom),
                .start_x = static_cast<float>(start_x),
                .start_y = static_cast<float>(start_y),
                .red = static_cast<float>(color.red) * channel_scale,
                .green = static_cast<float>(color.green) * channel_scale,
                .blue = static_cast<float>(color.blue) * channel_scale,
                .alpha = static_cast<float>(color.alpha) * channel_scale,
            };
        } else {
            const auto slot = pipe_slots[index]++;
            const auto half_thickness = thickness * 0.5F;

            rounded_strokes[slot] = vulkan_detail::RoundedStrokeInstance{
                .min_x = static_cast<float>(bounds.left),
                .min_y = static_cast<float>(bounds.top),
                .max_x = static_cast<float>(bounds.right),
                .max_y = static_cast<float>(bounds.bottom),
                .red = static_cast<float>(color.red) * channel_scale,
                .green = static_cast<float>(color.green) * channel_scale,
                .blue = static_cast<float>(color.blue) * channel_scale,
                .alpha = static_cast<float>(color.alpha) * channel_scale,
                .center_x = translated.x + translated.width * 0.5F,
                .center_y = translated.y + translated.height * 0.5F,
                .half_x = translated.width * 0.5F - half_thickness,
                .half_y = translated.height * 0.5F - half_thickness,
                .radius = radius,
                .thickness = thickness,
            };
        }

        ++pipe_counts[index];
        ++stats.submitted;

        if (pipe_counts[index] == kVulkanMaxRects) {
            ensure_pass();

            draw_pending(pipe, run_base, pipe_counts[index], set);

            pipe_counts[index] = 0;
            pending_valid = false;
        }
    }

    if (translation_depth != 0) {
        abandon_frame(*state, *stored, pass_open);

        return VulkanRenderResult::invalid_input;
    }

    if (pending_valid) {
        ensure_pass();

        draw_pending(pending_pipe, run_base, pipe_counts[pipe_index(pending_pipe)], pending_set);

        pipe_counts[pipe_index(pending_pipe)] = 0;
        pending_valid = false;
    }

    if (!pass_open) {
        begin_pass(*state, *stored);
        pass_open = true;
    }

    if (!end_frame(*state)) {
        vulkan_detail::evict_frame_images(*state, *stored);

        return VulkanRenderResult::device_error;
    }

    const auto byte_count = state->width * state->height * sizeof(SoftwarePixel);

    std::memcpy(output.data(), state->target.staging_mapped, byte_count);

    return VulkanRenderResult::complete;
}

}
