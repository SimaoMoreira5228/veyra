#include "noga_frame_batch.hpp"

#include "noga/frame_shared.h"
#include "software_renderer_internal.hpp"
#include "veyra/render/glyph_atlas.hpp"

#include <NoGraphicsAPI/NoGraphicsAPI.hpp>

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
    NogaBatch& batch;
    SoftwareClipRect clip{};
    float red{};
    float green{};
    float blue{};
    float alpha{};
    NogaRenderResult result{NogaRenderResult::complete};

    void emit_span(render_detail::PathPixelSpan span) noexcept {
        if (result != NogaRenderResult::complete) {
            return;
        }

        const auto span_clip = SoftwareClipRect{
            .left = span.left,
            .top = span.y,
            .right = span.right,
            .bottom = span.y + 1,
        };

        if (batch.outside_damage(span_clip)) {
            return;
        }

        if (span.left >= clip.right || span.right <= clip.left) {
            return;
        }

        const auto instance = NogaFrameRect{
            .bounds =
                {
                    static_cast<float>(span.left),
                    static_cast<float>(span.y),
                    static_cast<float>(span.right),
                    static_cast<float>(span.y + 1),
                },
            .color = {red, green, blue, alpha},
        };

        result = batch.emit(NogaPipe::rect, 0, 0, instance);
    }
};

}

NogaRenderResult
NogaEmitter::resolve_image(const DrawImage& drawn, NogaShapeStyle& style) noexcept {
    const auto* image = drawn.image;

    if (image == nullptr || !render_detail::valid_image(*image) ||
        !render_detail::valid_image_placement(drawn)) {
        batch.abandon();

        return NogaRenderResult::invalid_input;
    }

    if (drawn.opacity == 0) {
        style.skip = true;

        return NogaRenderResult::complete;
    }

    if (drawn.source.width <= 0.0F || drawn.source.height <= 0.0F) {
        style.skip = true;

        return NogaRenderResult::complete;
    }

    const auto found = noga_detail::lookup_image(stored, *image, drawn.sampling);

    if (found == nullptr) {
        batch.abandon();

        return NogaRenderResult::insufficient_capacity;
    }

    style.pipe = NogaPipe::image;
    style.texture_slot = found->slot;
    style.sampler_index = drawn.sampling == SoftwareImageSampling::nearest ? 0 : 1;
    style.opacity = static_cast<float>(drawn.opacity) * kNogaChannelScale;
    style.shape = drawn.destination;
    style.source = drawn.source;
    style.image = image;
    style.sampling = drawn.sampling;

    return NogaRenderResult::complete;
}

NogaRenderResult NogaEmitter::resolve_mask(const DrawMask& masked, NogaShapeStyle& style) noexcept {
    const auto* mask = masked.mask;

    if (mask == nullptr || mask->stride < mask->width ||
        mask->coverage.size() < mask->height * mask->stride) {
        batch.abandon();

        return NogaRenderResult::invalid_input;
    }

    if (masked.color.alpha == 0) {
        style.skip = true;

        return NogaRenderResult::complete;
    }

    const auto found = noga_detail::lookup_mask(stored, *mask);

    if (found == nullptr) {
        batch.abandon();

        return NogaRenderResult::insufficient_capacity;
    }

    style.pipe = NogaPipe::mask;
    style.texture_slot = found->slot;
    style.color = masked.color;
    style.mask = mask;
    style.shape = LayoutRect{
        .x = masked.x,
        .y = masked.y,
        .width = static_cast<float>(mask->width),
        .height = static_cast<float>(mask->height),
    };

    return NogaRenderResult::complete;
}

NogaRenderResult
NogaEmitter::resolve_glyph(const DrawGlyph& glyphed, NogaShapeStyle& style) noexcept {
    const auto* atlas = glyphed.atlas;

    if (atlas == nullptr || !std::isfinite(glyphed.x) || !std::isfinite(glyphed.y) ||
        glyphed.width == 0 || glyphed.height == 0 ||
        static_cast<std::uint64_t>(glyphed.u) + glyphed.width > atlas->width() ||
        static_cast<std::uint64_t>(glyphed.v) + glyphed.height > atlas->height()) {
        batch.abandon();

        return NogaRenderResult::invalid_input;
    }

    if (glyphed.color.alpha == 0) {
        style.skip = true;

        return NogaRenderResult::complete;
    }

    const auto& view = atlas->view();
    const auto found = noga_detail::lookup_mask(stored, view);

    if (found == nullptr) {
        batch.abandon();

        return NogaRenderResult::insufficient_capacity;
    }

    style.pipe = NogaPipe::glyph;
    style.texture_slot = found->slot;
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

    return NogaRenderResult::complete;
}

NogaRenderResult NogaEmitter::resolve_rounded_stroke(
    const StrokeRoundedRect& rounded_stroked,
    NogaShapeStyle& style
) noexcept {
    style.shape = rounded_stroked.bounds;
    style.color = rounded_stroked.color;

    if (!render_detail::valid_rect(style.shape)) {
        batch.abandon();

        return NogaRenderResult::invalid_input;
    }

    if (!std::isfinite(rounded_stroked.thickness) || rounded_stroked.thickness < 0.0F ||
        !std::isfinite(rounded_stroked.radius) || rounded_stroked.radius < 0.0F) {
        batch.abandon();

        return NogaRenderResult::invalid_input;
    }

    if (style.color.alpha == 0 || rounded_stroked.thickness == 0.0F) {
        style.skip = true;

        return NogaRenderResult::complete;
    }

    const auto minimum_dimension = std::min(style.shape.width, style.shape.height);

    if (minimum_dimension == 0.0F) {
        style.skip = true;

        return NogaRenderResult::complete;
    }

    if (rounded_stroked.thickness >= minimum_dimension) {
        style.radius = std::min(rounded_stroked.radius, minimum_dimension * 0.5F);
        style.pipe = style.radius > 0.0F ? NogaPipe::rounded : NogaPipe::rect;
    } else {
        const auto half_thickness = rounded_stroked.thickness * 0.5F;
        const auto clamped_radius = std::min(rounded_stroked.radius, minimum_dimension * 0.5F);
        style.thickness = rounded_stroked.thickness;
        style.radius = std::max(0.0F, clamped_radius - half_thickness);
        style.pipe = NogaPipe::rounded_stroke;
    }

    return NogaRenderResult::complete;
}

NogaRenderResult NogaEmitter::resolve_fill(
    const FillRect* fill,
    const FillRoundedRect* rounded_fill,
    NogaShapeStyle& style
) noexcept {
    style.shape = fill != nullptr ? fill->bounds : rounded_fill->bounds;
    style.color = fill != nullptr ? fill->color : rounded_fill->color;

    if (!render_detail::valid_rect(style.shape)) {
        batch.abandon();

        return NogaRenderResult::invalid_input;
    }

    if (style.color.alpha == 0) {
        style.skip = true;

        return NogaRenderResult::complete;
    }

    if (rounded_fill != nullptr) {
        const auto diameter =
            std::min(static_cast<float>(style.shape.width), static_cast<float>(style.shape.height));

        style.radius = std::min(rounded_fill->radius, diameter * 0.5F);

        if (style.radius > 0.0F) {
            style.pipe = NogaPipe::rounded;
        }
    }

    return NogaRenderResult::complete;
}

NogaRenderResult
NogaEmitter::emit_shape(const NogaShapeStyle& style, const LayoutPoint& translation) noexcept {
    const auto translated = LayoutRect{
        .x = style.shape.x + translation.x,
        .y = style.shape.y + translation.y,
        .width = style.shape.width,
        .height = style.shape.height,
    };

    if (!render_detail::valid_rect(translated)) {
        batch.abandon();

        return NogaRenderResult::invalid_input;
    }

    const auto bounds = render_detail::pixel_bounds(translated, state.width, state.height);

    if (bounds.left >= bounds.right || bounds.top >= bounds.bottom) {
        return NogaRenderResult::complete;
    }

    if (batch.outside_damage(bounds)) {
        return NogaRenderResult::complete;
    }

    if (noga_clip_excludes(batch.current_clip, bounds)) {
        return NogaRenderResult::complete;
    }

    if (style.pipe == NogaPipe::image) {
        const auto source_right = style.source.x + style.source.width;
        const auto source_bottom = style.source.y + style.source.height;

        if (style.source.width <= 0.0F || style.source.height <= 0.0F || source_right <= 0.0F ||
            source_bottom <= 0.0F) {
            return NogaRenderResult::complete;
        }
    }

    if (style.pipe == NogaPipe::rect) {
        const auto instance = NogaFrameRect{
            .bounds =
                {
                    static_cast<float>(bounds.left),
                    static_cast<float>(bounds.top),
                    static_cast<float>(bounds.right),
                    static_cast<float>(bounds.bottom),
                },
            .color = {
                static_cast<float>(style.color.red) * kNogaChannelScale,
                static_cast<float>(style.color.green) * kNogaChannelScale,
                static_cast<float>(style.color.blue) * kNogaChannelScale,
                static_cast<float>(style.color.alpha) * kNogaChannelScale,
            },
        };

        return batch.emit(style.pipe, 0, 0, instance);
    }

    if (style.pipe == NogaPipe::rounded) {
        const auto instance = NogaFrameRounded{
            .bounds =
                {
                    static_cast<float>(bounds.left),
                    static_cast<float>(bounds.top),
                    static_cast<float>(bounds.right),
                    static_cast<float>(bounds.bottom),
                },
            .color =
                {
                    static_cast<float>(style.color.red) * kNogaChannelScale,
                    static_cast<float>(style.color.green) * kNogaChannelScale,
                    static_cast<float>(style.color.blue) * kNogaChannelScale,
                    static_cast<float>(style.color.alpha) * kNogaChannelScale,
                },
            .shape =
                {
                    translated.x + translated.width * 0.5F,
                    translated.y + translated.height * 0.5F,
                    translated.width * 0.5F,
                    translated.height * 0.5F,
                },
            .radius = style.radius,
        };

        return batch.emit(style.pipe, 0, 0, instance);
    }

    if (style.pipe == NogaPipe::image) {
        const auto& image = *style.image;
        const auto image_width = static_cast<float>(image.width);
        const auto image_height = static_cast<float>(image.height);
        const auto scale_x = style.source.width / translated.width;
        const auto scale_y = style.source.height / translated.height;
        const auto nearest_bias = style.sampling == SoftwareImageSampling::nearest ? 0.001F : 0.0F;
        const auto uv_bias_x = nearest_bias / image_width;
        const auto uv_bias_y = nearest_bias / image_height;

        const auto instance = NogaFrameImage{
            .bounds =
                {
                    static_cast<float>(bounds.left),
                    static_cast<float>(bounds.top),
                    static_cast<float>(bounds.right),
                    static_cast<float>(bounds.bottom),
                },
            .uv =
                {
                    (style.source.x + (static_cast<float>(bounds.left) - translated.x) * scale_x) /
                            image_width +
                        uv_bias_x,
                    (style.source.y + static_cast<float>(bounds.top) - translated.y) * scale_y /
                            image_height +
                        uv_bias_y,
                    (style.source.x + (static_cast<float>(bounds.right) - translated.x) * scale_x) /
                            image_width +
                        uv_bias_x,
                    (style.source.y + static_cast<float>(bounds.bottom) - translated.y) * scale_y /
                            image_height +
                        uv_bias_y,
                },
            .opacity = style.opacity,
        };

        return batch.emit(style.pipe, style.texture_slot, style.sampler_index, instance);
    }

    if (style.pipe == NogaPipe::mask) {
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

        const auto instance = NogaFrameMask{
            .bounds =
                {
                    static_cast<float>(bounds.left),
                    static_cast<float>(bounds.top),
                    static_cast<float>(bounds.right),
                    static_cast<float>(bounds.bottom),
                },
            .start = {static_cast<float>(start_x), static_cast<float>(start_y)},
            .color = {
                static_cast<float>(style.color.red) * kNogaChannelScale,
                static_cast<float>(style.color.green) * kNogaChannelScale,
                static_cast<float>(style.color.blue) * kNogaChannelScale,
                static_cast<float>(style.color.alpha) * kNogaChannelScale,
            },
        };

        return batch.emit(style.pipe, style.texture_slot, 0, instance);
    }

    if (style.pipe == NogaPipe::rounded_stroke) {
        const auto half_thickness = style.thickness * 0.5F;

        const auto instance = NogaFrameRoundedStroke{
            .bounds =
                {
                    static_cast<float>(bounds.left),
                    static_cast<float>(bounds.top),
                    static_cast<float>(bounds.right),
                    static_cast<float>(bounds.bottom),
                },
            .color =
                {
                    static_cast<float>(style.color.red) * kNogaChannelScale,
                    static_cast<float>(style.color.green) * kNogaChannelScale,
                    static_cast<float>(style.color.blue) * kNogaChannelScale,
                    static_cast<float>(style.color.alpha) * kNogaChannelScale,
                },
            .shape =
                {
                    translated.x + translated.width * 0.5F,
                    translated.y + translated.height * 0.5F,
                    translated.width * 0.5F - half_thickness,
                    translated.height * 0.5F - half_thickness,
                },
            .radius = style.radius,
            .thickness = style.thickness,
        };

        return batch.emit(style.pipe, 0, 0, instance);
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

    const auto instance = NogaFrameGlyph{
        .bounds =
            {
                static_cast<float>(bounds.left),
                static_cast<float>(bounds.top),
                static_cast<float>(bounds.right),
                static_cast<float>(bounds.bottom),
            },
        .color =
            {
                static_cast<float>(style.color.red) * kNogaChannelScale,
                static_cast<float>(style.color.green) * kNogaChannelScale,
                static_cast<float>(style.color.blue) * kNogaChannelScale,
                static_cast<float>(style.color.alpha) * kNogaChannelScale,
            },
        .glyph = {
            style.glyph_uv.x + static_cast<float>(start_u),
            style.glyph_uv.y + static_cast<float>(start_v),
            translated.width,
            translated.height,
        },
    };

    return batch.emit(style.pipe, style.texture_slot, 0, instance);
}

NogaRenderResult
NogaEmitter::emit_path(const FillPath& pathed, const LayoutPoint& translation) noexcept {
    auto intersection_capacity = std::size_t{};
    auto winding_capacity = std::size_t{};

    if (!render_detail::valid_path(pathed, intersection_capacity, winding_capacity) ||
        !render_detail::valid_translated_path(pathed, translation)) {
        batch.abandon();

        return NogaRenderResult::invalid_input;
    }

    if (pathed.color.alpha == 0) {
        return NogaRenderResult::complete;
    }

    const auto needs_windings = pathed.fill_rule == SoftwarePathFillRule::non_zero;

    if (intersection_capacity > kPathSpanScratch ||
        (needs_windings && winding_capacity > kPathSpanScratch)) {
        batch.abandon();

        return NogaRenderResult::insufficient_capacity;
    }

    auto intersections = std::array<double, kPathSpanScratch>{};
    auto windings = std::array<std::int8_t, kPathSpanScratch>{};

    auto sink = PathRectSink{
        .batch = batch,
        .clip = batch.current_clip,
        .red = static_cast<float>(pathed.color.red) * kNogaChannelScale,
        .green = static_cast<float>(pathed.color.green) * kNogaChannelScale,
        .blue = static_cast<float>(pathed.color.blue) * kNogaChannelScale,
        .alpha = static_cast<float>(pathed.color.alpha) * kNogaChannelScale,
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
