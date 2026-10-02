#include "veyra/text/shaper.hpp"

#include <array>
#include <iostream>
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

}

int main() {
    try {
        check_ltr_and_rtl_runs();
        check_output_capacity_and_input_limit();
        check_invalid_inputs_and_empty_text();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
