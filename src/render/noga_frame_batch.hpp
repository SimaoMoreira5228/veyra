#ifndef VEYRA_SRC_RENDER_NOGA_FRAME_BATCH_HPP
#define VEYRA_SRC_RENDER_NOGA_FRAME_BATCH_HPP

#include "noga_internal.hpp"
#include "veyra/render/noga_frame.hpp"
#include "veyra/render/software_renderer.hpp"

#include <NoGraphicsAPI/NoGraphicsAPI.hpp>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace veyra {

enum class NogaPipe : std::uint8_t {
    rect,
    rounded,
    image,
    mask,
    stroke,
    rounded_stroke,
    glyph,
};

constexpr std::size_t kNogaPipeCount = 7;

constexpr float kNogaChannelScale = 1.0F / 255.0F;

inline bool noga_clip_excludes(SoftwareClipRect clip, SoftwareClipRect bounds) noexcept {
    return bounds.left >= clip.right || bounds.right <= clip.left || bounds.top >= clip.bottom ||
        bounds.bottom <= clip.top;
}

struct NogaShapeStyle {
    NogaPipe pipe{NogaPipe::rect};
    std::uint32_t texture_slot{};
    std::uint32_t sampler_index{};
    LayoutRect shape{};
    DisplayColor color{};
    LayoutRect source{};
    const SoftwareImage* image{};
    const SoftwareMask* mask{};
    SoftwareImageSampling sampling{SoftwareImageSampling::nearest};
    float radius{};
    float opacity{1.0F};
    float thickness{};
    LayoutPoint glyph_uv{};
    bool skip{false};
};

struct NogaBatch {
    noga_detail::State& state;
    noga_detail::FramePipelines& stored;
    NogaFrameStats& stats;
    gpu::CommandBuffer* commands{};
    std::size_t pipe_slots[kNogaPipeCount]{};
    std::size_t pipe_counts[kNogaPipeCount]{};
    bool pass_open{};
    bool dead{};
    bool bound_valid{};
    NogaPipe bound{NogaPipe::rect};
    bool pending_valid{};
    NogaPipe pending_pipe{NogaPipe::rect};
    std::uint32_t pending_slot{};
    std::uint32_t pending_sampler{};
    std::size_t run_base{};
    bool scissor_dirty{true};
    SoftwareClipRect current_clip{};

    NogaBatch(
        noga_detail::State& state,
        noga_detail::FramePipelines& stored,
        NogaFrameStats& stats,
        gpu::CommandBuffer* commands
    ) noexcept : state(state), stored(stored), stats(stats), commands(commands) {
        current_clip = SoftwareClipRect{
            .left = 0,
            .top = 0,
            .right = state.width,
            .bottom = state.height,
        };
    }

    void ensure_pass() noexcept;
    void abandon() noexcept;
    void flush_pending() noexcept;
    void flush_unless(NogaPipe pipe, std::uint32_t slot, std::uint32_t sampler) noexcept;
    void set_clip(SoftwareClipRect clip) noexcept;
    void draw_run(NogaPipe pipe, std::size_t first, std::size_t count) noexcept;

    template <typename Instance>
    NogaRenderResult emit(
        NogaPipe pipe,
        std::uint32_t texture_slot,
        std::uint32_t sampler_index,
        const Instance& instance
    ) noexcept {
        if (stats.submitted == kNogaMaxSubmitted) {
            abandon();

            return NogaRenderResult::insufficient_capacity;
        }

        const auto index = static_cast<std::size_t>(pipe);

        flush_unless(pipe, texture_slot, sampler_index);

        if (!pending_valid) {
            pending_pipe = pipe;
            pending_slot = texture_slot;
            pending_sampler = sampler_index;
            run_base = pipe_slots[index];
            pending_valid = true;
        }

        const auto slot = pipe_slots[index]++;

        auto* base = static_cast<std::uint8_t*>(static_cast<void*>(stored.upload.range.cpu)) +
            stored.pipe_offsets[index];

        std::memcpy(base + slot * stored.pipe_strides[index], &instance, sizeof(Instance));

        ++pipe_counts[index];
        ++stats.submitted;

        if (pipe_counts[index] == noga_detail::kFramePipeSlots) {
            ensure_pass();

            draw_run(pipe, run_base, pipe_counts[index]);

            pipe_counts[index] = 0;
            pending_valid = false;
        }

        return NogaRenderResult::complete;
    }
};

struct NogaEmitter {
    NogaBatch& batch;
    noga_detail::State& state;
    noga_detail::FramePipelines& stored;

    NogaEmitter(
        NogaBatch& batch,
        noga_detail::State& state,
        noga_detail::FramePipelines& stored
    ) noexcept : batch(batch), state(state), stored(stored) {
    }

    NogaRenderResult resolve_image(const DrawImage& drawn, NogaShapeStyle& style) noexcept;
    NogaRenderResult resolve_mask(const DrawMask& masked, NogaShapeStyle& style) noexcept;
    NogaRenderResult resolve_glyph(const DrawGlyph& glyphed, NogaShapeStyle& style) noexcept;
    NogaRenderResult resolve_rounded_stroke(
        const StrokeRoundedRect& rounded_stroked,
        NogaShapeStyle& style
    ) noexcept;
    NogaRenderResult resolve_fill(
        const FillRect* fill,
        const FillRoundedRect* rounded_fill,
        NogaShapeStyle& style
    ) noexcept;
    NogaRenderResult
    emit_shape(const NogaShapeStyle& style, const LayoutPoint& translation) noexcept;
    NogaRenderResult
    emit_stroke_rect(const StrokeRect& stroked, const LayoutPoint& translation) noexcept;
    NogaRenderResult emit_line(const StrokeLine& lined, const LayoutPoint& translation) noexcept;
    NogaRenderResult
    emit_polyline(const StrokePolyline& polylined, const LayoutPoint& translation) noexcept;
    NogaRenderResult emit_path(const FillPath& pathed, const LayoutPoint& translation) noexcept;
};

}

#endif
