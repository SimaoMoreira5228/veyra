#include "veyra/render/noga_frame.hpp"

#include "noga_frame_batch.hpp"
#include "noga_internal.hpp"
#include "software_renderer_internal.hpp"

#include <NoGraphicsAPI/NoGraphicsAPI.hpp>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

namespace veyra {
namespace {

bool damage_covers(SoftwareClipRect clip) noexcept {
    return clip.left < clip.right && clip.top < clip.bottom;
}

NogaRenderResult validate_damage(
    std::span<const LayoutRect> damage,
    std::size_t width,
    std::size_t height,
    bool& visible
) noexcept {
    visible = false;

    for (auto index = std::size_t{}; index < damage.size(); ++index) {
        if (!render_detail::valid_rect(damage[index])) {
            return NogaRenderResult::invalid_input;
        }

        const auto bounds = render_detail::pixel_bounds(damage[index], width, height);

        for (auto prior = std::size_t{}; prior < index; ++prior) {
            if (render_detail::overlaps(
                    bounds,
                    render_detail::pixel_bounds(damage[prior], width, height)
                )) {
                return NogaRenderResult::invalid_input;
            }
        }

        if (damage_covers(bounds)) {
            visible = true;
        }
    }

    return NogaRenderResult::complete;
}

NogaRenderResult render_damaged_regions(
    noga_detail::State& state,
    noga_detail::FramePipelines& stored,
    std::span<const DisplayCommand> commands,
    std::span<const LayoutRect> damage,
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
            gpu::end_commands(frame_commands);
            noga_detail::evict_frame_entries(state, stored);

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

    auto painted = std::size_t{};
    auto painted_pixels = std::uint64_t{};
    auto pass_opened = false;

    auto batch = NogaBatch{state, stored, stats, frame_commands};
    batch.load_existing = true;
    batch.damage_active = true;

    for (const auto& region : damage) {
        const auto clip = render_detail::pixel_bounds(region, state.width, state.height);

        if (!damage_covers(clip)) {
            continue;
        }

        ++painted;
        painted_pixels += static_cast<std::uint64_t>(clip.right - clip.left) *
            static_cast<std::uint64_t>(clip.bottom - clip.top);

        if (pass_opened) {
            gpu::barrier(
                frame_commands,
                gpu::Stage::color_output,
                gpu::Access::color_write,
                gpu::Stage::color_output,
                gpu::Access::color_read | gpu::Access::color_write
            );

            pass_opened = false;
        }

        batch.begin_region(clip);

        const auto replayed = replay_frame_commands(batch, commands);

        if (replayed != NogaRenderResult::complete) {
            return replayed;
        }

        batch.flush_pending();

        if (!batch.pass_open) {
            continue;
        }

        gpu::end_render_pass(frame_commands);
        pass_opened = true;
    }

    stats.damage_regions = painted;
    stats.damage_pixels = painted_pixels;

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

NogaRenderResult render_noga_frame_damaged(
    NogaContext& context,
    NogaFramePipeline& pipeline,
    std::span<const DisplayCommand> commands,
    std::span<const LayoutRect> damage,
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

    if (damage.empty()) {
        return NogaRenderResult::unchanged;
    }

    auto visible = false;
    const auto validated = validate_damage(damage, state->width, state->height, visible);

    if (validated != NogaRenderResult::complete) {
        return validated;
    }

    if (!visible) {
        return NogaRenderResult::unchanged;
    }

    const auto rendered = render_damaged_regions(*state, *stored, commands, damage, output, stats);

    stats.gpu_ns = 0;
    stats.gpu_timed = false;

    return rendered;
}

NogaRenderResult render_noga_frame_damaged(
    NogaContext& context,
    NogaFramePipeline& pipeline,
    std::span<const DisplayCommand> commands,
    const DamageTracker& damage,
    std::span<SoftwarePixel> output,
    NogaFrameStats& stats
) noexcept {
    if (!damage.valid() || damage.full_surface()) {
        return render_noga_frame(context, pipeline, commands, output, stats);
    }

    return render_noga_frame_damaged(context, pipeline, commands, damage.regions(), output, stats);
}

}
