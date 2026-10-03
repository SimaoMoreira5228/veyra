#ifndef VEYRA_TEXT_FREETYPE_RASTERIZER_HPP
#define VEYRA_TEXT_FREETYPE_RASTERIZER_HPP

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

GlyphRasterizeResult copy_freetype_bitmap(
    const FT_Bitmap& bitmap,
    std::int32_t bearing_x,
    std::int32_t bearing_y,
    GlyphRasterMode mode,
    std::span<std::uint8_t> output,
    std::size_t output_stride,
    GlyphMask& glyph
) noexcept;

GlyphRasterizeResult rasterize_freetype_glyph(
    FT_Face face,
    std::uint32_t glyph_id,
    GlyphRasterMode mode,
    std::span<std::uint8_t> output,
    std::size_t output_stride,
    GlyphMask& glyph
) noexcept;

}

#endif
