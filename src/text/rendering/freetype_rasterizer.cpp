#include "veyra/text/rendering/freetype_rasterizer.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace veyra {
namespace {

bool valid_mode(GlyphRasterMode mode) noexcept {
    switch (mode) {
    case GlyphRasterMode::grayscale:
    case GlyphRasterMode::monochrome:
        return true;
    }
    return false;
}

}

GlyphRasterizeResult copy_freetype_bitmap(
    const FT_Bitmap& bitmap,
    std::int32_t bearing_x,
    std::int32_t bearing_y,
    GlyphRasterMode mode,
    std::span<std::uint8_t> output,
    std::size_t output_stride,
    GlyphMask& glyph
) noexcept {
    if (!valid_mode(mode)) {
        return GlyphRasterizeResult::invalid_input;
    }

    const auto width = static_cast<std::size_t>(bitmap.width);
    const auto height = static_cast<std::size_t>(bitmap.rows);
    if (height != 0 && output_stride == 0) {
        return GlyphRasterizeResult::invalid_input;
    }
    if (width > output_stride ||
        (height != 0 && output_stride > std::numeric_limits<std::size_t>::max() / height)) {
        return GlyphRasterizeResult::glyph_too_large;
    }

    const auto output_size = height * output_stride;
    if (output_size > output.size()) {
        return GlyphRasterizeResult::glyph_too_large;
    }

    std::size_t source_row_bytes = 0;
    switch (bitmap.pixel_mode) {
    case FT_PIXEL_MODE_GRAY:
        if (mode != GlyphRasterMode::grayscale || bitmap.num_grays < 2 || bitmap.num_grays > 256) {
            return GlyphRasterizeResult::unsupported_bitmap;
        }
        source_row_bytes = width;
        break;
    case FT_PIXEL_MODE_MONO:
        if (mode != GlyphRasterMode::monochrome) {
            return GlyphRasterizeResult::unsupported_bitmap;
        }
        source_row_bytes = width / 8 + (width % 8 == 0 ? 0 : 1);
        break;
    default:
        return GlyphRasterizeResult::unsupported_bitmap;
    }

    const auto pitch = static_cast<std::int64_t>(bitmap.pitch);
    const auto absolute_pitch = static_cast<std::size_t>(pitch < 0 ? -pitch : pitch);
    if (absolute_pitch < source_row_bytes ||
        (height != 0 && absolute_pitch != 0 &&
         height - 1 > static_cast<std::size_t>(std::numeric_limits<std::ptrdiff_t>::max()) /
                 absolute_pitch)) {
        return GlyphRasterizeResult::unsupported_bitmap;
    }
    if (width != 0 && height != 0 && bitmap.buffer == nullptr) {
        return GlyphRasterizeResult::unsupported_bitmap;
    }

    if (width != 0 && height != 0) {
        for (std::size_t row_index = 0; row_index < height; ++row_index) {
            const auto row_offset = static_cast<std::ptrdiff_t>(row_index) * bitmap.pitch;
            const auto* source = bitmap.buffer + row_offset;
            auto* destination = output.data() + row_index * output_stride;

            if (bitmap.pixel_mode == FT_PIXEL_MODE_GRAY) {
                if (bitmap.num_grays == 256) {
                    std::copy_n(source, width, destination);
                    continue;
                }

                const auto gray_range = static_cast<std::uint32_t>(bitmap.num_grays - 1);
                for (std::size_t x = 0; x < width; ++x) {
                    const auto coverage =
                        std::min(static_cast<std::uint32_t>(source[x]), gray_range);
                    destination[x] = static_cast<std::uint8_t>(coverage * 255 / gray_range);
                }
                continue;
            }

            for (std::size_t x = 0; x < width; ++x) {
                const auto bit = static_cast<std::uint8_t>(0x80U >> (x % 8));
                const auto covered = (source[x / 8] & bit) != 0;
                destination[x] = covered ? 255 : 0;
            }
        }
    }

    glyph = {
        .mask =
            {
                .width = width,
                .height = height,
                .stride = output_stride,
                .coverage = output.first(output_size),
            },
        .bearing_x = bearing_x,
        .bearing_y = bearing_y,
    };
    return GlyphRasterizeResult::complete;
}

GlyphRasterizeResult copy_freetype_color_bitmap(
    const FT_Bitmap& bitmap,
    std::int32_t bearing_x,
    std::int32_t bearing_y,
    std::span<SoftwarePixel> output,
    std::size_t output_stride,
    GlyphColorImage& glyph
) noexcept {
    const auto width = static_cast<std::size_t>(bitmap.width);
    const auto height = static_cast<std::size_t>(bitmap.rows);
    if (height != 0 && output_stride == 0) {
        return GlyphRasterizeResult::invalid_input;
    }
    if (bitmap.pixel_mode != FT_PIXEL_MODE_BGRA) {
        return GlyphRasterizeResult::unsupported_bitmap;
    }
    if (width > output_stride || width > std::numeric_limits<std::size_t>::max() / 4 ||
        (height != 0 && output_stride > std::numeric_limits<std::size_t>::max() / height)) {
        return GlyphRasterizeResult::glyph_too_large;
    }

    const auto output_size = height * output_stride;
    const auto source_row_bytes = width * 4;
    if (output_size > output.size()) {
        return GlyphRasterizeResult::glyph_too_large;
    }

    const auto pitch = static_cast<std::int64_t>(bitmap.pitch);
    const auto absolute_pitch = static_cast<std::size_t>(pitch < 0 ? -pitch : pitch);
    if (absolute_pitch < source_row_bytes ||
        (height != 0 && absolute_pitch != 0 &&
         height - 1 > static_cast<std::size_t>(std::numeric_limits<std::ptrdiff_t>::max()) /
                 absolute_pitch)) {
        return GlyphRasterizeResult::unsupported_bitmap;
    }
    if (width != 0 && height != 0 && bitmap.buffer == nullptr) {
        return GlyphRasterizeResult::unsupported_bitmap;
    }

    if (width != 0 && height != 0) {
        for (std::size_t row_index = 0; row_index < height; ++row_index) {
            const auto row_offset = static_cast<std::ptrdiff_t>(row_index) * bitmap.pitch;
            const auto* source = bitmap.buffer + row_offset;
            auto* destination = output.data() + row_index * output_stride;
            for (std::size_t x = 0; x < width; ++x) {
                const auto* pixel = source + x * 4;
                const auto alpha = static_cast<std::uint32_t>(pixel[3]);
                if (alpha == 255U) {
                    destination[x] = {
                        .red = pixel[2],
                        .green = pixel[1],
                        .blue = pixel[0],
                        .alpha = 255,
                    };
                    continue;
                }
                if (alpha == 0U) {
                    destination[x] = {.red = 0, .green = 0, .blue = 0, .alpha = 0};
                    continue;
                }
                const auto unpremultiply = [alpha](std::uint8_t channel) {
                    const auto straight =
                        (static_cast<std::uint32_t>(channel) * 255U + alpha / 2U) / alpha;
                    return static_cast<std::uint8_t>(std::min(straight, 255U));
                };
                destination[x] = {
                    .red = unpremultiply(pixel[2]),
                    .green = unpremultiply(pixel[1]),
                    .blue = unpremultiply(pixel[0]),
                    .alpha = static_cast<std::uint8_t>(alpha),
                };
            }
        }
    }

    glyph = {
        .image =
            {
                .width = width,
                .height = height,
                .stride = output_stride,
                .pixels = output.first(output_size),
            },
        .bearing_x = bearing_x,
        .bearing_y = bearing_y,
    };
    return GlyphRasterizeResult::complete;
}

GlyphRasterizeResult rasterize_freetype_glyph(
    FT_Face face,
    std::uint32_t glyph_id,
    GlyphRasterMode mode,
    std::span<std::uint8_t> output,
    std::size_t output_stride,
    GlyphMask& glyph
) noexcept {
    if (face == nullptr || face->glyph == nullptr || !valid_mode(mode)) {
        return GlyphRasterizeResult::invalid_input;
    }

    FT_Matrix transform{};
    FT_Vector delta{};
    FT_Get_Transform(face, &transform, &delta);
    if (transform.xx != 0x10000L || transform.xy != 0 || transform.yx != 0 ||
        transform.yy != 0x10000L || delta.x != 0 || delta.y != 0) {
        return GlyphRasterizeResult::unsupported_transform;
    }

    auto load_flags = static_cast<FT_Int32>(FT_LOAD_DEFAULT | FT_LOAD_NO_BITMAP | FT_LOAD_NO_SVG);
    if (mode == GlyphRasterMode::monochrome) {
        load_flags |= FT_LOAD_TARGET_MONO;
    }
    if (FT_Load_Glyph(face, static_cast<FT_UInt>(glyph_id), load_flags) != 0) {
        return GlyphRasterizeResult::raster_failure;
    }

    const auto render_mode =
        mode == GlyphRasterMode::grayscale ? FT_RENDER_MODE_NORMAL : FT_RENDER_MODE_MONO;
    if (FT_Render_Glyph(face->glyph, render_mode) != 0) {
        return GlyphRasterizeResult::raster_failure;
    }

    return copy_freetype_bitmap(
        face->glyph->bitmap,
        static_cast<std::int32_t>(face->glyph->bitmap_left),
        static_cast<std::int32_t>(face->glyph->bitmap_top),
        mode,
        output,
        output_stride,
        glyph
    );
}

GlyphRasterizeResult measure_freetype_glyph_bounds(
    FT_Face face,
    std::uint32_t glyph_id,
    GlyphRasterMode mode,
    GlyphBounds& bounds
) noexcept {
    if (face == nullptr || face->glyph == nullptr || !valid_mode(mode)) {
        return GlyphRasterizeResult::invalid_input;
    }

    FT_Matrix transform{};
    FT_Vector delta{};
    FT_Get_Transform(face, &transform, &delta);
    if (transform.xx != 0x10000L || transform.xy != 0 || transform.yx != 0 ||
        transform.yy != 0x10000L || delta.x != 0 || delta.y != 0) {
        return GlyphRasterizeResult::unsupported_transform;
    }

    auto load_flags = static_cast<FT_Int32>(
        FT_LOAD_DEFAULT | FT_LOAD_BITMAP_METRICS_ONLY | FT_LOAD_NO_BITMAP | FT_LOAD_NO_SVG
    );
    if (mode == GlyphRasterMode::monochrome) {
        load_flags |= FT_LOAD_TARGET_MONO;
    }
    if (FT_Load_Glyph(face, static_cast<FT_UInt>(glyph_id), load_flags) != 0) {
        return GlyphRasterizeResult::raster_failure;
    }

    const auto* slot = face->glyph;
    bounds = {
        .width = static_cast<std::int32_t>(slot->bitmap.width),
        .height = static_cast<std::int32_t>(slot->bitmap.rows),
        .bearing_x = static_cast<std::int32_t>(slot->bitmap_left),
        .bearing_y = static_cast<std::int32_t>(slot->bitmap_top),
    };
    return GlyphRasterizeResult::complete;
}

GlyphRasterizeResult rasterize_freetype_color_glyph(
    FT_Face face,
    std::uint32_t glyph_id,
    std::span<SoftwarePixel> output,
    std::size_t output_stride,
    GlyphColorImage& glyph
) noexcept {
    if (face == nullptr || face->glyph == nullptr) {
        return GlyphRasterizeResult::invalid_input;
    }

    FT_Matrix transform{};
    FT_Vector delta{};
    FT_Get_Transform(face, &transform, &delta);
    if (transform.xx != 0x10000L || transform.xy != 0 || transform.yx != 0 ||
        transform.yy != 0x10000L || delta.x != 0 || delta.y != 0) {
        return GlyphRasterizeResult::unsupported_transform;
    }

    const auto load_flags = static_cast<FT_Int32>(FT_LOAD_DEFAULT | FT_LOAD_COLOR | FT_LOAD_NO_SVG);
    if (FT_Load_Glyph(face, static_cast<FT_UInt>(glyph_id), load_flags) != 0) {
        return GlyphRasterizeResult::raster_failure;
    }
    if (face->glyph->format != FT_GLYPH_FORMAT_BITMAP &&
        FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL) != 0) {
        return GlyphRasterizeResult::raster_failure;
    }

    return copy_freetype_color_bitmap(
        face->glyph->bitmap,
        static_cast<std::int32_t>(face->glyph->bitmap_left),
        static_cast<std::int32_t>(face->glyph->bitmap_top),
        output,
        output_stride,
        glyph
    );
}

}
