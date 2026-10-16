#include "vulkan_frame_emit.hpp"

#include "software_renderer_internal.hpp"
#include "veyra/render/glyph_atlas.hpp"

#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <variant>

namespace veyra {
namespace {

constexpr std::size_t kPathSpanScratch = 256;

struct PathRectSink {
    FrameBatch& batch;
    SoftwareClipRect clip{};
    float red{};
    float green{};
    float blue{};
    float alpha{};
    VulkanRenderResult result{VulkanRenderResult::complete};

    void emit_span(render_detail::PathPixelSpan span) noexcept {
        if (result != VulkanRenderResult::complete) {
            return;
        }

        if (span.left >= clip.right || span.right <= clip.left) {
            return;
        }

        const auto instance = vulkan_detail::RectInstance{
            .min_x = static_cast<float>(span.left),
            .min_y = static_cast<float>(span.y),
            .max_x = static_cast<float>(span.right),
            .max_y = static_cast<float>(span.y + 1),
            .red = red,
            .green = green,
            .blue = blue,
            .alpha = alpha,
        };

        result = batch.emit(DrawPipe::rect, VkDescriptorSet{}, instance);
    }
};

}

FrameEmitter::FrameEmitter(
    FrameBatch& batch,
    vulkan_detail::ContextState& state,
    vulkan_detail::PipelineState& stored
) noexcept : batch(batch), state(state), stored(stored) {
}

VulkanRenderResult FrameEmitter::resolve_image(const DrawImage& drawn, ShapeStyle& style) noexcept {
    const auto* image = drawn.image;

    if (image == nullptr || !render_detail::valid_image(*image) ||
        !render_detail::valid_image_placement(drawn)) {
        batch.abandon();

        return VulkanRenderResult::invalid_input;
    }

    if (drawn.opacity == 0) {
        style.skip = true;

        return VulkanRenderResult::complete;
    }

    if (drawn.source.width <= 0.0F || drawn.source.height <= 0.0F) {
        style.skip = true;

        return VulkanRenderResult::complete;
    }

    const auto found = vulkan_detail::lookup_image(stored, *image, drawn.sampling);

    if (found == VK_NULL_HANDLE) {
        batch.abandon();

        return VulkanRenderResult::insufficient_capacity;
    }

    style.pipe = DrawPipe::image;
    style.set = found;
    style.opacity = static_cast<float>(drawn.opacity) * kFrameChannelScale;
    style.shape = drawn.destination;
    style.source = drawn.source;
    style.image = image;
    style.sampling = drawn.sampling;

    return VulkanRenderResult::complete;
}

VulkanRenderResult FrameEmitter::resolve_mask(const DrawMask& masked, ShapeStyle& style) noexcept {
    const auto* mask = masked.mask;

    if (mask == nullptr || mask->stride < mask->width ||
        mask->coverage.size() < mask->height * mask->stride) {
        batch.abandon();

        return VulkanRenderResult::invalid_input;
    }

    if (masked.color.alpha == 0) {
        style.skip = true;

        return VulkanRenderResult::complete;
    }

    const auto found = vulkan_detail::lookup_mask(stored, *mask);

    if (found == VK_NULL_HANDLE) {
        batch.abandon();

        return VulkanRenderResult::insufficient_capacity;
    }

    style.pipe = DrawPipe::mask;
    style.set = found;
    style.color = masked.color;
    style.mask = mask;
    style.shape = LayoutRect{
        .x = masked.x,
        .y = masked.y,
        .width = static_cast<float>(mask->width),
        .height = static_cast<float>(mask->height),
    };

    return VulkanRenderResult::complete;
}

VulkanRenderResult
FrameEmitter::resolve_glyph(const DrawGlyph& glyphed, ShapeStyle& style) noexcept {
    const auto* atlas = glyphed.atlas;

    if (atlas == nullptr || !std::isfinite(glyphed.x) || !std::isfinite(glyphed.y) ||
        glyphed.width == 0 || glyphed.height == 0 ||
        static_cast<std::uint64_t>(glyphed.u) + glyphed.width > atlas->width() ||
        static_cast<std::uint64_t>(glyphed.v) + glyphed.height > atlas->height()) {
        batch.abandon();

        return VulkanRenderResult::invalid_input;
    }

    if (glyphed.color.alpha == 0) {
        style.skip = true;

        return VulkanRenderResult::complete;
    }

    const auto& view = atlas->view();
    const auto found = vulkan_detail::lookup_mask(stored, view);

    if (found == VK_NULL_HANDLE) {
        batch.abandon();

        return VulkanRenderResult::insufficient_capacity;
    }

    style.pipe = DrawPipe::glyph;
    style.set = found;
    style.color = glyphed.color;
    style.shape = LayoutRect{
        .x = glyphed.x,
        .y = glyphed.y,
        .width = static_cast<float>(glyphed.width),
        .height = static_cast<float>(glyphed.height),
    };
    style.glyph_uv = LayoutPoint{
        .x = static_cast<float>(glyphed.u),
        .y = static_cast<float>(glyphed.v),
    };

    return VulkanRenderResult::complete;
}

VulkanRenderResult FrameEmitter::resolve_rounded_stroke(
    const StrokeRoundedRect& rounded_stroked,
    ShapeStyle& style
) noexcept {
    style.shape = rounded_stroked.bounds;
    style.color = rounded_stroked.color;

    if (!render_detail::valid_rect(style.shape)) {
        batch.abandon();

        return VulkanRenderResult::invalid_input;
    }

    if (!std::isfinite(rounded_stroked.thickness) || rounded_stroked.thickness < 0.0F ||
        !std::isfinite(rounded_stroked.radius) || rounded_stroked.radius < 0.0F) {
        batch.abandon();

        return VulkanRenderResult::invalid_input;
    }

    if (style.color.alpha == 0 || rounded_stroked.thickness == 0.0F) {
        style.skip = true;

        return VulkanRenderResult::complete;
    }

    const auto minimum_dimension = std::min(style.shape.width, style.shape.height);

    if (minimum_dimension == 0.0F) {
        style.skip = true;

        return VulkanRenderResult::complete;
    }

    if (rounded_stroked.thickness >= minimum_dimension) {
        style.radius = std::min(rounded_stroked.radius, minimum_dimension * 0.5F);
        style.pipe = (style.radius > 0.0F || stored.unified) ? DrawPipe::rounded : DrawPipe::rect;
    } else {
        const auto half_thickness = rounded_stroked.thickness * 0.5F;
        const auto clamped_radius = std::min(rounded_stroked.radius, minimum_dimension * 0.5F);
        style.thickness = rounded_stroked.thickness;
        style.radius = std::max(0.0F, clamped_radius - half_thickness);
        style.pipe = DrawPipe::rounded_stroke;
    }

    return VulkanRenderResult::complete;
}

VulkanRenderResult FrameEmitter::resolve_fill(
    const FillRect* fill,
    const FillRoundedRect* rounded_fill,
    ShapeStyle& style
) noexcept {
    style.shape = fill != nullptr ? fill->bounds : rounded_fill->bounds;
    style.color = fill != nullptr ? fill->color : rounded_fill->color;

    if (!render_detail::valid_rect(style.shape)) {
        batch.abandon();

        return VulkanRenderResult::invalid_input;
    }

    if (style.color.alpha == 0) {
        style.skip = true;

        return VulkanRenderResult::complete;
    }

    if (rounded_fill != nullptr || stored.unified) {
        const auto diameter =
            std::min(static_cast<float>(style.shape.width), static_cast<float>(style.shape.height));

        style.radius =
            rounded_fill != nullptr ? std::min(rounded_fill->radius, diameter * 0.5F) : 0.0F;

        if (style.radius > 0.0F || stored.unified) {
            style.pipe = DrawPipe::rounded;
        }
    }

    return VulkanRenderResult::complete;
}

VulkanRenderResult
FrameEmitter::emit_shape(const ShapeStyle& style, const LayoutPoint& translation) noexcept {
    const auto translated = LayoutRect{
        .x = style.shape.x + translation.x,
        .y = style.shape.y + translation.y,
        .width = style.shape.width,
        .height = style.shape.height,
    };

    if (!render_detail::valid_rect(translated)) {
        batch.abandon();

        return VulkanRenderResult::invalid_input;
    }

    const auto bounds = render_detail::pixel_bounds(translated, state.width, state.height);

    if (bounds.left >= bounds.right || bounds.top >= bounds.bottom) {
        return VulkanRenderResult::complete;
    }

    if (frame_clip_excludes(batch.current_clip, bounds)) {
        return VulkanRenderResult::complete;
    }

    if (style.pipe == DrawPipe::image) {
        const auto source_right = style.source.x + style.source.width;
        const auto source_bottom = style.source.y + style.source.height;

        if (style.source.width <= 0.0F || style.source.height <= 0.0F || source_right <= 0.0F ||
            source_bottom <= 0.0F) {
            return VulkanRenderResult::complete;
        }
    }

    batch.flush_unless(style.pipe, style.set);

    if (style.pipe == DrawPipe::rect) {
        const auto instance = vulkan_detail::RectInstance{
            .min_x = static_cast<float>(bounds.left),
            .min_y = static_cast<float>(bounds.top),
            .max_x = static_cast<float>(bounds.right),
            .max_y = static_cast<float>(bounds.bottom),
            .red = static_cast<float>(style.color.red) * kFrameChannelScale,
            .green = static_cast<float>(style.color.green) * kFrameChannelScale,
            .blue = static_cast<float>(style.color.blue) * kFrameChannelScale,
            .alpha = static_cast<float>(style.color.alpha) * kFrameChannelScale,
        };

        return batch.emit(style.pipe, style.set, instance);
    }

    if (style.pipe == DrawPipe::rounded) {
        const auto instance = vulkan_detail::RoundedRectInstance{
            .min_x = static_cast<float>(bounds.left),
            .min_y = static_cast<float>(bounds.top),
            .max_x = static_cast<float>(bounds.right),
            .max_y = static_cast<float>(bounds.bottom),
            .red = static_cast<float>(style.color.red) * kFrameChannelScale,
            .green = static_cast<float>(style.color.green) * kFrameChannelScale,
            .blue = static_cast<float>(style.color.blue) * kFrameChannelScale,
            .alpha = static_cast<float>(style.color.alpha) * kFrameChannelScale,
            .center_x = translated.x + translated.width * 0.5F,
            .center_y = translated.y + translated.height * 0.5F,
            .half_x = translated.width * 0.5F,
            .half_y = translated.height * 0.5F,
            .radius = style.radius,
        };

        return batch.emit(style.pipe, style.set, instance);
    }

    if (style.pipe == DrawPipe::image) {
        const auto& image = *style.image;
        const auto image_width = static_cast<float>(image.width);
        const auto image_height = static_cast<float>(image.height);
        const auto scale_x = style.source.width / translated.width;
        const auto scale_y = style.source.height / translated.height;
        const auto nearest_bias =
            style.sampling == veyra::SoftwareImageSampling::nearest ? 0.001F : 0.0F;
        const auto uv_bias_x = nearest_bias / image_width;
        const auto uv_bias_y = nearest_bias / image_height;

        const auto instance = vulkan_detail::ImageInstance{
            .min_x = static_cast<float>(bounds.left),
            .min_y = static_cast<float>(bounds.top),
            .max_x = static_cast<float>(bounds.right),
            .max_y = static_cast<float>(bounds.bottom),
            .min_u = (style.source.x + (static_cast<float>(bounds.left) - translated.x) * scale_x) /
                    image_width +
                uv_bias_x,
            .min_v = (style.source.y + static_cast<float>(bounds.top) - translated.y) * scale_y /
                    image_height +
                uv_bias_y,
            .max_u =
                (style.source.x + (static_cast<float>(bounds.right) - translated.x) * scale_x) /
                    image_width +
                uv_bias_x,
            .max_v = (style.source.y + static_cast<float>(bounds.bottom) - translated.y) * scale_y /
                    image_height +
                uv_bias_y,
            .opacity = style.opacity,
        };

        return batch.emit(style.pipe, style.set, instance);
    }

    if (style.pipe == DrawPipe::mask) {
        const auto& mask = *style.mask;

        const auto start_x = std::clamp(
            std::floor(static_cast<double>(bounds.left) + 0.5 - static_cast<double>(translated.x)),
            0.0,
            static_cast<double>(mask.width - 1)
        );
        const auto start_y = std::clamp(
            std::floor(static_cast<double>(bounds.top) + 0.5 - static_cast<double>(translated.y)),
            0.0,
            static_cast<double>(mask.height - 1)
        );

        const auto instance = vulkan_detail::MaskInstance{
            .min_x = static_cast<float>(bounds.left),
            .min_y = static_cast<float>(bounds.top),
            .max_x = static_cast<float>(bounds.right),
            .max_y = static_cast<float>(bounds.bottom),
            .start_x = static_cast<float>(start_x),
            .start_y = static_cast<float>(start_y),
            .red = static_cast<float>(style.color.red) * kFrameChannelScale,
            .green = static_cast<float>(style.color.green) * kFrameChannelScale,
            .blue = static_cast<float>(style.color.blue) * kFrameChannelScale,
            .alpha = static_cast<float>(style.color.alpha) * kFrameChannelScale,
        };

        return batch.emit(style.pipe, style.set, instance);
    }

    if (style.pipe == DrawPipe::rounded_stroke) {
        const auto half_thickness = style.thickness * 0.5F;

        const auto instance = vulkan_detail::RoundedStrokeInstance{
            .min_x = static_cast<float>(bounds.left),
            .min_y = static_cast<float>(bounds.top),
            .max_x = static_cast<float>(bounds.right),
            .max_y = static_cast<float>(bounds.bottom),
            .red = static_cast<float>(style.color.red) * kFrameChannelScale,
            .green = static_cast<float>(style.color.green) * kFrameChannelScale,
            .blue = static_cast<float>(style.color.blue) * kFrameChannelScale,
            .alpha = static_cast<float>(style.color.alpha) * kFrameChannelScale,
            .center_x = translated.x + translated.width * 0.5F,
            .center_y = translated.y + translated.height * 0.5F,
            .half_x = translated.width * 0.5F - half_thickness,
            .half_y = translated.height * 0.5F - half_thickness,
            .radius = style.radius,
            .thickness = style.thickness,
        };

        return batch.emit(style.pipe, style.set, instance);
    }

    const auto start_u = std::clamp(
        std::floor(static_cast<double>(bounds.left) + 0.5 - static_cast<double>(translated.x)),
        0.0,
        static_cast<double>(translated.width - 1.0F)
    );
    const auto start_v = std::clamp(
        std::floor(static_cast<double>(bounds.top) + 0.5 - static_cast<double>(translated.y)),
        0.0,
        static_cast<double>(translated.height - 1.0F)
    );

    const auto instance = vulkan_detail::GlyphInstance{
        .min_x = static_cast<float>(bounds.left),
        .min_y = static_cast<float>(bounds.top),
        .max_x = static_cast<float>(bounds.right),
        .max_y = static_cast<float>(bounds.bottom),
        .red = static_cast<float>(style.color.red) * kFrameChannelScale,
        .green = static_cast<float>(style.color.green) * kFrameChannelScale,
        .blue = static_cast<float>(style.color.blue) * kFrameChannelScale,
        .alpha = static_cast<float>(style.color.alpha) * kFrameChannelScale,
        .origin_u = style.glyph_uv.x + static_cast<float>(start_u),
        .origin_v = style.glyph_uv.y + static_cast<float>(start_v),
        .size_w = translated.width,
        .size_h = translated.height,
    };

    return batch.emit(style.pipe, style.set, instance);
}

VulkanRenderResult
FrameEmitter::emit_path(const FillPath& pathed, const LayoutPoint& translation) noexcept {
    auto intersection_capacity = std::size_t{};
    auto winding_capacity = std::size_t{};

    if (!render_detail::valid_path(pathed, intersection_capacity, winding_capacity) ||
        !render_detail::valid_translated_path(pathed, translation)) {
        batch.abandon();

        return VulkanRenderResult::invalid_input;
    }

    if (pathed.color.alpha == 0) {
        return VulkanRenderResult::complete;
    }

    const auto needs_windings = pathed.fill_rule == SoftwarePathFillRule::non_zero;

    if (intersection_capacity > kPathSpanScratch ||
        (needs_windings && winding_capacity > kPathSpanScratch)) {
        batch.abandon();

        return VulkanRenderResult::insufficient_capacity;
    }

    auto intersections = std::array<double, kPathSpanScratch>{};
    auto windings = std::array<std::int8_t, kPathSpanScratch>{};

    auto sink = PathRectSink{
        .batch = batch,
        .clip = batch.current_clip,
        .red = static_cast<float>(pathed.color.red) * kFrameChannelScale,
        .green = static_cast<float>(pathed.color.green) * kFrameChannelScale,
        .blue = static_cast<float>(pathed.color.blue) * kFrameChannelScale,
        .alpha = static_cast<float>(pathed.color.alpha) * kFrameChannelScale,
    };

    const auto emit_span = [](void* context, render_detail::PathPixelSpan span) noexcept {
        static_cast<PathRectSink*>(context)->emit_span(span);
    };

    render_detail::fill_path_spans(
        pathed,
        translation,
        state.width,
        state.height,
        SoftwareClipRect{
            .left = 0,
            .top = 0,
            .right = state.width,
            .bottom = state.height,
        },
        intersections,
        windings,
        &sink,
        emit_span
    );

    return sink.result;
}

}
