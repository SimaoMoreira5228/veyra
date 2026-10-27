#include "veyra/text/shaping/line_breaker.hpp"

#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

void check_breaks_and_utf8_offsets() {
    veyra::TextLineBreaker breaker("en");
    std::array<veyra::TextLineBreak, 8> breaks{};
    std::size_t count = 0;
    const auto result = breaker.find_breaks("one two\nx", breaks, count);

    require(result == veyra::LineBreakResult::complete, "valid UTF-8 line breaks are returned");
    require(count == 3, "breaks include the space, newline, and end of text");
    require(breaks[0].byte_offset == 4, "space break uses its UTF-8 byte offset");
    require(breaks[0].kind == veyra::TextLineBreakKind::soft, "space break is soft");
    require(breaks[1].byte_offset == 8, "newline break uses its UTF-8 byte offset");
    require(breaks[1].kind == veyra::TextLineBreakKind::hard, "newline break is hard");
    require(breaks[2].byte_offset == 9, "final text boundary is returned");

    const auto multibyte_result = breaker.find_breaks("é café", breaks, count);
    require(multibyte_result == veyra::LineBreakResult::complete, "multibyte text is accepted");
    require(count == 2, "multibyte text returns space and final boundaries");
    require(breaks[0].byte_offset == 3, "break positions count UTF-8 bytes");
    require(breaks[1].byte_offset == 8, "final boundary counts UTF-8 bytes");
}

void check_errors_and_reuse() {
    veyra::TextLineBreaker breaker;
    std::array<veyra::TextLineBreak, 1> too_small{{{77, veyra::TextLineBreakKind::hard}}};
    std::size_t count = 0;
    const auto small_result = breaker.find_breaks("one two", too_small, count);
    require(
        small_result == veyra::LineBreakResult::insufficient_output,
        "small output is reported"
    );
    require(count == 2, "small output reports the required count");
    require(too_small[0].byte_offset == 77, "small output remains unchanged");

    constexpr std::array<std::string_view, 4> invalid_inputs{
        "bad\xff",
        "\xc0\xaf",
        "\xed\xa0\x80",
        "\xf4\x90\x80\x80",
    };
    for (const auto input : invalid_inputs) {
        const auto invalid_result = breaker.find_breaks(input, too_small, count);
        require(
            invalid_result == veyra::LineBreakResult::invalid_utf8,
            "invalid UTF-8 is rejected"
        );
        require(count == 0, "errors reset the output count");
    }

    std::array<veyra::TextLineBreak, 1> valid_output{};
    const auto reuse_result = breaker.find_breaks("x", valid_output, count);
    require(reuse_result == veyra::LineBreakResult::complete, "breaker can be reused after errors");
    require(
        count == 1 && valid_output[0].byte_offset == 1,
        "reused breaker returns current text only"
    );

    const auto empty_result = breaker.find_breaks("", valid_output, count);
    require(empty_result == veyra::LineBreakResult::complete, "empty text is accepted");
    require(count == 0, "empty text has no non-initial boundaries");
}

}

int main() {
    try {
        check_breaks_and_utf8_offsets();
        check_errors_and_reuse();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
