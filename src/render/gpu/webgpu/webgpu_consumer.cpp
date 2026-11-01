#include "veyra/render/gpu/webgpu/webgpu_consumer.hpp"

namespace veyra {

WebGpuPipeline webgpu_pipeline(BatchPrimitive primitive) noexcept {
    switch (primitive) {
    case BatchPrimitive::fill_rect:
        return WebGpuPipeline::fill_rect;
    case BatchPrimitive::fill_rounded_rect:
        return WebGpuPipeline::fill_rounded_rect;
    case BatchPrimitive::stroke_rect:
        return WebGpuPipeline::stroke_rect;
    case BatchPrimitive::stroke_rounded_rect:
        return WebGpuPipeline::stroke_rounded_rect;
    case BatchPrimitive::stroke_line:
        return WebGpuPipeline::stroke_line;
    case BatchPrimitive::stroke_polyline:
        return WebGpuPipeline::stroke_polyline;
    case BatchPrimitive::draw_mask:
        return WebGpuPipeline::draw_mask;
    case BatchPrimitive::draw_glyph:
        return WebGpuPipeline::draw_glyph;
    case BatchPrimitive::fill_path:
        return WebGpuPipeline::fill_path;
    case BatchPrimitive::draw_image:
        return WebGpuPipeline::draw_image;
    }

    return WebGpuPipeline::fill_rect;
}

std::string_view webgpu_pipeline_name(WebGpuPipeline pipeline) noexcept {
    switch (pipeline) {
    case WebGpuPipeline::fill_rect:
        return "fill_rect";
    case WebGpuPipeline::fill_rounded_rect:
        return "fill_rounded_rect";
    case WebGpuPipeline::stroke_rect:
        return "stroke_rect";
    case WebGpuPipeline::stroke_rounded_rect:
        return "stroke_rounded_rect";
    case WebGpuPipeline::stroke_line:
        return "stroke_line";
    case WebGpuPipeline::stroke_polyline:
        return "stroke_polyline";
    case WebGpuPipeline::draw_mask:
        return "draw_mask";
    case WebGpuPipeline::draw_glyph:
        return "draw_glyph";
    case WebGpuPipeline::fill_path:
        return "fill_path";
    case WebGpuPipeline::draw_image:
        return "draw_image";
    }

    return "?";
}

WebGpuConsumer::WebGpuConsumer(WebGpuSink& sink) noexcept : sink_(&sink) {
}

WebGpuPlanResult WebGpuConsumer::submit(
    std::span<const DisplayCommand> commands,
    std::uint32_t width,
    std::uint32_t height,
    WebGpuFramePlan& plan
) noexcept {
    plan = {};
    plan.command_count = commands.size();

    auto runs = std::array<BatchRun, WebGpuFramePlan::kMaxBatches>{};
    auto clip_scopes = std::array<std::uint64_t, 64>{};
    auto run_count = std::size_t{};

    const auto analysis = analyze_display_batches(commands, runs, clip_scopes, run_count);
    if (analysis != BatchAnalysisResult::complete) {
        plan.overflowed = true;
        return WebGpuPlanResult::insufficient_capacity;
    }

    plan.batch_count = run_count;
    for (std::size_t index = 0; index < run_count; ++index) {
        const auto& run = runs[index];
        plan.batches[index] = {
            .pipeline = webgpu_pipeline(run.key.primitive),
            .resource = run.key.resource,
            .sampling = run.key.sampling,
            .clip_id = run.key.clip_id,
            .clip_depth = run.key.clip_depth,
            .start = run.start,
            .count = run.count,
            .sequence = run.sequence,
        };
    }

    sink_->begin_frame(width, height);
    for (std::size_t index = 0; index < plan.batch_count; ++index) {
        sink_->draw(plan.batches[index], commands);
    }
    sink_->end_frame();

    return WebGpuPlanResult::complete;
}

}
