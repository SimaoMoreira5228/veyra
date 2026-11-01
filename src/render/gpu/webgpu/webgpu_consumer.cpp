#include "veyra/render/gpu/webgpu/webgpu_consumer.hpp"

#include <algorithm>
#include <variant>

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

    struct ClipEntry {
        std::uint64_t id{};
        LayoutRect bounds{};
    };

    const auto intersect = [](LayoutRect left, LayoutRect right) noexcept {
        const auto x0 = std::max(left.x, right.x);
        const auto y0 = std::max(left.y, right.y);
        const auto x1 = std::min(left.x + left.width, right.x + right.width);
        const auto y1 = std::min(left.y + left.height, right.y + right.height);
        if (x1 <= x0 || y1 <= y0) {
            return LayoutRect{};
        }

        return LayoutRect{
            .x = x0,
            .y = y0,
            .width = x1 - x0,
            .height = y1 - y0,
        };
    };

    auto clip_entries = std::array<ClipEntry, 64>{};
    auto clip_stack = std::array<LayoutRect, 64>{};
    auto clip_count = std::size_t{};
    auto clip_depth = std::size_t{};
    for (const auto& command : commands) {
        if (const auto* clip = std::get_if<PushClip>(&command)) {
            const auto parent = clip_depth == 0
                ? LayoutRect{.width = static_cast<float>(width), .height = static_cast<float>(height)}
                : clip_stack[clip_depth - 1];
            const auto effective = intersect(parent, clip->bounds);
            if (clip_depth < clip_stack.size()) {
                clip_stack[clip_depth] = effective;
                ++clip_depth;
            }
            if (clip_count < clip_entries.size()) {
                clip_entries[clip_count++] = {.id = clip->clip_id, .bounds = effective};
            }

            continue;
        }
        if (std::holds_alternative<PopClip>(command) && clip_depth != 0) {
            --clip_depth;
        }
    }

    const auto find_clip = [&clip_entries,
                            clip_count](std::uint64_t id) noexcept -> const ClipEntry* {
        for (std::size_t index = clip_count; index > 0; --index) {
            if (clip_entries[index - 1].id == id) {
                return &clip_entries[index - 1];
            }
        }

        return nullptr;
    };

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
        const auto* clip =
            run.key.clip_id == 0 && run.key.clip_depth == 0 ? nullptr : find_clip(run.key.clip_id);
        plan.batches[index] = {
            .pipeline = webgpu_pipeline(run.key.primitive),
            .resource = run.key.resource,
            .sampling = run.key.sampling,
            .clip_id = run.key.clip_id,
            .clip_depth = run.key.clip_depth,
            .clip_bounds = clip == nullptr ? LayoutRect{} : clip->bounds,
            .clipped = run.key.clip_depth != 0,
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
