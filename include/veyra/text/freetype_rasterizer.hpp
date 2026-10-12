#ifndef VEYRA_TEXT_FREETYPE_RASTERIZER_HPP
#define VEYRA_TEXT_FREETYPE_RASTERIZER_HPP

#include "veyra/text/glyph_bounds_cache.hpp"
#include "veyra/text/glyph_image_cache.hpp"
#include "veyra/text/glyph_mask_cache.hpp"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <cstddef>
#include <cstdint>
#include <span>

namespace veyra {

enum class GlyphRasterizeResult : std::uint8_t {
    complete,
    invalid_input,
    raster_failure,
    unsupported_bitmap,
    unsupported_transform,
    glyph_too_large,
};

using GlyphColorImage = GlyphImage;

GlyphRasterizeResult copy_freetype_bitmap(
    const FT_Bitmap& bitmap,
    std::int32_t bearing_x,
    std::int32_t bearing_y,
    GlyphRasterMode mode,
    std::span<std::uint8_t> output,
    std::size_t output_stride,
    GlyphMask& glyph
) noexcept;

GlyphRasterizeResult copy_freetype_color_bitmap(
    const FT_Bitmap& bitmap,
    std::int32_t bearing_x,
    std::int32_t bearing_y,
    std::span<SoftwarePixel> output,
    std::size_t output_stride,
    GlyphColorImage& glyph
) noexcept;

GlyphRasterizeResult rasterize_freetype_glyph(
    FT_Face face,
    std::uint32_t glyph_id,
    GlyphRasterMode mode,
    std::span<std::uint8_t> output,
    std::size_t output_stride,
    GlyphMask& glyph
) noexcept;

GlyphRasterizeResult measure_freetype_glyph_bounds(
    FT_Face face,
    std::uint32_t glyph_id,
    GlyphRasterMode mode,
    GlyphBounds& bounds
) noexcept;

GlyphRasterizeResult rasterize_freetype_color_glyph(
    FT_Face face,
    std::uint32_t glyph_id,
    std::span<SoftwarePixel> output,
    std::size_t output_stride,
    GlyphColorImage& glyph
) noexcept;

}

#endif
