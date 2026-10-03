#include "veyra/text/glyph_run.hpp"
#include "veyra/text/shaper.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

hb_bool_t map_codepoint(hb_font_t*, void*, hb_codepoint_t codepoint, hb_codepoint_t* glyph, void*) {
    *glyph = codepoint;
    return true;
}

class SyntheticFont {
  public:
    SyntheticFont() :
        font_(hb_font_create(hb_face_get_empty()), hb_font_destroy),
        functions_(hb_font_funcs_create(), hb_font_funcs_destroy) {
        hb_font_funcs_set_nominal_glyph_func(functions_.get(), map_codepoint, nullptr, nullptr);
        hb_font_funcs_make_immutable(functions_.get());
        hb_font_set_funcs(font_.get(), functions_.get(), nullptr, nullptr);
        hb_font_set_scale(font_.get(), 1000, 1000);
    }

    hb_font_t* get() const noexcept {
        return font_.get();
    }

  private:
    std::unique_ptr<hb_font_t, decltype(&hb_font_destroy)> font_;
    std::unique_ptr<hb_font_funcs_t, decltype(&hb_font_funcs_destroy)> functions_;
};

void check_ltr_and_rtl_runs() {
    SyntheticFont font;
    veyra::TextShaper shaper(64);
    std::array<veyra::ShapedGlyph, 2> glyphs{
        veyra::ShapedGlyph{.id = 99, .cluster = 99, .x_advance = 99},
        veyra::ShapedGlyph{.id = 99, .cluster = 99, .x_advance = 99},
    };
    std::size_t glyph_count = 0;

    const auto ltr_result = shaper.shape_utf8(
        font.get(),
        "ab",
        {
            .direction = veyra::TextDirection::left_to_right,
            .script = "Latn",
            .language = "en",
        },
        glyphs,
        glyph_count
    );
    require(ltr_result == veyra::ShapeResult::complete, "left-to-right run shapes");
    require(glyph_count == 2, "one glyph is emitted for each mapped character");
    require(glyphs[0].id == 'a' && glyphs[1].id == 'b', "left-to-right glyph order is preserved");
    require(glyphs[0].cluster == 0 && glyphs[1].cluster == 1, "clusters use UTF-8 byte offsets");
    require(glyphs[0].x_advance != 99, "shaped positions replace existing output values");

    const auto rtl_result = shaper.shape_utf8(
        font.get(),
        "\xd8\xa7\xd8\xa8",
        {
            .direction = veyra::TextDirection::right_to_left,
            .script = "Arab",
            .language = "ar",
        },
        glyphs,
        glyph_count
    );
    require(rtl_result == veyra::ShapeResult::complete, "right-to-left run shapes");
    require(glyph_count == 2, "right-to-left run emits both glyphs");
    const auto clusters_are_input_offsets = (glyphs[0].cluster == 0 && glyphs[1].cluster == 2) ||
        (glyphs[0].cluster == 2 && glyphs[1].cluster == 0);
    require(clusters_are_input_offsets, "Arabic clusters refer to UTF-8 byte offsets");
}

void check_output_capacity_and_input_limit() {
    SyntheticFont font;
    veyra::TextShaper shaper(1);
    const auto unchanged = veyra::ShapedGlyph{.id = 99};
    std::array<veyra::ShapedGlyph, 1> glyphs{unchanged};
    std::size_t glyph_count = 7;

    const auto too_long = shaper.shape_utf8(
        font.get(),
        "ab",
        {.direction = veyra::TextDirection::left_to_right},
        glyphs,
        glyph_count
    );
    require(too_long == veyra::ShapeResult::input_limit_exceeded, "input limit is enforced");
    require(glyph_count == 0, "rejected input reports no output glyphs");
    require(glyphs[0].id == unchanged.id, "rejected input leaves output untouched");

    veyra::TextShaper bounded_shaper(64);
    const auto insufficient = bounded_shaper.shape_utf8(
        font.get(),
        "ab",
        {.direction = veyra::TextDirection::left_to_right},
        glyphs,
        glyph_count
    );
    require(
        insufficient == veyra::ShapeResult::insufficient_output,
        "short output storage is reported"
    );
    require(glyph_count == 2, "short output reports the required glyph count");
    require(glyphs[0].id == unchanged.id, "short output leaves its buffer untouched");
}

void check_invalid_inputs_and_empty_text() {
    SyntheticFont font;
    veyra::TextShaper shaper(0);
    std::array<veyra::ShapedGlyph, 1> glyphs{};
    std::size_t glyph_count = 4;

    const auto invalid_font = shaper.shape_utf8(
        nullptr,
        "",
        {.direction = veyra::TextDirection::left_to_right},
        glyphs,
        glyph_count
    );
    require(invalid_font == veyra::ShapeResult::invalid_input, "null font is rejected");

    const auto empty = shaper.shape_utf8(
        font.get(),
        "",
        {.direction = veyra::TextDirection::left_to_right},
        glyphs,
        glyph_count
    );
    require(empty == veyra::ShapeResult::complete, "empty text shapes successfully");
    require(glyph_count == 0, "empty text produces no glyphs");
}

void check_glyph_draw_list() {
    std::array<veyra::GlyphMaskCacheSlot, 4> slots{};
    std::array<std::uint8_t, 16> cache_storage{};
    veyra::GlyphMaskCache cache(slots, cache_storage, 2, 2);
    const std::array<std::uint8_t, 1> coverage{255};
    const veyra::GlyphMask mask{
        .mask =
            {
                .width = 1,
                .height = 1,
                .stride = 1,
                .coverage = coverage,
            },
        .bearing_x = -1,
        .bearing_y = 3,
    };
    auto key = veyra::GlyphMaskKey{
        .face_id = 1,
        .glyph_id = 7,
        .pixel_size = 12,
    };
    require(
        cache.insert(key, mask) == veyra::GlyphMaskCacheResult::inserted,
        "first glyph mask enters the cache"
    );
    key.glyph_id = 8;
    const auto second_mask = veyra::GlyphMask{
        .mask =
            {
                .width = 1,
                .height = 1,
                .stride = 1,
                .coverage = coverage,
            },
        .bearing_x = 2,
        .bearing_y = 0,
    };
    require(
        cache.insert(key, second_mask) == veyra::GlyphMaskCacheResult::inserted,
        "second glyph mask enters the cache"
    );

    const std::array glyphs{
        veyra::ShapedGlyph{
            .id = 7,
            .x_advance = 96,
            .y_advance = 64,
            .x_offset = 32,
            .y_offset = 64,
        },
        veyra::ShapedGlyph{
            .id = 8,
            .x_offset = -32,
        },
    };
    std::array<veyra::SoftwareMask, 2> masks{};
    std::array<veyra::DisplayCommand, 2> commands{};
    std::size_t command_count = 0;
    key.glyph_id = 0;

    const auto result = veyra::build_glyph_draw_commands(
        glyphs,
        cache,
        key,
        {.x = 10.0F, .y = 20.0F},
        64.0,
        32.0,
        {.red = 20, .green = 30, .blue = 40},
        masks,
        commands,
        command_count
    );
    require(result == veyra::GlyphRunResult::complete, "cached glyphs build a draw list");
    require(command_count == 2, "one mask draw is emitted for each shaped glyph");
    const auto* first = std::get_if<veyra::DrawMask>(&commands[0]);
    const auto* second = std::get_if<veyra::DrawMask>(&commands[1]);
    require(
        first != nullptr && first->x == 10 && first->y == 15,
        "first glyph uses offset and bearing"
    );
    require(
        second != nullptr && second->x == 13 && second->y == 18,
        "next glyph follows shaped advances"
    );
    require(
        first->mask == &masks[0] && second->mask == &masks[1],
        "draws reference caller-owned mask views"
    );

    constexpr auto surface_width = std::size_t{16};
    constexpr auto surface_height = std::size_t{24};
    std::array<veyra::SoftwarePixel, surface_width * surface_height> pixels{};
    const auto surface = veyra::SoftwareSurface{
        .width = surface_width,
        .height = surface_height,
        .stride = surface_width,
        .pixels = pixels,
    };
    require(
        veyra::render_software(
            std::span<const veyra::DisplayCommand>(commands).first(command_count),
            surface,
            {}
        ) == veyra::SoftwareRenderResult::complete,
        "built glyph draws render through the software renderer"
    );
    require(pixels[15 * surface_width + 10].red == 20, "first glyph coverage reaches the surface");
    require(
        pixels[18 * surface_width + 13].red == 20,
        "following glyph coverage reaches the surface"
    );

    command_count = 9;
    const auto short_output = veyra::build_glyph_draw_commands(
        glyphs,
        cache,
        {.face_id = 1, .pixel_size = 12},
        {.x = 10.0F, .y = 20.0F},
        64.0,
        64.0,
        {},
        masks,
        std::span<veyra::DisplayCommand>(commands).first(1),
        command_count
    );
    require(
        short_output == veyra::GlyphRunResult::insufficient_output,
        "short command storage is rejected"
    );
    require(command_count == 0, "short command storage writes no commands");

    const auto invalid_scale = veyra::build_glyph_draw_commands(
        glyphs,
        cache,
        {.face_id = 1, .pixel_size = 12},
        {.x = 10.0F, .y = 20.0F},
        0.0,
        64.0,
        {},
        masks,
        commands,
        command_count
    );
    require(
        invalid_scale == veyra::GlyphRunResult::invalid_input,
        "zero position scale is rejected"
    );
    require(command_count == 0, "invalid input writes no commands");

    const auto overflow = veyra::build_glyph_draw_commands(
        std::span<const veyra::ShapedGlyph>(glyphs).first(1),
        cache,
        {.face_id = 1, .pixel_size = 12},
        {.x = std::numeric_limits<float>::max(), .y = 20.0F},
        64.0,
        64.0,
        {},
        masks,
        commands,
        command_count
    );
    require(
        overflow == veyra::GlyphRunResult::position_overflow,
        "unrepresentable position is rejected"
    );
    require(command_count == 0, "position overflow writes no command");

    command_count = 9;
    const std::array missing_glyph{
        veyra::ShapedGlyph{.id = 99},
    };
    const auto missing = veyra::build_glyph_draw_commands(
        missing_glyph,
        cache,
        {.face_id = 1, .glyph_id = 99, .pixel_size = 12},
        {.x = 0.0F, .y = 0.0F},
        64.0,
        64.0,
        {},
        masks,
        commands,
        command_count
    );
    require(missing == veyra::GlyphRunResult::glyph_not_cached, "uncached glyph is reported");
    require(command_count == 0, "a cache miss reports an empty command prefix");
}

}

int main() {
    try {
        check_ltr_and_rtl_runs();
        check_output_capacity_and_input_limit();
        check_invalid_inputs_and_empty_text();
        check_glyph_draw_list();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
