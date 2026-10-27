#include "veyra/text/shaping/shaper.hpp"

#include <limits>
#include <utility>

namespace veyra {
namespace {

bool valid_direction(TextDirection direction) noexcept {
    switch (direction) {
    case TextDirection::left_to_right:
    case TextDirection::right_to_left:
    case TextDirection::top_to_bottom:
    case TextDirection::bottom_to_top:
        return true;
    }
    return false;
}

hb_direction_t to_harfbuzz_direction(TextDirection direction) noexcept {
    switch (direction) {
    case TextDirection::left_to_right:
        return HB_DIRECTION_LTR;
    case TextDirection::right_to_left:
        return HB_DIRECTION_RTL;
    case TextDirection::top_to_bottom:
        return HB_DIRECTION_TTB;
    case TextDirection::bottom_to_top:
        return HB_DIRECTION_BTT;
    }
    return HB_DIRECTION_INVALID;
}

}

void TextShaper::BufferDeleter::operator()(hb_buffer_t* buffer) const noexcept {
    hb_buffer_destroy(buffer);
}

TextShaper::TextShaper(std::size_t maximum_input_bytes) noexcept :
    buffer_(hb_buffer_create()), maximum_input_bytes_(maximum_input_bytes) {
}

TextShaper::~TextShaper() = default;

TextShaper::TextShaper(TextShaper&&) noexcept = default;

TextShaper& TextShaper::operator=(TextShaper&&) noexcept = default;

ShapeResult TextShaper::shape_utf8(
    hb_font_t* font,
    std::string_view text,
    TextRunProperties properties,
    std::span<ShapedGlyph> output,
    std::size_t& glyph_count
) noexcept {
    glyph_count = 0;
    if (font == nullptr || !valid_direction(properties.direction)) {
        return ShapeResult::invalid_input;
    }
    if (text.size() > maximum_input_bytes_ ||
        text.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
        properties.script.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
        properties.language.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return ShapeResult::input_limit_exceeded;
    }
    if (!buffer_ || !hb_buffer_allocation_successful(buffer_.get())) {
        return ShapeResult::allocation_failure;
    }

    hb_buffer_reset(buffer_.get());
    const auto* data = text.empty() ? "" : text.data();
    const auto length = static_cast<int>(text.size());
    hb_buffer_add_utf8(buffer_.get(), data, length, 0, length);
    hb_buffer_set_direction(buffer_.get(), to_harfbuzz_direction(properties.direction));
    if (!properties.script.empty()) {
        const auto script = hb_script_from_string(
            properties.script.data(),
            static_cast<int>(properties.script.size())
        );
        if (script == HB_SCRIPT_INVALID) {
            return ShapeResult::invalid_input;
        }
        hb_buffer_set_script(buffer_.get(), script);
    }
    if (!properties.language.empty()) {
        const auto language = hb_language_from_string(
            properties.language.data(),
            static_cast<int>(properties.language.size())
        );
        if (language == HB_LANGUAGE_INVALID) {
            return ShapeResult::invalid_input;
        }
        hb_buffer_set_language(buffer_.get(), language);
    }
    hb_buffer_guess_segment_properties(buffer_.get());
    if (!hb_buffer_allocation_successful(buffer_.get())) {
        return ShapeResult::allocation_failure;
    }

    hb_shape(font, buffer_.get(), nullptr, 0);
    if (!hb_buffer_allocation_successful(buffer_.get())) {
        return ShapeResult::allocation_failure;
    }

    unsigned int shaped_count = 0;
    const auto* infos = hb_buffer_get_glyph_infos(buffer_.get(), &shaped_count);
    unsigned int position_count = 0;
    const auto* positions = hb_buffer_get_glyph_positions(buffer_.get(), &position_count);
    glyph_count = shaped_count;
    if (shaped_count != position_count) {
        glyph_count = 0;
        return ShapeResult::allocation_failure;
    }
    if (glyph_count > output.size()) {
        return ShapeResult::insufficient_output;
    }
    if (glyph_count != 0 && (infos == nullptr || positions == nullptr)) {
        glyph_count = 0;
        return ShapeResult::allocation_failure;
    }

    for (std::size_t index = 0; index < glyph_count; ++index) {
        output[index] = {
            .id = infos[index].codepoint,
            .cluster = infos[index].cluster,
            .x_advance = positions[index].x_advance,
            .y_advance = positions[index].y_advance,
            .x_offset = positions[index].x_offset,
            .y_offset = positions[index].y_offset,
        };
    }

    return ShapeResult::complete;
}

}
