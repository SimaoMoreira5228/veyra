#include "veyra/render/vulkan_rect_pipeline.hpp"

#include "software_renderer_internal.hpp"
#include "veyra/render/glyph_atlas.hpp"
#include "vulkan_frame_batch.hpp"
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

bool clip_excludes(SoftwareClipRect clip, SoftwareClipRect bounds) noexcept {
    return bounds.left >= clip.right || bounds.right <= clip.left || bounds.top >= clip.bottom ||
        bounds.bottom <= clip.top;
}

constexpr std::size_t kPathSpanScratch = 256;

struct PathRectSink {
    FrameBatch& batch;
    SoftwareClipRect clip{};
    float red{};
    float green{};
    float blue{};
    float alpha{};
    VulkanRenderResult result{VulkanRenderResult::complete};

    void emit_span(render_detail::PathPixelSpan span) noexcept {
        if (result != VulkanRenderResult::complete) {
            return;
        }

        if (span.left >= clip.right || span.right <= clip.left) {
            return;
        }

        const auto instance = vulkan_detail::RectInstance{
            .min_x = static_cast<float>(span.left),
            .min_y = static_cast<float>(span.y),
            .max_x = static_cast<float>(span.right),
            .max_y = static_cast<float>(span.y + 1),
            .red = red,
            .green = green,
            .blue = blue,
            .alpha = alpha,
        };

        result = batch.emit(DrawPipe::rect, VkDescriptorSet{}, instance);
    }
};

}

VulkanRenderResult end_frame(
    vulkan_detail::ContextState& state,
    std::uint64_t& gpu_ns,
    std::span<const LayoutRect> damage,
    const SoftwareClipRect* exposed,
    std::size_t exposed_count,
    bool full_copy
) noexcept {
    gpu_ns = 0;

    vkCmdEndRenderPass(state.commands);

    const auto copy_image = [&](SoftwareClipRect bounds) noexcept {
        const auto copy = VkBufferImageCopy{
            .bufferOffset = static_cast<VkDeviceSize>(
                (bounds.top * state.width + bounds.left) * sizeof(SoftwarePixel)
            ),
            .bufferRowLength = static_cast<std::uint32_t>(state.width),
            .bufferImageHeight = static_cast<std::uint32_t>(state.height),
            .imageSubresource =
                {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .layerCount = 1,
                },
            .imageOffset =
                {
                    .x = static_cast<std::int32_t>(bounds.left),
                    .y = static_cast<std::int32_t>(bounds.top),
                },
            .imageExtent = {
                .width = static_cast<std::uint32_t>(bounds.right - bounds.left),
                .height = static_cast<std::uint32_t>(bounds.bottom - bounds.top),
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
    };

    const auto to_transfer = VkImageMemoryBarrier{
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
        0,
        1,
        &to_transfer
    );

    if (full_copy) {
        copy_image(
            SoftwareClipRect{
                .left = 0,
                .top = 0,
                .right = state.width,
                .bottom = state.height,
            }
        );
    } else {
        for (const auto& region : damage) {
            const auto bounds = render_detail::pixel_bounds(region, state.width, state.height);

            if (bounds.left >= bounds.right || bounds.top >= bounds.bottom) {
                continue;
            }

            copy_image(bounds);
        }

        for (auto index = std::size_t{}; index < exposed_count; ++index) {
            copy_image(exposed[index]);
        }
    }

    const auto restore = VkImageMemoryBarrier{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = {},
        .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
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
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        0,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &restore
    );

    if (state.timestamp_pool != VK_NULL_HANDLE) {
        vkCmdWriteTimestamp(
            state.commands,
            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            state.timestamp_pool,
            1
        );
    }

    if (vkEndCommandBuffer(state.commands) != VK_SUCCESS) {
        return VulkanRenderResult::device_error;
    }

    vkResetFences(state.device, 1, &state.fence);

    const auto submit = VkSubmitInfo{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &state.commands,
    };

    const auto submitted = vkQueueSubmit(state.queue, 1, &submit, state.fence);

    if (submitted == VK_ERROR_DEVICE_LOST) {
        state.device_lost = true;

        return VulkanRenderResult::device_lost;
    }

    if (submitted != VK_SUCCESS) {
        return VulkanRenderResult::device_error;
    }

    constexpr auto timeout_ns = 10'000'000'000ULL;

    const auto waited = vkWaitForFences(state.device, 1, &state.fence, VK_TRUE, timeout_ns);

    if (waited == VK_ERROR_DEVICE_LOST) {
        state.device_lost = true;

        return VulkanRenderResult::device_lost;
    }

    if (waited != VK_SUCCESS) {
        return VulkanRenderResult::device_error;
    }

    if (state.timestamp_pool != VK_NULL_HANDLE) {
        auto ticks = std::array<std::uint64_t, 2>{};

        if (vkGetQueryPoolResults(
                state.device,
                state.timestamp_pool,
                0,
                2,
                sizeof(ticks),
                ticks.data(),
                sizeof(std::uint64_t),
                VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT
            ) == VK_SUCCESS) {
            const auto elapsed = (ticks[1] - ticks[0]) & state.timestamp_mask;
            gpu_ns = static_cast<std::uint64_t>(
                static_cast<double>(elapsed) * static_cast<double>(state.timestamp_period)
            );
        }
    }

    return VulkanRenderResult::complete;
}

static VulkanRenderResult render_frame(
    VulkanContext& context,
    VulkanRectPipeline& pipeline,
    std::span<const DisplayCommand> commands,
    std::span<SoftwarePixel> output,
    VulkanRectStats& stats,
    std::span<const LayoutRect> damage,
    bool full_repaint,
    std::int32_t scroll_x,
    std::int32_t scroll_y
) noexcept;

VulkanRenderResult render_vulkan_rects(
    VulkanContext& context,
    VulkanRectPipeline& pipeline,
    std::span<const DisplayCommand> commands,
    std::span<SoftwarePixel> output,
    VulkanRectStats& stats
) noexcept {
    return render_frame(context, pipeline, commands, output, stats, {}, true, 0, 0);
}

VulkanRenderResult render_vulkan_rects(
    VulkanContext& context,
    VulkanRectPipeline& pipeline,
    std::span<const DisplayCommand> commands,
    std::span<SoftwarePixel> output,
    VulkanRectStats& stats,
    std::span<const LayoutRect> damage
) noexcept {
    return render_frame(context, pipeline, commands, output, stats, damage, damage.empty(), 0, 0);
}

VulkanRenderResult render_vulkan_scrolled(
    VulkanContext& context,
    VulkanRectPipeline& pipeline,
    std::span<const DisplayCommand> commands,
    std::span<SoftwarePixel> output,
    VulkanRectStats& stats,
    std::span<const LayoutRect> damage,
    std::int32_t scroll_x,
    std::int32_t scroll_y
) noexcept {
    const auto wide_enough = scroll_x > -static_cast<std::int32_t>(context.width) &&
        scroll_x < static_cast<std::int32_t>(context.width);
    const auto high_enough = scroll_y > -static_cast<std::int32_t>(context.height) &&
        scroll_y < static_cast<std::int32_t>(context.height);

    return render_frame(
        context,
        pipeline,
        commands,
        output,
        stats,
        damage,
        !(wide_enough && high_enough),
        scroll_x,
        scroll_y
    );
}

static VulkanRenderResult render_frame(
    VulkanContext& context,
    VulkanRectPipeline& pipeline,
    std::span<const DisplayCommand> commands,
    std::span<SoftwarePixel> output,
    VulkanRectStats& stats,
    std::span<const LayoutRect> damage,
    bool full_repaint,
    std::int32_t scroll_x,
    std::int32_t scroll_y
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

    if (state->device_lost) {
        return VulkanRenderResult::device_lost;
    }

    const auto surface_width = static_cast<std::int64_t>(state->width);
    const auto surface_height = static_cast<std::int64_t>(state->height);
    const auto scroll_x_64 = static_cast<std::int64_t>(scroll_x);
    const auto scroll_y_64 = static_cast<std::int64_t>(scroll_y);
    const auto full = full_repaint || !state->target_valid;

    auto batch =
        FrameBatch{*state, *stored, stats, full ? state->shared_pass : state->shared_load_pass};

    if (!begin_commands(*state)) {
        return VulkanRenderResult::device_error;
    }

    if (state->timestamp_pool != VK_NULL_HANDLE) {
        vkCmdResetQueryPool(state->commands, state->timestamp_pool, 0, 2);
        vkCmdWriteTimestamp(
            state->commands,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            state->timestamp_pool,
            0
        );
    }

    stored->staging_used = 0;
    stored->frame_seq += 1;

    {
        auto frame_uploads = std::size_t{};
        const auto uploaded =
            vulkan_detail::upload_textures(*state, *stored, commands, frame_uploads);

        stats.uploads += frame_uploads;

        if (uploaded != VulkanRenderResult::complete) {
            batch.abandon();
            return uploaded;
        }
    }

    auto damage_union = SoftwareClipRect{
        .left = 0,
        .top = 0,
        .right = state->width,
        .bottom = state->height,
    };

    if (!damage.empty()) {
        damage_union = SoftwareClipRect{
            .left = state->width,
            .top = state->height,
            .right = 0,
            .bottom = 0,
        };

        for (const auto& region : damage) {
            if (!render_detail::valid_rect(region)) {
                batch.abandon();

                return VulkanRenderResult::invalid_input;
            }

            const auto bounds = render_detail::pixel_bounds(region, state->width, state->height);

            for (const auto& prior : damage) {
                if (&prior == &region) {
                    break;
                }

                if (render_detail::overlaps(
                        bounds,
                        render_detail::pixel_bounds(prior, state->width, state->height)
                    )) {
                    batch.abandon();

                    return VulkanRenderResult::invalid_input;
                }
            }

            damage_union.left = std::min(damage_union.left, bounds.left);
            damage_union.top = std::min(damage_union.top, bounds.top);
            damage_union.right = std::max(damage_union.right, bounds.right);
            damage_union.bottom = std::max(damage_union.bottom, bounds.bottom);
        }

        if (damage_union.left >= damage_union.right || damage_union.top >= damage_union.bottom) {
            return VulkanRenderResult::complete;
        }
    }

    const auto retain = !full && (scroll_x != 0 || scroll_y != 0);

    auto exposed = std::array<SoftwareClipRect, 2>{};
    auto exposed_count = std::size_t{};

    if (retain) {
        if (scroll_y_64 > 0) {
            exposed[exposed_count++] = SoftwareClipRect{
                .left = 0,
                .top = 0,
                .right = state->width,
                .bottom = static_cast<std::size_t>(scroll_y_64),
            };
        } else if (scroll_y_64 < 0) {
            exposed[exposed_count++] = SoftwareClipRect{
                .left = 0,
                .top = static_cast<std::size_t>(surface_height + scroll_y_64),
                .right = state->width,
                .bottom = state->height,
            };
        }

        if (scroll_x_64 != 0) {
            auto band_top = std::size_t{};
            auto band_bottom = state->height;

            if (scroll_y_64 > 0) {
                band_top = static_cast<std::size_t>(scroll_y_64);
            } else if (scroll_y_64 < 0) {
                band_bottom = static_cast<std::size_t>(surface_height + scroll_y_64);
            }

            if (scroll_x_64 > 0) {
                exposed[exposed_count++] = SoftwareClipRect{
                    .left = 0,
                    .top = band_top,
                    .right = static_cast<std::size_t>(scroll_x_64),
                    .bottom = band_bottom,
                };
            } else {
                exposed[exposed_count++] = SoftwareClipRect{
                    .left = static_cast<std::size_t>(surface_width + scroll_x_64),
                    .top = band_top,
                    .right = state->width,
                    .bottom = band_bottom,
                };
            }
        }

        for (auto exposed_index = std::size_t{}; exposed_index < exposed_count; ++exposed_index) {
            for (const auto& region : damage) {
                if (render_detail::overlaps(
                        exposed[exposed_index],
                        render_detail::pixel_bounds(region, state->width, state->height)
                    )) {
                    batch.abandon();

                    return VulkanRenderResult::invalid_input;
                }
            }
        }

        const auto source_left = static_cast<std::size_t>(scroll_x_64 < 0 ? -scroll_x_64 : 0);
        const auto source_top = static_cast<std::size_t>(scroll_y_64 < 0 ? -scroll_y_64 : 0);
        const auto source_right =
            static_cast<std::size_t>(scroll_x_64 > 0 ? surface_width - scroll_x_64 : surface_width);
        const auto source_bottom = static_cast<std::size_t>(
            scroll_y_64 > 0 ? surface_height - scroll_y_64 : surface_height
        );
        const auto destination_left = static_cast<std::size_t>(scroll_x_64 > 0 ? scroll_x_64 : 0);
        const auto destination_top = static_cast<std::size_t>(scroll_y_64 > 0 ? scroll_y_64 : 0);

        const auto acquire = VkImageMemoryBarrier{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_GENERAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = state->target.image,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .levelCount = 1,
                .layerCount = 1,
            },
        };

        vkCmdPipelineBarrier(
            state->commands,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0,
            0,
            nullptr,
            0,
            0,
            1,
            &acquire
        );

        const auto shift = VkBufferImageCopy{
            .bufferOffset = static_cast<VkDeviceSize>(
                (source_top * state->width + source_left) * sizeof(SoftwarePixel)
            ),
            .bufferRowLength = static_cast<std::uint32_t>(state->width),
            .bufferImageHeight = static_cast<std::uint32_t>(state->height),
            .imageSubresource =
                {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .layerCount = 1,
                },
            .imageOffset =
                {
                    .x = static_cast<std::int32_t>(source_left),
                    .y = static_cast<std::int32_t>(source_top),
                },
            .imageExtent = {
                .width = static_cast<std::uint32_t>(source_right - source_left),
                .height = static_cast<std::uint32_t>(source_bottom - source_top),
                .depth = 1,
            },
        };

        vkCmdCopyImageToBuffer(
            state->commands,
            state->target.image,
            VK_IMAGE_LAYOUT_GENERAL,
            state->target.staging,
            1,
            &shift
        );

        const auto place = VkBufferImageCopy{
            .bufferOffset = shift.bufferOffset,
            .bufferRowLength = shift.bufferRowLength,
            .bufferImageHeight = shift.bufferImageHeight,
            .imageSubresource = shift.imageSubresource,
            .imageOffset =
                {
                    .x = static_cast<std::int32_t>(destination_left),
                    .y = static_cast<std::int32_t>(destination_top),
                },
            .imageExtent = shift.imageExtent,
        };

        vkCmdCopyBufferToImage(
            state->commands,
            state->target.staging,
            state->target.image,
            VK_IMAGE_LAYOUT_GENERAL,
            1,
            &place
        );

        const auto release = VkImageMemoryBarrier{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .dstAccessMask = {},
            .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
            .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = state->target.image,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .levelCount = 1,
                .layerCount = 1,
            },
        };

        vkCmdPipelineBarrier(
            state->commands,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            0,
            0,
            nullptr,
            0,
            0,
            1,
            &release
        );
    }

    const auto region_at = [&](std::size_t index) noexcept {
        if (full) {
            return SoftwareClipRect{
                .left = 0,
                .top = 0,
                .right = state->width,
                .bottom = state->height,
            };
        }

        if (index < damage.size()) {
            return render_detail::pixel_bounds(damage[index], state->width, state->height);
        }

        return exposed[index - damage.size()];
    };

    const auto region_total = full ? std::size_t{1} : damage.size() + exposed_count;

    for (auto region = std::size_t{}; region < region_total; ++region) {
        batch.begin_region(region_at(region));

        auto translation = LayoutPoint{};
        auto translation_stack = std::array<LayoutPoint, 8>{};
        auto translation_depth = std::size_t{};
        auto clip_stack = std::array<SoftwareClipRect, 8>{};
        auto clip_depth = std::size_t{};

        for (const auto& command : commands) {
            if (const auto* push = std::get_if<PushTranslation>(&command)) {
                if (translation_depth >= translation_stack.size()) {
                    batch.abandon();

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
                    batch.abandon();

                    return VulkanRenderResult::invalid_input;
                }

                --translation_depth;
                translation = translation_stack[translation_depth];

                continue;
            }

            if (const auto* push_clip = std::get_if<PushClip>(&command)) {
                if (!render_detail::valid_rect(push_clip->bounds)) {
                    batch.abandon();

                    return VulkanRenderResult::invalid_input;
                }

                if (clip_depth >= clip_stack.size()) {
                    batch.abandon();

                    return VulkanRenderResult::insufficient_capacity;
                }

                clip_stack[clip_depth++] = batch.current_clip;

                auto translated = push_clip->bounds;
                translated.x += translation.x;
                translated.y += translation.y;

                const auto pushed =
                    render_detail::pixel_bounds(translated, state->width, state->height);

                batch.set_clip(render_detail::intersect(batch.current_clip, pushed));

                continue;
            }

            if (std::holds_alternative<PopClip>(command)) {
                if (clip_depth == 0) {
                    batch.abandon();

                    return VulkanRenderResult::invalid_input;
                }

                batch.set_clip(clip_stack[--clip_depth]);

                continue;
            }

            const auto* fill = std::get_if<FillRect>(&command);
            const auto* rounded_fill = std::get_if<FillRoundedRect>(&command);
            const auto* drawn = std::get_if<DrawImage>(&command);
            const auto* masked = std::get_if<DrawMask>(&command);
            const auto* glyphed = std::get_if<DrawGlyph>(&command);
            const auto* stroked = std::get_if<StrokeRect>(&command);
            const auto* pathed = std::get_if<FillPath>(&command);
            const auto* rounded_stroked = std::get_if<StrokeRoundedRect>(&command);
            const auto* lined = std::get_if<StrokeLine>(&command);
            const auto* polylined = std::get_if<StrokePolyline>(&command);

            if (fill == nullptr && rounded_fill == nullptr && drawn == nullptr &&
                masked == nullptr && glyphed == nullptr && stroked == nullptr &&
                pathed == nullptr && rounded_stroked == nullptr && lined == nullptr &&
                polylined == nullptr) {
                batch.abandon();

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
            LayoutPoint glyph_uv{};

            if (drawn != nullptr) {
                const auto* image = drawn->image;

                if (image == nullptr || !render_detail::valid_image(*image) ||
                    !render_detail::valid_image_placement(*drawn)) {
                    batch.abandon();

                    return VulkanRenderResult::invalid_input;
                }

                if (drawn->opacity == 0) {
                    continue;
                }

                const auto found = vulkan_detail::lookup_image(*stored, *image, drawn->sampling);

                if (found == VK_NULL_HANDLE) {
                    batch.abandon();

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
                    batch.abandon();

                    return VulkanRenderResult::invalid_input;
                }

                if (masked->color.alpha == 0) {
                    continue;
                }

                const auto found = vulkan_detail::lookup_mask(*stored, *mask);

                if (found == VK_NULL_HANDLE) {
                    batch.abandon();

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
            } else if (glyphed != nullptr) {
                const auto* atlas = glyphed->atlas;

                if (atlas == nullptr || !std::isfinite(glyphed->x) || !std::isfinite(glyphed->y) ||
                    glyphed->width == 0 || glyphed->height == 0 ||
                    static_cast<std::uint64_t>(glyphed->u) + glyphed->width > atlas->width() ||
                    static_cast<std::uint64_t>(glyphed->v) + glyphed->height > atlas->height()) {
                    batch.abandon();

                    return VulkanRenderResult::invalid_input;
                }

                if (glyphed->color.alpha == 0) {
                    continue;
                }

                const auto& view = atlas->view();
                const auto found = vulkan_detail::lookup_mask(*stored, view);

                if (found == VK_NULL_HANDLE) {
                    batch.abandon();

                    return VulkanRenderResult::insufficient_capacity;
                }

                pipe = DrawPipe::glyph;
                set = found;
                color = glyphed->color;
                shape = LayoutRect{
                    .x = glyphed->x,
                    .y = glyphed->y,
                    .width = static_cast<float>(glyphed->width),
                    .height = static_cast<float>(glyphed->height),
                };
                glyph_uv = LayoutPoint{
                    .x = static_cast<float>(glyphed->u),
                    .y = static_cast<float>(glyphed->v),
                };
            } else if (stroked != nullptr) {
                shape = stroked->bounds;
                color = stroked->color;

                if (!render_detail::valid_rect(shape)) {
                    batch.abandon();

                    return VulkanRenderResult::invalid_input;
                }

                if (!std::isfinite(stroked->thickness) || stroked->thickness < 0.0F) {
                    batch.abandon();

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
                    batch.abandon();

                    return VulkanRenderResult::invalid_input;
                }

                if (color.alpha == 0 || thickness == 0.0F) {
                    continue;
                }

                if (polylined != nullptr && polylined->points.size() < 2) {
                    batch.abandon();

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
                        batch.abandon();

                        return VulkanRenderResult::invalid_input;
                    }

                    const auto fringe = static_cast<double>(thickness) * 0.5 + 0.5;
                    const auto quad = LayoutRect{
                        .x = static_cast<float>(
                            std::min(translated_start.x, translated_end.x) - fringe
                        ),
                        .y = static_cast<float>(
                            std::min(translated_start.y, translated_end.y) - fringe
                        ),
                        .width = static_cast<float>(
                            std::abs(translated_end.x - translated_start.x) + fringe * 2.0
                        ),
                        .height = static_cast<float>(
                            std::abs(translated_end.y - translated_start.y) + fringe * 2.0
                        ),
                    };

                    const auto bounds =
                        render_detail::pixel_bounds(quad, state->width, state->height);

                    if (bounds.left >= bounds.right || bounds.top >= bounds.bottom) {
                        return VulkanRenderResult::complete;
                    }

                    if (clip_excludes(batch.current_clip, bounds)) {
                        return VulkanRenderResult::complete;
                    }

                    constexpr auto channel_scale = 1.0F / 255.0F;

                    const auto instance = vulkan_detail::StrokeInstance{
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

                    return batch.emit(DrawPipe::stroke, VkDescriptorSet{}, instance);
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
            } else if (pathed != nullptr) {
                auto intersection_capacity = std::size_t{};
                auto winding_capacity = std::size_t{};

                if (!render_detail::valid_path(*pathed, intersection_capacity, winding_capacity) ||
                    !render_detail::valid_translated_path(*pathed, translation)) {
                    batch.abandon();

                    return VulkanRenderResult::invalid_input;
                }

                if (pathed->color.alpha == 0) {
                    continue;
                }

                const auto needs_windings = pathed->fill_rule == SoftwarePathFillRule::non_zero;

                if (intersection_capacity > kPathSpanScratch ||
                    (needs_windings && winding_capacity > kPathSpanScratch)) {
                    batch.abandon();

                    return VulkanRenderResult::insufficient_capacity;
                }

                auto intersections = std::array<double, kPathSpanScratch>{};
                auto windings = std::array<std::int8_t, kPathSpanScratch>{};

                constexpr auto channel_scale = 1.0F / 255.0F;
                auto sink = PathRectSink{
                    .batch = batch,
                    .clip = batch.current_clip,
                    .red = static_cast<float>(pathed->color.red) * channel_scale,
                    .green = static_cast<float>(pathed->color.green) * channel_scale,
                    .blue = static_cast<float>(pathed->color.blue) * channel_scale,
                    .alpha = static_cast<float>(pathed->color.alpha) * channel_scale,
                };

                const auto emit_span = [](void* context,
                                          render_detail::PathPixelSpan span) noexcept {
                    static_cast<PathRectSink*>(context)->emit_span(span);
                };

                render_detail::fill_path_spans(
                    *pathed,
                    translation,
                    state->width,
                    state->height,
                    SoftwareClipRect{
                        .left = 0,
                        .top = 0,
                        .right = state->width,
                        .bottom = state->height,
                    },
                    intersections,
                    windings,
                    &sink,
                    emit_span
                );

                if (sink.result != VulkanRenderResult::complete) {
                    return sink.result;
                }

                continue;
            } else if (rounded_stroked != nullptr) {
                shape = rounded_stroked->bounds;
                color = rounded_stroked->color;

                if (!render_detail::valid_rect(shape)) {
                    batch.abandon();

                    return VulkanRenderResult::invalid_input;
                }

                if (!std::isfinite(rounded_stroked->thickness) ||
                    rounded_stroked->thickness < 0.0F || !std::isfinite(rounded_stroked->radius) ||
                    rounded_stroked->radius < 0.0F) {
                    batch.abandon();

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
                    batch.abandon();

                    return VulkanRenderResult::invalid_input;
                }

                if (color.alpha == 0) {
                    continue;
                }

                if (rounded_fill != nullptr || stored->unified) {
                    const auto diameter =
                        std::min(static_cast<float>(shape.width), static_cast<float>(shape.height));

                    radius = rounded_fill != nullptr
                        ? std::min(rounded_fill->radius, diameter * 0.5F)
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
                batch.abandon();

                return VulkanRenderResult::invalid_input;
            }

            if (is_stroke) {
                batch.flush_unless(DrawPipe::rect, VkDescriptorSet{});

                LayoutRect bars[4]{};

                const auto bar_total =
                    stroke_rect_bars(translated, thickness, state->width, state->height, bars);

                for (auto bar = std::size_t{}; bar < bar_total; ++bar) {
                    const auto bar_bounds =
                        render_detail::pixel_bounds(bars[bar], state->width, state->height);

                    if (bar_bounds.left >= bar_bounds.right ||
                        bar_bounds.top >= bar_bounds.bottom) {
                        continue;
                    }

                    if (clip_excludes(batch.current_clip, bar_bounds)) {
                        continue;
                    }

                    const auto instance = vulkan_detail::RectInstance{
                        .min_x = static_cast<float>(bar_bounds.left),
                        .min_y = static_cast<float>(bar_bounds.top),
                        .max_x = static_cast<float>(bar_bounds.right),
                        .max_y = static_cast<float>(bar_bounds.bottom),
                        .red = static_cast<float>(color.red) * channel_scale,
                        .green = static_cast<float>(color.green) * channel_scale,
                        .blue = static_cast<float>(color.blue) * channel_scale,
                        .alpha = static_cast<float>(color.alpha) * channel_scale,
                    };

                    const auto emitted = batch.emit(DrawPipe::rect, VkDescriptorSet{}, instance);

                    if (emitted != VulkanRenderResult::complete) {
                        return emitted;
                    }
                }

                continue;
            }

            const auto bounds =
                render_detail::pixel_bounds(translated, state->width, state->height);

            if (bounds.left >= bounds.right || bounds.top >= bounds.bottom) {
                continue;
            }

            if (clip_excludes(batch.current_clip, bounds)) {
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

            batch.flush_unless(pipe, set);

            if (pipe == DrawPipe::rect) {
                const auto instance = vulkan_detail::RectInstance{
                    .min_x = static_cast<float>(bounds.left),
                    .min_y = static_cast<float>(bounds.top),
                    .max_x = static_cast<float>(bounds.right),
                    .max_y = static_cast<float>(bounds.bottom),
                    .red = static_cast<float>(color.red) * channel_scale,
                    .green = static_cast<float>(color.green) * channel_scale,
                    .blue = static_cast<float>(color.blue) * channel_scale,
                    .alpha = static_cast<float>(color.alpha) * channel_scale,
                };

                const auto emitted = batch.emit(pipe, set, instance);

                if (emitted != VulkanRenderResult::complete) {
                    return emitted;
                }
            } else if (pipe == DrawPipe::rounded) {
                const auto instance = vulkan_detail::RoundedRectInstance{
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

                const auto emitted = batch.emit(pipe, set, instance);

                if (emitted != VulkanRenderResult::complete) {
                    return emitted;
                }
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

                const auto instance = vulkan_detail::ImageInstance{
                    .min_x = static_cast<float>(bounds.left),
                    .min_y = static_cast<float>(bounds.top),
                    .max_x = static_cast<float>(bounds.right),
                    .max_y = static_cast<float>(bounds.bottom),
                    .min_u = (drawn->source.x +
                              (static_cast<float>(bounds.left) - translated.x) * scale_x) /
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

                const auto emitted = batch.emit(pipe, set, instance);

                if (emitted != VulkanRenderResult::complete) {
                    return emitted;
                }
            } else if (pipe == DrawPipe::mask) {
                const auto& mask = *masked->mask;

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

                const auto instance = vulkan_detail::MaskInstance{
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

                const auto emitted = batch.emit(pipe, set, instance);

                if (emitted != VulkanRenderResult::complete) {
                    return emitted;
                }
            } else if (pipe == DrawPipe::rounded_stroke) {
                const auto half_thickness = thickness * 0.5F;

                const auto instance = vulkan_detail::RoundedStrokeInstance{
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

                const auto emitted = batch.emit(pipe, set, instance);

                if (emitted != VulkanRenderResult::complete) {
                    return emitted;
                }
            } else {
                const auto start_u = std::clamp(
                    std::floor(
                        static_cast<double>(bounds.left) + 0.5 - static_cast<double>(translated.x)
                    ),
                    0.0,
                    static_cast<double>(translated.width - 1.0F)
                );
                const auto start_v = std::clamp(
                    std::floor(
                        static_cast<double>(bounds.top) + 0.5 - static_cast<double>(translated.y)
                    ),
                    0.0,
                    static_cast<double>(translated.height - 1.0F)
                );

                const auto instance = vulkan_detail::GlyphInstance{
                    .min_x = static_cast<float>(bounds.left),
                    .min_y = static_cast<float>(bounds.top),
                    .max_x = static_cast<float>(bounds.right),
                    .max_y = static_cast<float>(bounds.bottom),
                    .red = static_cast<float>(color.red) * channel_scale,
                    .green = static_cast<float>(color.green) * channel_scale,
                    .blue = static_cast<float>(color.blue) * channel_scale,
                    .alpha = static_cast<float>(color.alpha) * channel_scale,
                    .origin_u = glyph_uv.x + static_cast<float>(start_u),
                    .origin_v = glyph_uv.y + static_cast<float>(start_v),
                    .size_w = translated.width,
                    .size_h = translated.height,
                };

                const auto emitted = batch.emit(pipe, set, instance);

                if (emitted != VulkanRenderResult::complete) {
                    return emitted;
                }
            }
        }

        if (translation_depth != 0) {
            batch.abandon();

            return VulkanRenderResult::invalid_input;
        }

        if (clip_depth != 0) {
            batch.abandon();

            return VulkanRenderResult::invalid_input;
        }

        batch.flush_pending();
    }

    batch.ensure_pass();

    auto gpu_ns = std::uint64_t{};

    const auto finished =
        end_frame(*state, gpu_ns, damage, exposed.data(), exposed_count, full || retain);

    if (finished != VulkanRenderResult::complete) {
        vulkan_detail::evict_frame_images(*state, *stored);

        return finished;
    }

    stats.gpu_ns = gpu_ns;
    stats.gpu_timed = state->timestamp_pool != VK_NULL_HANDLE;

    const auto copy_back = [&](SoftwareClipRect bounds) noexcept {
        const auto copy_row_bytes = (bounds.right - bounds.left) * sizeof(SoftwarePixel);

        for (auto y = bounds.top; y < bounds.bottom; ++y) {
            const auto destination = y * state->width + bounds.left;

            std::memcpy(
                output.data() + destination,
                static_cast<const SoftwarePixel*>(state->target.staging_mapped) + destination,
                copy_row_bytes
            );
        }
    };

    if (full || retain || (damage.empty() && exposed_count == 0)) {
        copy_back(
            SoftwareClipRect{
                .left = 0,
                .top = 0,
                .right = state->width,
                .bottom = state->height,
            }
        );
    } else {
        for (const auto& region : damage) {
            const auto bounds = render_detail::pixel_bounds(region, state->width, state->height);

            if (bounds.left >= bounds.right || bounds.top >= bounds.bottom) {
                continue;
            }

            copy_back(bounds);
        }

        for (auto index = std::size_t{}; index < exposed_count; ++index) {
            copy_back(exposed[index]);
        }
    }

    state->target_valid = true;

    return VulkanRenderResult::complete;
}

}
