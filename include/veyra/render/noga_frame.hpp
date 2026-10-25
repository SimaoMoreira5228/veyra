#ifndef VEYRA_RENDER_NOGA_FRAME_HPP
#define VEYRA_RENDER_NOGA_FRAME_HPP

#include "veyra/render/damage_tracker.hpp"
#include "veyra/render/noga_rect.hpp"
#include "veyra/render/software_renderer.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace veyra {

constexpr std::size_t kNogaMaxFrameImages = 64;
constexpr std::size_t kNogaMaxFrameRects = 256;
constexpr std::size_t kNogaMaxSubmitted = 4096;

struct NogaFrameStats {
    std::size_t submitted{};
    std::size_t draws{};
    std::size_t rect_draws{};
    std::size_t rounded_draws{};
    std::size_t image_draws{};
    std::size_t mask_draws{};
    std::size_t stroke_draws{};
    std::size_t rounded_stroke_draws{};
    std::size_t glyph_draws{};
    std::size_t uploads{};
    std::size_t upload_bytes{};
    std::size_t damage_regions{};
    std::uint64_t damage_pixels{};
    std::size_t damage_skipped{};
    std::uint64_t gpu_ns{};
    bool gpu_timed{};
};

struct NogaFramePipeline {
    void* pipeline_state{};
    bool ready{};
};

NogaRenderResult
create_noga_frame_pipeline(NogaContext& context, NogaFramePipeline& pipeline) noexcept;

void destroy_noga_frame_pipeline(NogaContext& context, NogaFramePipeline& pipeline) noexcept;

NogaContextResult recover_noga_context(NogaContext& context, NogaFramePipeline& pipeline) noexcept;

NogaRenderResult render_noga_frame(
    NogaContext& context,
    NogaFramePipeline& pipeline,
    std::span<const DisplayCommand> commands,
    std::span<SoftwarePixel> output,
    NogaFrameStats& stats
) noexcept;

NogaRenderResult render_noga_frame_damaged(
    NogaContext& context,
    NogaFramePipeline& pipeline,
    std::span<const DisplayCommand> commands,
    std::span<const LayoutRect> damage,
    std::span<SoftwarePixel> output,
    NogaFrameStats& stats
) noexcept;

NogaRenderResult render_noga_frame_damaged(
    NogaContext& context,
    NogaFramePipeline& pipeline,
    std::span<const DisplayCommand> commands,
    const DamageTracker& damage,
    std::span<SoftwarePixel> output,
    NogaFrameStats& stats
) noexcept;

}

#endif
