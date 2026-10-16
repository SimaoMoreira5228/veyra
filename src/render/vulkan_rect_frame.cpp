#include "veyra/render/vulkan_rect_pipeline.hpp"

#include "software_renderer_internal.hpp"
#include "vulkan_frame_batch.hpp"
#include "vulkan_frame_emit.hpp"
#include "vulkan_frame_transfer.hpp"
#include "vulkan_internal.hpp"

#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <variant>

namespace veyra {

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

    const auto full = full_repaint || !state->target_valid;

    auto batch =
        FrameBatch{*state, *stored, stats, full ? state->shared_pass : state->shared_load_pass};

    if (!vulkan_detail::begin_frame_commands(*state)) {
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
        auto frame_bytes = std::size_t{};
        const auto uploaded =
            vulkan_detail::upload_textures(*state, *stored, commands, frame_uploads, frame_bytes);

        stats.uploads += frame_uploads;
        stats.upload_bytes += frame_bytes;

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
        const auto kept = vulkan_detail::retain_scrolled_image(
            *state,
            batch,
            damage,
            scroll_x,
            scroll_y,
            exposed,
            exposed_count
        );

        if (kept != VulkanRenderResult::complete) {
            return kept;
        }
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
        auto emitter = FrameEmitter{batch, *state, *stored};

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

            if (stroked != nullptr) {
                const auto emitted = emitter.emit_stroke_rect(*stroked, translation);

                if (emitted != VulkanRenderResult::complete) {
                    return emitted;
                }

                continue;
            }

            if (lined != nullptr) {
                const auto emitted = emitter.emit_line(*lined, translation);

                if (emitted != VulkanRenderResult::complete) {
                    return emitted;
                }

                continue;
            }

            if (polylined != nullptr) {
                const auto emitted = emitter.emit_polyline(*polylined, translation);

                if (emitted != VulkanRenderResult::complete) {
                    return emitted;
                }

                continue;
            }

            if (pathed != nullptr) {
                const auto emitted = emitter.emit_path(*pathed, translation);

                if (emitted != VulkanRenderResult::complete) {
                    return emitted;
                }

                continue;
            }

            auto style = ShapeStyle{};

            if (drawn != nullptr) {
                const auto resolved = emitter.resolve_image(*drawn, style);

                if (resolved != VulkanRenderResult::complete) {
                    return resolved;
                }
            } else if (masked != nullptr) {
                const auto resolved = emitter.resolve_mask(*masked, style);

                if (resolved != VulkanRenderResult::complete) {
                    return resolved;
                }
            } else if (glyphed != nullptr) {
                const auto resolved = emitter.resolve_glyph(*glyphed, style);

                if (resolved != VulkanRenderResult::complete) {
                    return resolved;
                }
            } else if (rounded_stroked != nullptr) {
                const auto resolved = emitter.resolve_rounded_stroke(*rounded_stroked, style);

                if (resolved != VulkanRenderResult::complete) {
                    return resolved;
                }
            } else {
                const auto resolved = emitter.resolve_fill(fill, rounded_fill, style);

                if (resolved != VulkanRenderResult::complete) {
                    return resolved;
                }
            }

            if (style.skip) {
                continue;
            }

            const auto emitted = emitter.emit_shape(style, translation);

            if (emitted != VulkanRenderResult::complete) {
                return emitted;
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

    const auto finished = vulkan_detail::end_frame_transfer(
        *state,
        gpu_ns,
        damage,
        exposed.data(),
        exposed_count,
        full || retain
    );

    if (finished != VulkanRenderResult::complete) {
        vulkan_detail::evict_frame_images(*state, *stored);

        return finished;
    }

    stats.gpu_ns = gpu_ns;
    stats.gpu_timed = state->timestamp_pool != VK_NULL_HANDLE;

    if (full || retain || (damage.empty() && exposed_count == 0)) {
        vulkan_detail::copy_frame_output(
            *state,
            output,
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

            vulkan_detail::copy_frame_output(*state, output, bounds);
        }

        for (auto index = std::size_t{}; index < exposed_count; ++index) {
            vulkan_detail::copy_frame_output(*state, output, exposed[index]);
        }
    }

    state->target_valid = true;

    return VulkanRenderResult::complete;
}

}
