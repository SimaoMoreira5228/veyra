#ifndef VEYRA_TEXT_GLYPH_RUN_HPP
#define VEYRA_TEXT_GLYPH_RUN_HPP

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

}

#endif
