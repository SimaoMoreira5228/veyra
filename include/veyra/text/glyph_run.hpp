#ifndef VEYRA_TEXT_GLYPH_RUN_HPP
#define VEYRA_TEXT_GLYPH_RUN_HPP

#include "veyra/render/common/glyph_atlas.hpp"
#include "veyra/text/glyph_image_cache.hpp"
#include "veyra/text/glyph_mask_cache.hpp"
#include "veyra/text/shaper.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace veyra {

enum class GlyphRunResult : std::uint8_t {
    complete,
    invalid_input,
    insufficient_output,
    glyph_not_cached,
    position_overflow,
};

enum class GlyphDrawKind : std::uint8_t {
    mask,
    color_image,
};

GlyphRunResult build_glyph_draw_commands(
    std::span<const ShapedGlyph> glyphs,
    GlyphMaskCache& cache,
    GlyphMaskKey key,
    LayoutPoint baseline,
    double x_position_units_per_pixel,
    double y_position_units_per_pixel,
    DisplayColor color,
    std::span<SoftwareMask> mask_views,
    std::span<DisplayCommand> commands,
    std::size_t& command_count
) noexcept;

GlyphRunResult build_glyph_atlas_draws(
    std::span<const ShapedGlyph> glyphs,
    GlyphAtlas& atlas,
    GlyphMaskKey key,
    LayoutPoint baseline,
    double x_position_units_per_pixel,
    double y_position_units_per_pixel,
    DisplayColor color,
    std::span<DisplayCommand> commands,
    std::size_t& command_count
) noexcept;

GlyphRunResult build_color_glyph_draw_commands(
    std::span<const ShapedGlyph> glyphs,
    GlyphImageCache& cache,
    GlyphImageKey key,
    LayoutPoint baseline,
    double x_position_units_per_pixel,
    double y_position_units_per_pixel,
    std::span<SoftwareImage> image_views,
    std::span<DisplayCommand> commands,
    std::size_t& command_count
) noexcept;

GlyphRunResult build_mixed_glyph_draw_commands(
    std::span<const ShapedGlyph> glyphs,
    std::span<const GlyphDrawKind> draw_kinds,
    GlyphMaskCache& mask_cache,
    GlyphMaskKey mask_key,
    GlyphImageCache& image_cache,
    GlyphImageKey image_key,
    LayoutPoint baseline,
    double x_position_units_per_pixel,
    double y_position_units_per_pixel,
    DisplayColor mask_color,
    std::span<SoftwareMask> mask_views,
    std::span<SoftwareImage> image_views,
    std::span<DisplayCommand> commands,
    std::size_t& command_count
) noexcept;

}

#endif
