#include "vulkan_frame_emit.hpp"

#include "software_renderer_internal.hpp"

#include <vulkan/vulkan.h>

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
VulkanRenderResult emit_stroke_segment(
    FrameBatch& batch,
    vulkan_detail::ContextState& state,
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

        return VulkanRenderResult::invalid_input;
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
        return VulkanRenderResult::complete;
    }

    if (frame_clip_excludes(batch.current_clip, bounds)) {
        return VulkanRenderResult::complete;
    }

    const auto instance = vulkan_detail::StrokeInstance{
        .min_x = static_cast<float>(bounds.left),
        .min_y = static_cast<float>(bounds.top),
        .max_x = static_cast<float>(bounds.right),
        .max_y = static_cast<float>(bounds.bottom),
        .red = static_cast<float>(color.red) * kFrameChannelScale,
        .green = static_cast<float>(color.green) * kFrameChannelScale,
        .blue = static_cast<float>(color.blue) * kFrameChannelScale,
        .alpha = static_cast<float>(color.alpha) * kFrameChannelScale,
        .start_x = translated_start.x,
        .start_y = translated_start.y,
        .end_x = translated_end.x,
        .end_y = translated_end.y,
        .thickness = thickness,
    };

    return batch.emit(DrawPipe::stroke, VkDescriptorSet{}, instance);
}
}

VulkanRenderResult
FrameEmitter::emit_stroke_rect(const StrokeRect& stroked, const LayoutPoint& translation) noexcept {
    const auto shape = stroked.bounds;
    const auto color = stroked.color;

    if (!render_detail::valid_rect(shape)) {
        batch.abandon();

        return VulkanRenderResult::invalid_input;
    }

    if (!std::isfinite(stroked.thickness) || stroked.thickness < 0.0F) {
        batch.abandon();

        return VulkanRenderResult::invalid_input;
    }

    if (color.alpha == 0 || stroked.thickness == 0.0F) {
        return VulkanRenderResult::complete;
    }

    batch.flush_unless(DrawPipe::rect, VkDescriptorSet{});

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

        if (frame_clip_excludes(batch.current_clip, bar_bounds)) {
            continue;
        }

        const auto instance = vulkan_detail::RectInstance{
            .min_x = static_cast<float>(bar_bounds.left),
            .min_y = static_cast<float>(bar_bounds.top),
            .max_x = static_cast<float>(bar_bounds.right),
            .max_y = static_cast<float>(bar_bounds.bottom),
            .red = static_cast<float>(color.red) * kFrameChannelScale,
            .green = static_cast<float>(color.green) * kFrameChannelScale,
            .blue = static_cast<float>(color.blue) * kFrameChannelScale,
            .alpha = static_cast<float>(color.alpha) * kFrameChannelScale,
        };

        const auto emitted = batch.emit(DrawPipe::rect, VkDescriptorSet{}, instance);

        if (emitted != VulkanRenderResult::complete) {
            return emitted;
        }
    }

    return VulkanRenderResult::complete;
}

VulkanRenderResult
FrameEmitter::emit_line(const StrokeLine& lined, const LayoutPoint& translation) noexcept {
    if (!std::isfinite(lined.thickness) || lined.thickness < 0.0F) {
        batch.abandon();

        return VulkanRenderResult::invalid_input;
    }

    if (lined.color.alpha == 0 || lined.thickness == 0.0F) {
        return VulkanRenderResult::complete;
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

VulkanRenderResult FrameEmitter::emit_polyline(
    const StrokePolyline& polylined,
    const LayoutPoint& translation
) noexcept {
    if (!std::isfinite(polylined.thickness) || polylined.thickness < 0.0F) {
        batch.abandon();

        return VulkanRenderResult::invalid_input;
    }

    if (polylined.color.alpha == 0 || polylined.thickness == 0.0F) {
        return VulkanRenderResult::complete;
    }

    if (polylined.points.size() < 2) {
        batch.abandon();

        return VulkanRenderResult::invalid_input;
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

        if (emitted != VulkanRenderResult::complete) {
            return emitted;
        }
    }

    return VulkanRenderResult::complete;
}
}
