#ifndef VEYRA_RENDER_DISPLAY_BATCHES_HPP
#define VEYRA_RENDER_DISPLAY_BATCHES_HPP

#include "veyra/render/software_renderer.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace veyra {

enum class BatchPrimitive : std::uint8_t {
    fill_rect,
    fill_rounded_rect,
    stroke_rect,
    stroke_rounded_rect,
    stroke_line,
    stroke_polyline,
    draw_mask,
    fill_path,
    draw_image,
};

struct BatchKey {
    BatchPrimitive primitive{BatchPrimitive::fill_rect};
    const void* resource{};
    SoftwareImageSampling sampling{SoftwareImageSampling::nearest};
    std::size_t clip_depth{};
    std::uint64_t clip_id{};
};

struct BatchRun {
    BatchKey key{};
    std::size_t start{};
    std::size_t count{};
    std::size_t sequence{};
};

enum class BatchAnalysisResult : std::uint8_t {
    complete,
    insufficient_output,
};

bool same_batch_key(const BatchKey& left, const BatchKey& right) noexcept;

BatchAnalysisResult analyze_display_batches(
    std::span<const DisplayCommand> commands,
    std::span<BatchRun> output,
    std::span<std::uint64_t> clip_scopes,
    std::size_t& produced
) noexcept;

}

#endif
