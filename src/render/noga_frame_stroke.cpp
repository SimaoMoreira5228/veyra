#include "noga_frame_batch.hpp"

#include "noga/frame_shared.h"
#include "software_renderer_internal.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace veyra {
namespace {

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

NogaRenderResult emit_stroke_segment(
    NogaBatch& batch,
    noga_detail::State& state,
    LayoutPoint start,
    LayoutPoint end,
    LayoutPoint translation,
    float thickness,
    DisplayColor color
) noexcept {
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

        return NogaRenderResult::invalid_input;
    }

    const auto fringe = static_cast<double>(thickness) * 0.5 + 0.5;
    const auto quad = LayoutRect{
        .x = static_cast<float>(std::min(translated_start.x, translated_end.x) - fringe),
        .y = static_cast<float>(std::min(translated_start.y, translated_end.y) - fringe),
        .width = static_cast<float>(std::abs(translated_end.x - translated_start.x) + fringe * 2.0),
        .height =
            static_cast<float>(std::abs(translated_end.y - translated_start.y) + fringe * 2.0),
    };

    const auto bounds = render_detail::pixel_bounds(quad, state.width, state.height);

    if (bounds.left >= bounds.right || bounds.top >= bounds.bottom) {
        return NogaRenderResult::complete;
    }

    if (noga_clip_excludes(batch.current_clip, bounds)) {
        return NogaRenderResult::complete;
    }

    const auto instance = NogaFrameStroke{
        .bounds =
            {
                static_cast<float>(bounds.left),
                static_cast<float>(bounds.top),
                static_cast<float>(bounds.right),
                static_cast<float>(bounds.bottom),
            },
        .color =
            {
                static_cast<float>(color.red) * kNogaChannelScale,
                static_cast<float>(color.green) * kNogaChannelScale,
                static_cast<float>(color.blue) * kNogaChannelScale,
                static_cast<float>(color.alpha) * kNogaChannelScale,
            },
        .segment = {translated_start.x, translated_start.y, translated_end.x, translated_end.y},
        .thickness = thickness,
    };

    return batch.emit(NogaPipe::stroke, 0, 0, instance);
}

}

NogaRenderResult
NogaEmitter::emit_stroke_rect(const StrokeRect& stroked, const LayoutPoint& translation) noexcept {
    const auto shape = stroked.bounds;
    const auto color = stroked.color;

    if (!render_detail::valid_rect(shape)) {
        batch.abandon();

        return NogaRenderResult::invalid_input;
    }

    if (!std::isfinite(stroked.thickness) || stroked.thickness < 0.0F) {
        batch.abandon();

        return NogaRenderResult::invalid_input;
    }

    if (color.alpha == 0 || stroked.thickness == 0.0F) {
        return NogaRenderResult::complete;
    }

    batch.flush_unless(NogaPipe::rect, 0, 0);

    const auto translated = LayoutRect{
        .x = shape.x + translation.x,
        .y = shape.y + translation.y,
        .width = shape.width,
        .height = shape.height,
    };

    LayoutRect bars[4]{};

    const auto bar_total =
        stroke_rect_bars(translated, stroked.thickness, state.width, state.height, bars);

    for (auto bar = std::size_t{}; bar < bar_total; ++bar) {
        const auto bar_bounds = render_detail::pixel_bounds(bars[bar], state.width, state.height);

        if (bar_bounds.left >= bar_bounds.right || bar_bounds.top >= bar_bounds.bottom) {
            continue;
        }

        if (noga_clip_excludes(batch.current_clip, bar_bounds)) {
            continue;
        }

        const auto instance = NogaFrameRect{
            .bounds =
                {
                    static_cast<float>(bar_bounds.left),
                    static_cast<float>(bar_bounds.top),
                    static_cast<float>(bar_bounds.right),
                    static_cast<float>(bar_bounds.bottom),
                },
            .color = {
                static_cast<float>(color.red) * kNogaChannelScale,
                static_cast<float>(color.green) * kNogaChannelScale,
                static_cast<float>(color.blue) * kNogaChannelScale,
                static_cast<float>(color.alpha) * kNogaChannelScale,
            },
        };

        const auto emitted = batch.emit(NogaPipe::rect, 0, 0, instance);

        if (emitted != NogaRenderResult::complete) {
            return emitted;
        }
    }

    return NogaRenderResult::complete;
}

NogaRenderResult
NogaEmitter::emit_line(const StrokeLine& lined, const LayoutPoint& translation) noexcept {
    if (!std::isfinite(lined.thickness) || lined.thickness < 0.0F) {
        batch.abandon();

        return NogaRenderResult::invalid_input;
    }

    if (lined.color.alpha == 0 || lined.thickness == 0.0F) {
        return NogaRenderResult::complete;
    }

    return emit_stroke_segment(
        batch,
        state,
        lined.start,
        lined.end,
        translation,
        lined.thickness,
        lined.color
    );
}

NogaRenderResult NogaEmitter::emit_polyline(
    const StrokePolyline& polylined,
    const LayoutPoint& translation
) noexcept {
    if (!std::isfinite(polylined.thickness) || polylined.thickness < 0.0F) {
        batch.abandon();

        return NogaRenderResult::invalid_input;
    }

    if (polylined.color.alpha == 0 || polylined.thickness == 0.0F) {
        return NogaRenderResult::complete;
    }

    if (polylined.points.size() < 2) {
        batch.abandon();

        return NogaRenderResult::invalid_input;
    }

    const auto point_count = polylined.points.size();
    const auto segment_total = polylined.closed ? point_count : point_count - 1;

    for (std::size_t segment = 0; segment < segment_total; ++segment) {
        const auto emitted = emit_stroke_segment(
            batch,
            state,
            polylined.points[segment],
            polylined.points[(segment + 1) % point_count],
            translation,
            polylined.thickness,
            polylined.color
        );

        if (emitted != NogaRenderResult::complete) {
            return emitted;
        }
    }

    return NogaRenderResult::complete;
}

}
