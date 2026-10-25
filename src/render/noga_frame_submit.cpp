#include "veyra/render/noga_frame.hpp"

#include "noga_frame_batch.hpp"
#include "noga_internal.hpp"
#include "software_renderer_internal.hpp"

#include <NoGraphicsAPI/NoGraphicsAPI.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <variant>

namespace veyra {

NogaRenderResult
replay_frame_commands(NogaBatch& batch, std::span<const DisplayCommand> commands) noexcept {
    auto translation = LayoutPoint{};
    auto translation_stack = std::array<LayoutPoint, noga_detail::kTranslationDepth>{};
    auto translation_depth = std::size_t{};
    auto clip_stack = std::array<SoftwareClipRect, noga_detail::kTranslationDepth>{};
    auto clip_depth = std::size_t{};
    auto emitter = NogaEmitter{batch, batch.state, batch.stored};

    for (const auto& command : commands) {
        if (const auto* push = std::get_if<PushTranslation>(&command)) {
            if (translation_depth >= translation_stack.size()) {
                batch.abandon();

                return NogaRenderResult::invalid_input;
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

                return NogaRenderResult::invalid_input;
            }

            --translation_depth;
            translation = translation_stack[translation_depth];

            continue;
        }

        if (const auto* push_clip = std::get_if<PushClip>(&command)) {
            if (!render_detail::valid_rect(push_clip->bounds)) {
                batch.abandon();

                return NogaRenderResult::invalid_input;
            }

            if (clip_depth >= clip_stack.size()) {
                batch.abandon();

                return NogaRenderResult::insufficient_capacity;
            }

            clip_stack[clip_depth++] = batch.current_clip;

            auto translated = push_clip->bounds;
            translated.x += translation.x;
            translated.y += translation.y;

            const auto pushed =
                render_detail::pixel_bounds(translated, batch.state.width, batch.state.height);

            batch.set_clip(render_detail::intersect(batch.current_clip, pushed));

            continue;
        }

        if (std::holds_alternative<PopClip>(command)) {
            if (clip_depth == 0) {
                batch.abandon();

                return NogaRenderResult::invalid_input;
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

        if (fill == nullptr && rounded_fill == nullptr && drawn == nullptr && masked == nullptr &&
            glyphed == nullptr && stroked == nullptr && pathed == nullptr &&
            rounded_stroked == nullptr && lined == nullptr && polylined == nullptr) {
            batch.abandon();

            return NogaRenderResult::unsupported_commands;
        }

        if (stroked != nullptr) {
            const auto emitted = emitter.emit_stroke_rect(*stroked, translation);

            if (emitted != NogaRenderResult::complete) {
                return emitted;
            }

            continue;
        }

        if (lined != nullptr) {
            const auto emitted = emitter.emit_line(*lined, translation);

            if (emitted != NogaRenderResult::complete) {
                return emitted;
            }

            continue;
        }

        if (polylined != nullptr) {
            const auto emitted = emitter.emit_polyline(*polylined, translation);

            if (emitted != NogaRenderResult::complete) {
                return emitted;
            }

            continue;
        }

        if (pathed != nullptr) {
            const auto emitted = emitter.emit_path(*pathed, translation);

            if (emitted != NogaRenderResult::complete) {
                return emitted;
            }

            continue;
        }

        auto style = NogaShapeStyle{};

        if (drawn != nullptr) {
            const auto resolved = emitter.resolve_image(*drawn, style);

            if (resolved != NogaRenderResult::complete) {
                return resolved;
            }
        } else if (masked != nullptr) {
            const auto resolved = emitter.resolve_mask(*masked, style);

            if (resolved != NogaRenderResult::complete) {
                return resolved;
            }
        } else if (glyphed != nullptr) {
            const auto resolved = emitter.resolve_glyph(*glyphed, style);

            if (resolved != NogaRenderResult::complete) {
                return resolved;
            }
        } else if (rounded_stroked != nullptr) {
            const auto resolved = emitter.resolve_rounded_stroke(*rounded_stroked, style);

            if (resolved != NogaRenderResult::complete) {
                return resolved;
            }
        } else {
            const auto resolved = emitter.resolve_fill(fill, rounded_fill, style);

            if (resolved != NogaRenderResult::complete) {
                return resolved;
            }
        }

        if (style.skip) {
            continue;
        }

        const auto emitted = emitter.emit_shape(style, translation);

        if (emitted != NogaRenderResult::complete) {
            return emitted;
        }
    }

    if (translation_depth != 0) {
        batch.abandon();

        return NogaRenderResult::invalid_input;
    }

    if (clip_depth != 0) {
        batch.abandon();

        return NogaRenderResult::invalid_input;
    }

    return NogaRenderResult::complete;
}

namespace {

NogaRenderResult render_frame(
    noga_detail::State& state,
    noga_detail::FramePipelines& stored,
    std::span<const DisplayCommand> commands,
    std::span<SoftwarePixel> output,
    NogaFrameStats& stats
) noexcept {
    gpu::wait_timeline({.semaphore = state.semaphore, .value = state.completed});
    gpu::reset_command_pool(state.pool);

    gpu::CommandBuffer* frame_commands = gpu::begin_commands(state.pool);

    if (frame_commands == nullptr) {
        return NogaRenderResult::device_error;
    }

    gpu::set_texture_descriptor_heap(frame_commands, stored.textures);
    gpu::set_sampler_descriptor_heap(frame_commands, stored.samplers);

    stored.frame_seq += 1;
    stored.staging_used = 0;
    stored.bump = stored.root_base;

    auto batch = NogaBatch{state, stored, stats, frame_commands};

    {
        auto frame_uploads = std::size_t{};
        auto frame_bytes = std::size_t{};
        const auto uploaded = noga_detail::upload_frame_textures(
            state,
            stored,
            frame_commands,
            commands,
            frame_uploads,
            frame_bytes
        );

        stats.uploads += frame_uploads;
        stats.upload_bytes += frame_bytes;

        if (uploaded != NogaRenderResult::complete) {
            batch.abandon();

            return uploaded;
        }
    }

    if (stats.uploads > 0) {
        gpu::barrier(
            frame_commands,
            gpu::Stage::transfer,
            gpu::Access::transfer_write,
            gpu::Stage::fragment,
            gpu::Access::shader_read
        );
    }

    const auto replayed = replay_frame_commands(batch, commands);

    if (replayed != NogaRenderResult::complete) {
        return replayed;
    }

    batch.flush_pending();
    batch.ensure_pass();

    gpu::end_render_pass(frame_commands);
    gpu::barrier(
        frame_commands,
        gpu::Stage::color_output,
        gpu::Access::color_write,
        gpu::Stage::transfer,
        gpu::Access::transfer_read
    );
    gpu::copy_texture_to_memory(
        frame_commands,
        state.target,
        {.gpu = state.readback.range.gpu, .size = state.readback.range.size}
    );
    gpu::barrier(
        frame_commands,
        gpu::Stage::transfer,
        gpu::Access::transfer_write,
        gpu::Stage::host,
        gpu::Access::host_read
    );
    gpu::end_commands(frame_commands);

    state.completed += 1;
    gpu::submit(
        state.device,
        {.commands = {frame_commands},
         .completion = {.semaphore = state.semaphore, .value = state.completed}}
    );
    gpu::wait_timeline({.semaphore = state.semaphore, .value = state.completed});

    std::memcpy(
        output.data(),
        static_cast<const void*>(state.readback.range.cpu),
        output.size() * sizeof(SoftwarePixel)
    );

    return NogaRenderResult::complete;
}

}

NogaRenderResult render_noga_frame(
    NogaContext& context,
    NogaFramePipeline& pipeline,
    std::span<const DisplayCommand> commands,
    std::span<SoftwarePixel> output,
    NogaFrameStats& stats
) noexcept {
    auto* state = noga_detail::state_of(context);
    auto* stored = noga_detail::frames_of(pipeline);

    if (state == nullptr || stored == nullptr || !context.ready || !pipeline.ready) {
        return NogaRenderResult::invalid_input;
    }

    if (output.size() != state->width * state->height || !state->target_ready) {
        return NogaRenderResult::invalid_input;
    }

    stats = NogaFrameStats{};

    const auto rendered = render_frame(*state, *stored, commands, output, stats);

    stats.gpu_ns = 0;
    stats.gpu_timed = false;

    return rendered;
}

}
