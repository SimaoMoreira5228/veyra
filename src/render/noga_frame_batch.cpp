#include "noga_frame_batch.hpp"

#include "noga/frame_shared.h"
#include "noga_internal.hpp"

#include <NoGraphicsAPI/NoGraphicsAPI.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace veyra {

namespace {

struct PackedRoot {
    void* items{};
    float extent_x{};
    float extent_y{};
    std::uint32_t texture_index{};
    std::uint32_t sampler_index{};
    std::uint32_t pad_a{};
    std::uint32_t pad_b{};
};

}

void NogaBatch::ensure_pass() noexcept {
    if (pass_open || dead) {
        return;
    }

    gpu::begin_render_pass(
        commands,
        {.colors = {{
             .render_view = state.view,
             .load = gpu::LoadOp::clear,
             .clear = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F},
         }}}
    );

    const auto width = static_cast<float>(state.width);
    const auto height = static_cast<float>(state.height);

    gpu::set_viewport(commands, {.width = width, .height = height});
    gpu::set_scissor(
        commands,
        {
            .width = static_cast<gpu::uint32>(state.width),
            .height = static_cast<gpu::uint32>(state.height),
        }
    );

    pass_open = true;
}

void NogaBatch::abandon() noexcept {
    if (dead) {
        return;
    }

    if (pass_open) {
        gpu::end_render_pass(commands);
        pass_open = false;
    }

    gpu::end_commands(commands);
    noga_detail::evict_frame_entries(state, stored);
    dead = true;
}

void NogaBatch::flush_pending() noexcept {
    if (!pending_valid || dead) {
        return;
    }

    ensure_pass();

    draw_run(pending_pipe, run_base, pipe_counts[static_cast<std::size_t>(pending_pipe)]);

    pipe_counts[static_cast<std::size_t>(pending_pipe)] = 0;
    pending_valid = false;
}

void NogaBatch::flush_unless(NogaPipe pipe, std::uint32_t slot, std::uint32_t sampler) noexcept {
    if (dead) {
        return;
    }

    if (pending_valid &&
        (pending_pipe != pipe || pending_slot != slot || pending_sampler != sampler)) {
        ensure_pass();

        draw_run(pending_pipe, run_base, pipe_counts[static_cast<std::size_t>(pending_pipe)]);

        pipe_counts[static_cast<std::size_t>(pending_pipe)] = 0;
        pending_valid = false;
    }
}

void NogaBatch::set_clip(SoftwareClipRect clip) noexcept {
    flush_pending();

    current_clip = clip;
    scissor_dirty = true;
}

void NogaBatch::draw_run(NogaPipe pipe, std::size_t first, std::size_t count) noexcept {
    if (dead || count == 0) {
        return;
    }

    if (scissor_dirty) {
        const auto width = current_clip.right - current_clip.left;
        const auto height = current_clip.bottom - current_clip.top;

        gpu::set_scissor(
            commands,
            {
                .x = static_cast<std::int32_t>(current_clip.left),
                .y = static_cast<std::int32_t>(current_clip.top),
                .width = static_cast<gpu::uint32>(width),
                .height = static_cast<gpu::uint32>(height),
            }
        );

        scissor_dirty = false;
    }

    if (!bound_valid || bound != pipe) {
        gpu::bind_pso(commands, stored.pipes[static_cast<std::size_t>(pipe)]);

        bound = pipe;
        bound_valid = true;
    }

    const auto index = static_cast<std::size_t>(pipe);
    const auto stride = stored.pipe_strides[index];
    auto* items_gpu = static_cast<std::uint8_t*>(static_cast<void*>(stored.upload.range.gpu)) +
        stored.pipe_offsets[index] + first * stride;

    const auto root_offset = stored.bump;

    stored.bump += sizeof(PackedRoot);

    auto* root_cpu =
        static_cast<std::uint8_t*>(static_cast<void*>(stored.upload.range.cpu)) + root_offset;
    const auto* root_gpu =
        static_cast<const std::uint8_t*>(static_cast<const void*>(stored.upload.range.gpu)) +
        root_offset;

    const auto packed = PackedRoot{
        .items = items_gpu,
        .extent_x = static_cast<float>(state.width),
        .extent_y = static_cast<float>(state.height),
        .texture_index = pending_slot,
        .sampler_index = pending_sampler,
    };

    std::memcpy(root_cpu, &packed, sizeof(PackedRoot));

    gpu::draw(commands, root_gpu, 6, static_cast<std::uint32_t>(count), 0, 0);

    if (pipe == NogaPipe::rect) {
        ++stats.rect_draws;
    } else if (pipe == NogaPipe::rounded) {
        ++stats.rounded_draws;
    } else if (pipe == NogaPipe::image) {
        ++stats.image_draws;
    } else if (pipe == NogaPipe::mask) {
        ++stats.mask_draws;
    } else if (pipe == NogaPipe::stroke) {
        ++stats.stroke_draws;
    } else if (pipe == NogaPipe::rounded_stroke) {
        ++stats.rounded_stroke_draws;
    } else {
        ++stats.glyph_draws;
    }

    ++stats.draws;
}

}
