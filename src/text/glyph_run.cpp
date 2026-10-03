#include "veyra/text/glyph_run.hpp"

#include <cmath>
#include <cstdint>
#include <limits>

namespace veyra {
namespace {

bool add_position(std::int64_t value, std::int32_t increment, std::int64_t& result) noexcept {
    if ((increment > 0 && value > std::numeric_limits<std::int64_t>::max() - increment) ||
        (increment < 0 && value < std::numeric_limits<std::int64_t>::min() - increment)) {
        return false;
    }

    result = value + increment;
    return true;
}

bool rounded_coordinate(double value, std::int32_t& result) noexcept {
    if (!std::isfinite(value)) {
        return false;
    }

    const auto rounded = std::round(value);
    if (rounded < std::numeric_limits<std::int32_t>::min() ||
        rounded > std::numeric_limits<std::int32_t>::max()) {
        return false;
    }

    result = static_cast<std::int32_t>(rounded);
    return true;
}

}

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
) noexcept {
    command_count = 0;
    if (!cache.valid() || !std::isfinite(baseline.x) || !std::isfinite(baseline.y) ||
        !std::isfinite(x_position_units_per_pixel) || x_position_units_per_pixel <= 0.0 ||
        !std::isfinite(y_position_units_per_pixel) || y_position_units_per_pixel <= 0.0) {
        return GlyphRunResult::invalid_input;
    }
    if (mask_views.size() < glyphs.size() || commands.size() < glyphs.size()) {
        return GlyphRunResult::insufficient_output;
    }

    std::int64_t pen_x = 0;
    std::int64_t pen_y = 0;
    for (std::size_t index = 0; index < glyphs.size(); ++index) {
        const auto& shaped = glyphs[index];
        key.glyph_id = shaped.id;
        const auto cached = cache.find(key);
        if (!cached.has_value()) {
            return GlyphRunResult::glyph_not_cached;
        }

        std::int64_t glyph_x = 0;
        std::int64_t glyph_y = 0;
        if (!add_position(pen_x, shaped.x_offset, glyph_x) ||
            !add_position(pen_y, shaped.y_offset, glyph_y)) {
            return GlyphRunResult::position_overflow;
        }

        const auto x = static_cast<double>(baseline.x) +
            static_cast<double>(glyph_x) / x_position_units_per_pixel + cached->bearing_x;
        const auto y = static_cast<double>(baseline.y) -
            static_cast<double>(glyph_y) / y_position_units_per_pixel - cached->bearing_y;
        std::int32_t draw_x = 0;
        std::int32_t draw_y = 0;
        if (!rounded_coordinate(x, draw_x) || !rounded_coordinate(y, draw_y)) {
            return GlyphRunResult::position_overflow;
        }

        mask_views[index] = cached->mask;
        commands[index] = DrawMask{
            .x = draw_x,
            .y = draw_y,
            .mask = &mask_views[index],
            .color = color,
        };
        command_count = index + 1;

        if (index + 1 == glyphs.size()) {
            continue;
        }

        std::int64_t next_pen_x = 0;
        std::int64_t next_pen_y = 0;
        if (!add_position(pen_x, shaped.x_advance, next_pen_x) ||
            !add_position(pen_y, shaped.y_advance, next_pen_y)) {
            return GlyphRunResult::position_overflow;
        }
        pen_x = next_pen_x;
        pen_y = next_pen_y;
    }

    return GlyphRunResult::complete;
}

}
