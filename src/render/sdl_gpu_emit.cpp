#include "sdl_gpu_internal.hpp"

#include "software_renderer_internal.hpp"
#include "veyra/render/glyph_atlas.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace veyra::sdl_gpu_detail {
WindowResult SdlFrameEmitter::resolve_image(const DrawImage& drawn, ShapeStyle& style) noexcept {
    const auto* image = drawn.image;

    if (image == nullptr || !render_detail::valid_image(*image) ||
        !render_detail::valid_image_placement(drawn)) {
        return WindowResult::invalid_input;
    }

    if (drawn.opacity == 0) {
        style.skip = true;

        return WindowResult::ready;
    }

    if (drawn.source.width <= 0.0F || drawn.source.height <= 0.0F) {
        style.skip = true;

        return WindowResult::ready;
    }

    auto slot = kSdlNoTexture;
    auto uploaded = std::size_t{};

    const auto ensured = ensure_sdl_image(state, *image, slot, uploaded);

    if (ensured != WindowResult::ready) {
        return ensured;
    }

    batch.stats.uploads += uploaded > 0 ? 1 : 0;
    batch.stats.upload_bytes += uploaded;

    style.pipe = SdlDrawPipe::image;
    style.texture = slot;
    style.sampling = drawn.sampling == SoftwareImageSampling::nearest ? 0 : 1;
    style.opacity = static_cast<float>(drawn.opacity) * kSdlChannelScale;
    style.shape = drawn.destination;
    style.source = drawn.source;
    style.image = image;

    return WindowResult::ready;
}

WindowResult SdlFrameEmitter::resolve_mask(const DrawMask& masked, ShapeStyle& style) noexcept {
    const auto* mask = masked.mask;

    if (mask == nullptr || mask->stride < mask->width ||
        mask->coverage.size() < mask->height * mask->stride) {
        return WindowResult::invalid_input;
    }

    if (masked.color.alpha == 0) {
        style.skip = true;

        return WindowResult::ready;
    }

    auto slot = kSdlNoTexture;
    auto uploaded = std::size_t{};

    const auto ensured = ensure_sdl_mask(state, *mask, slot, uploaded);

    if (ensured != WindowResult::ready) {
        return ensured;
    }

    batch.stats.uploads += uploaded > 0 ? 1 : 0;
    batch.stats.upload_bytes += uploaded;

    style.pipe = SdlDrawPipe::mask;
    style.texture = slot;
    style.color = masked.color;
    style.mask = mask;
    style.shape = LayoutRect{
        .x = masked.x,
        .y = masked.y,
        .width = static_cast<float>(mask->width),
        .height = static_cast<float>(mask->height),
    };

    return WindowResult::ready;
}

WindowResult SdlFrameEmitter::resolve_glyph(const DrawGlyph& glyphed, ShapeStyle& style) noexcept {
    const auto* atlas = glyphed.atlas;

    if (atlas == nullptr || !std::isfinite(glyphed.x) || !std::isfinite(glyphed.y) ||
        glyphed.width == 0 || glyphed.height == 0 ||
        static_cast<std::uint64_t>(glyphed.u) + glyphed.width > atlas->width() ||
        static_cast<std::uint64_t>(glyphed.v) + glyphed.height > atlas->height()) {
        return WindowResult::invalid_input;
    }

    if (glyphed.color.alpha == 0) {
        style.skip = true;

        return WindowResult::ready;
    }

    const auto& view = atlas->view();

    auto slot = kSdlNoTexture;
    auto uploaded = std::size_t{};

    const auto ensured = ensure_sdl_mask(state, view, slot, uploaded);

    if (ensured != WindowResult::ready) {
        return ensured;
    }

    batch.stats.uploads += uploaded > 0 ? 1 : 0;
    batch.stats.upload_bytes += uploaded;

    style.pipe = SdlDrawPipe::glyph;
    style.texture = slot;
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

    return WindowResult::ready;
}

WindowResult SdlFrameEmitter::resolve_rounded_stroke(
    const StrokeRoundedRect& rounded_stroked,
    ShapeStyle& style
) noexcept {
    style.shape = rounded_stroked.bounds;
    style.color = rounded_stroked.color;

    if (!render_detail::valid_rect(style.shape)) {
        return WindowResult::invalid_input;
    }

    if (!std::isfinite(rounded_stroked.thickness) || rounded_stroked.thickness < 0.0F ||
        !std::isfinite(rounded_stroked.radius) || rounded_stroked.radius < 0.0F) {
        return WindowResult::invalid_input;
    }

    if (style.color.alpha == 0 || rounded_stroked.thickness == 0.0F) {
        style.skip = true;

        return WindowResult::ready;
    }

    const auto minimum_dimension = std::min(style.shape.width, style.shape.height);

    if (minimum_dimension == 0.0F) {
        style.skip = true;

        return WindowResult::ready;
    }

    if (rounded_stroked.thickness >= minimum_dimension) {
        style.radius = std::min(rounded_stroked.radius, minimum_dimension * 0.5F);
        style.pipe = style.radius > 0.0F ? SdlDrawPipe::rounded : SdlDrawPipe::rect;
    } else {
        const auto half_thickness = rounded_stroked.thickness * 0.5F;
        const auto clamped_radius = std::min(rounded_stroked.radius, minimum_dimension * 0.5F);
        style.thickness = rounded_stroked.thickness;
        style.radius = std::max(0.0F, clamped_radius - half_thickness);
        style.pipe = SdlDrawPipe::rounded_stroke;
    }

    return WindowResult::ready;
}

WindowResult SdlFrameEmitter::resolve_fill(
    const FillRect* fill,
    const FillRoundedRect* rounded_fill,
    ShapeStyle& style
) noexcept {
    style.shape = fill != nullptr ? fill->bounds : rounded_fill->bounds;
    style.color = fill != nullptr ? fill->color : rounded_fill->color;

    if (!render_detail::valid_rect(style.shape)) {
        return WindowResult::invalid_input;
    }

    if (style.color.alpha == 0) {
        style.skip = true;

        return WindowResult::ready;
    }

    if (rounded_fill != nullptr) {
        const auto diameter =
            std::min(static_cast<float>(style.shape.width), static_cast<float>(style.shape.height));

        style.radius = std::min(rounded_fill->radius, diameter * 0.5F);

        if (style.radius > 0.0F) {
            style.pipe = SdlDrawPipe::rounded;
        }
    }

    return WindowResult::ready;
}

WindowResult
SdlFrameEmitter::emit_shape(const ShapeStyle& style, const LayoutPoint& translation) noexcept {
    const auto translated = LayoutRect{
        .x = style.shape.x + translation.x,
        .y = style.shape.y + translation.y,
        .width = style.shape.width,
        .height = style.shape.height,
    };

    if (!render_detail::valid_rect(translated)) {
        return WindowResult::invalid_input;
    }

    const auto bounds = render_detail::pixel_bounds(translated, state.width, state.height);

    if (bounds.left >= bounds.right || bounds.top >= bounds.bottom) {
        return WindowResult::ready;
    }

    if (clip_excludes(batch.clip, bounds)) {
        return WindowResult::ready;
    }

    if (style.pipe == SdlDrawPipe::image) {
        const auto source_right = style.source.x + style.source.width;
        const auto source_bottom = style.source.y + style.source.height;

        if (style.source.width <= 0.0F || style.source.height <= 0.0F || source_right <= 0.0F ||
            source_bottom <= 0.0F) {
            return WindowResult::ready;
        }
    }

    if (style.pipe == SdlDrawPipe::rect) {
        const auto instance = RectInstance{
            .min_x = static_cast<float>(bounds.left),
            .min_y = static_cast<float>(bounds.top),
            .max_x = static_cast<float>(bounds.right),
            .max_y = static_cast<float>(bounds.bottom),
            .red = static_cast<float>(style.color.red) * kSdlChannelScale,
            .green = static_cast<float>(style.color.green) * kSdlChannelScale,
            .blue = static_cast<float>(style.color.blue) * kSdlChannelScale,
            .alpha = static_cast<float>(style.color.alpha) * kSdlChannelScale,
        };

        return batch.emit(
            style.pipe,
            kSdlNoTexture,
            0,
            instance,
            state.rect_hosts.data(),
            sizeof(RectInstance)
        );
    }

    if (style.pipe == SdlDrawPipe::rounded) {
        const auto instance = RoundedRectInstance{
            .min_x = static_cast<float>(bounds.left),
            .min_y = static_cast<float>(bounds.top),
            .max_x = static_cast<float>(bounds.right),
            .max_y = static_cast<float>(bounds.bottom),
            .red = static_cast<float>(style.color.red) * kSdlChannelScale,
            .green = static_cast<float>(style.color.green) * kSdlChannelScale,
            .blue = static_cast<float>(style.color.blue) * kSdlChannelScale,
            .alpha = static_cast<float>(style.color.alpha) * kSdlChannelScale,
            .center_x = translated.x + translated.width * 0.5F,
            .center_y = translated.y + translated.height * 0.5F,
            .half_x = translated.width * 0.5F,
            .half_y = translated.height * 0.5F,
            .radius = style.radius,
        };

        return batch.emit(
            style.pipe,
            kSdlNoTexture,
            0,
            instance,
            state.rounded_hosts.data(),
            sizeof(RoundedRectInstance)
        );
    }

    if (style.pipe == SdlDrawPipe::image) {
        const auto& image = *style.image;
        const auto image_width = static_cast<float>(image.width);
        const auto image_height = static_cast<float>(image.height);
        const auto scale_x = style.source.width / translated.width;
        const auto scale_y = style.source.height / translated.height;
        const auto nearest_bias = style.sampling == 0 ? 0.001F : 0.0F;
        const auto uv_bias_x = nearest_bias / image_width;
        const auto uv_bias_y = nearest_bias / image_height;

        const auto instance = ImageInstance{
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

        return batch.emit(
            style.pipe,
            style.texture,
            style.sampling,
            instance,
            state.image_hosts.data(),
            sizeof(ImageInstance)
        );
    }

    if (style.pipe == SdlDrawPipe::mask) {
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

        const auto instance = MaskInstance{
            .min_x = static_cast<float>(bounds.left),
            .min_y = static_cast<float>(bounds.top),
            .max_x = static_cast<float>(bounds.right),
            .max_y = static_cast<float>(bounds.bottom),
            .start_x = static_cast<float>(start_x),
            .start_y = static_cast<float>(start_y),
            .red = static_cast<float>(style.color.red) * kSdlChannelScale,
            .green = static_cast<float>(style.color.green) * kSdlChannelScale,
            .blue = static_cast<float>(style.color.blue) * kSdlChannelScale,
            .alpha = static_cast<float>(style.color.alpha) * kSdlChannelScale,
        };

        return batch.emit(
            style.pipe,
            style.texture,
            0,
            instance,
            state.mask_hosts.data(),
            sizeof(MaskInstance)
        );
    }

    if (style.pipe == SdlDrawPipe::rounded_stroke) {
        const auto half_thickness = style.thickness * 0.5F;

        const auto instance = RoundedStrokeInstance{
            .min_x = static_cast<float>(bounds.left),
            .min_y = static_cast<float>(bounds.top),
            .max_x = static_cast<float>(bounds.right),
            .max_y = static_cast<float>(bounds.bottom),
            .red = static_cast<float>(style.color.red) * kSdlChannelScale,
            .green = static_cast<float>(style.color.green) * kSdlChannelScale,
            .blue = static_cast<float>(style.color.blue) * kSdlChannelScale,
            .alpha = static_cast<float>(style.color.alpha) * kSdlChannelScale,
            .center_x = translated.x + translated.width * 0.5F,
            .center_y = translated.y + translated.height * 0.5F,
            .half_x = translated.width * 0.5F - half_thickness,
            .half_y = translated.height * 0.5F - half_thickness,
            .radius = style.radius,
            .thickness = style.thickness,
        };

        return batch.emit(
            style.pipe,
            kSdlNoTexture,
            0,
            instance,
            state.rounded_stroke_hosts.data(),
            sizeof(RoundedStrokeInstance)
        );
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

    const auto instance = GlyphInstance{
        .min_x = static_cast<float>(bounds.left),
        .min_y = static_cast<float>(bounds.top),
        .max_x = static_cast<float>(bounds.right),
        .max_y = static_cast<float>(bounds.bottom),
        .red = static_cast<float>(style.color.red) * kSdlChannelScale,
        .green = static_cast<float>(style.color.green) * kSdlChannelScale,
        .blue = static_cast<float>(style.color.blue) * kSdlChannelScale,
        .alpha = static_cast<float>(style.color.alpha) * kSdlChannelScale,
        .origin_u = style.glyph_uv.x + static_cast<float>(start_u),
        .origin_v = style.glyph_uv.y + static_cast<float>(start_v),
        .size_w = translated.width,
        .size_h = translated.height,
    };

    return batch.emit(
        style.pipe,
        style.texture,
        0,
        instance,
        state.glyph_hosts.data(),
        sizeof(GlyphInstance)
    );
}

}
