#include "veyra/text/shaping/bidi_resolver.hpp"

#include <array>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

void check_visual_runs() {
    veyra::TextBidiResolver resolver(64);
    std::array<veyra::TextBidiRun, 8> runs{};
    std::size_t count = 0;
    require(
        resolver.set_paragraph(
            "abc \xd7\x90\xd7\x91\xd7\x92 123",
            veyra::TextBidiParagraphDirection::automatic_left_to_right
        ) == veyra::TextBidiResult::complete,
        "mixed-direction paragraph is resolved"
    );
    require(
        resolver.visual_runs_for_line(0, 14, runs, count) == veyra::TextBidiResult::complete,
        "paragraph visual runs are returned"
    );
    require(count == 3, "mixed paragraph has three visual runs");
    require(
        runs[0].byte_offset == 0 && runs[0].byte_length == 4 &&
            runs[0].direction == veyra::TextDirection::left_to_right,
        "Latin run comes first in visual order"
    );
    require(
        runs[1].byte_offset == 11 && runs[1].byte_length == 3 &&
            runs[1].direction == veyra::TextDirection::left_to_right,
        "number run retains its logical byte range"
    );
    require(
        runs[2].byte_offset == 4 && runs[2].byte_length == 7 &&
            runs[2].direction == veyra::TextDirection::right_to_left,
        "Hebrew run follows the number run visually"
    );
}

void check_lines_and_utf8_boundaries() {
    veyra::TextBidiResolver resolver(32);
    std::array<veyra::TextBidiRun, 4> runs{};
    std::size_t count = 0;
    require(
        resolver.set_paragraph(
            "A\xf0\x9f\x98\x80\xd7\x90\xd7\x91",
            veyra::TextBidiParagraphDirection::left_to_right
        ) == veyra::TextBidiResult::complete,
        "supplementary code point is converted for ICU"
    );
    require(
        resolver.visual_runs_for_line(5, 4, runs, count) == veyra::TextBidiResult::complete,
        "line range maps from UTF-8 bytes to ICU indices"
    );
    require(
        count == 1 && runs[0].byte_offset == 5 && runs[0].byte_length == 4,
        "Hebrew line run maps back to exact UTF-8 bytes"
    );
    require(
        runs[0].direction == veyra::TextDirection::right_to_left,
        "Hebrew line is marked right-to-left"
    );
    require(
        resolver.visual_runs_for_line(2, 4, runs, count) ==
            veyra::TextBidiResult::invalid_line_range,
        "line boundaries cannot split a UTF-8 code point"
    );
    require(
        resolver.set_paragraph("next", veyra::TextBidiParagraphDirection::left_to_right) ==
            veyra::TextBidiResult::complete,
        "resolver can replace a paragraph after returning line runs"
    );
    require(
        resolver.visual_runs_for_line(0, 4, runs, count) == veyra::TextBidiResult::complete,
        "replacement paragraph returns its own visual runs"
    );
    require(
        count == 1 && runs[0].byte_offset == 0 && runs[0].byte_length == 4,
        "replacement paragraph does not retain prior line offsets"
    );

    require(
        resolver.set_paragraph(
            "\xd7\x90\xd7\x91\xd7\x92  x",
            veyra::TextBidiParagraphDirection::left_to_right
        ) == veyra::TextBidiResult::complete,
        "paragraph with trailing line spaces is resolved"
    );
    require(
        resolver.visual_runs_for_line(0, 8, runs, count) == veyra::TextBidiResult::complete,
        "line boundary reordering is applied to the selected line"
    );
    require(
        count == 2 && runs[0].byte_offset == 0 && runs[0].byte_length == 6 &&
            runs[0].direction == veyra::TextDirection::right_to_left && runs[1].byte_offset == 6 &&
            runs[1].byte_length == 2 && runs[1].direction == veyra::TextDirection::left_to_right,
        "trailing spaces use the paragraph direction for this line"
    );
}

void check_limits_and_reuse() {
    veyra::TextBidiResolver resolver(16);
    std::array<veyra::TextBidiRun, 1> output{{
        {99, 99, veyra::TextDirection::right_to_left},
    }};
    std::size_t count = 0;
    constexpr std::array<std::string_view, 5> invalid_inputs{
        "a\xff",
        "\xc0\xaf",
        "\xed\xa0\x80",
        "\xf4\x90\x80\x80",
        "\xe2\x82",
    };
    for (const auto input : invalid_inputs) {
        require(
            resolver.set_paragraph(input, veyra::TextBidiParagraphDirection::left_to_right) ==
                veyra::TextBidiResult::invalid_utf8,
            "malformed UTF-8 is rejected"
        );
    }
    require(
        resolver.set_paragraph("first\nsecond", veyra::TextBidiParagraphDirection::left_to_right) ==
            veyra::TextBidiResult::multiple_paragraphs,
        "paragraph separators are left to the caller"
    );
    require(
        resolver.set_paragraph("a\xd7\x90", veyra::TextBidiParagraphDirection::left_to_right) ==
            veyra::TextBidiResult::complete,
        "resolver accepts a valid paragraph after errors"
    );
    require(
        resolver.visual_runs_for_line(0, 3, output, count) ==
            veyra::TextBidiResult::insufficient_output,
        "insufficient visual-run storage is reported"
    );
    require(count == 2, "required run capacity is reported");
    require(output[0].byte_offset == 99, "insufficient output remains unchanged");
    require(
        resolver.set_paragraph(
            "too long for this buffer",
            veyra::TextBidiParagraphDirection::left_to_right
        ) == veyra::TextBidiResult::input_limit_exceeded,
        "maximum paragraph length is enforced"
    );
}

void check_empty_paragraph() {
    veyra::TextBidiResolver resolver(0);
    std::size_t count = 1;
    require(
        resolver.set_paragraph("", veyra::TextBidiParagraphDirection::automatic_left_to_right) ==
            veyra::TextBidiResult::complete,
        "empty paragraph is accepted with zero capacity"
    );
    require(
        resolver.visual_runs_for_line(0, 0, std::span<veyra::TextBidiRun>{}, count) ==
            veyra::TextBidiResult::complete,
        "empty paragraph has no visual runs"
    );
    require(count == 0, "empty paragraph resets the run count");

    veyra::TextBidiResolver invalid_limit(std::numeric_limits<std::size_t>::max());
    require(
        invalid_limit.set_paragraph("", veyra::TextBidiParagraphDirection::left_to_right) ==
            veyra::TextBidiResult::input_limit_exceeded,
        "UTF-16 index limits are checked before allocating scratch storage"
    );
}

}

int main() {
    try {
        check_visual_runs();
        check_lines_and_utf8_boundaries();
        check_limits_and_reuse();
        check_empty_paragraph();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
