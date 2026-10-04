#include "veyra/text/font_fallback.hpp"

#include <array>
#include <cstdint>
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

struct Coverage {
    bool latin{};
    bool hebrew{};
    bool combining{};
    bool emoji{};
};

hb_bool_t map_covered_code_point(
    hb_font_t*,
    void*,
    hb_codepoint_t code_point,
    hb_codepoint_t* glyph,
    void* user_data
) {
    const auto& coverage = *static_cast<const Coverage*>(user_data);
    const auto supported = (coverage.latin && code_point < 0x80U) ||
        (coverage.hebrew && code_point >= 0x0590U && code_point <= 0x05ffU) ||
        (coverage.combining && code_point == 0x0301U) || (coverage.emoji && code_point == 0x1f600U);
    if (!supported) {
        return false;
    }
    *glyph = code_point;
    return true;
}

class SyntheticFont {
  public:
    explicit SyntheticFont(Coverage coverage) :
        coverage_(coverage), font_(hb_font_create(hb_face_get_empty()), hb_font_destroy),
        functions_(hb_font_funcs_create(), hb_font_funcs_destroy) {
        hb_font_funcs_set_nominal_glyph_func(
            functions_.get(),
            map_covered_code_point,
            &coverage_,
            nullptr
        );
        hb_font_funcs_make_immutable(functions_.get());
        hb_font_set_funcs(font_.get(), functions_.get(), nullptr, nullptr);
    }

    hb_font_t* get() const noexcept {
        return font_.get();
    }

  private:
    Coverage coverage_;
    std::unique_ptr<hb_font_t, decltype(&hb_font_destroy)> font_;
    std::unique_ptr<hb_font_funcs_t, decltype(&hb_font_funcs_destroy)> functions_;
};

void check_runs_respect_font_order_and_graphemes() {
    SyntheticFont latin({.latin = true, .emoji = true});
    SyntheticFont hebrew({.latin = true, .hebrew = true, .combining = true});
    std::array<hb_font_t*, 2> fonts{latin.get(), hebrew.get()};
    std::array<veyra::FontFallbackRun, 8> runs{};
    veyra::FontFallbackResolver resolver(64);
    std::size_t run_count = 0;

    const auto mixed = resolver.resolve_utf8(
        "A\xd7\x90"
        "B",
        fonts,
        runs,
        run_count
    );
    require(mixed == veyra::FontFallbackResult::complete, "mixed scripts resolve");
    require(run_count == 3, "adjacent graphemes coalesce by selected font");
    require(
        runs[0].byte_offset == 0 && runs[0].byte_length == 1 && runs[0].font == latin.get(),
        "first run selects the preferred Latin font"
    );
    require(
        runs[1].byte_offset == 1 && runs[1].byte_length == 2 && runs[1].font == hebrew.get(),
        "Hebrew run preserves its UTF-8 byte range"
    );
    require(
        runs[2].byte_offset == 3 && runs[2].byte_length == 1 && runs[2].font == latin.get(),
        "last run returns to the preferred Latin font"
    );

    const auto combining = resolver.resolve_utf8("e\xcc\x81", fonts, runs, run_count);
    require(combining == veyra::FontFallbackResult::complete, "combining grapheme resolves");
    require(run_count == 1, "a grapheme remains within one font run");
    require(runs[0].font == hebrew.get(), "fallback font covers every significant code point");
    require(runs[0].byte_length == 3, "combining grapheme keeps its full byte range");

    const auto variation_selector =
        resolver.resolve_utf8("\xf0\x9f\x98\x80\xef\xb8\x8f", fonts, runs, run_count);
    require(
        variation_selector == veyra::FontFallbackResult::complete,
        "variation selector is ignored"
    );
    require(run_count == 1 && runs[0].font == latin.get(), "emoji uses its covering font");
}

void check_errors_preserve_output_and_allow_reuse() {
    SyntheticFont latin({.latin = true});
    SyntheticFont hebrew({.hebrew = true});
    std::array<hb_font_t*, 2> fonts{latin.get(), hebrew.get()};
    const veyra::FontFallbackRun sentinel{.byte_offset = 99, .byte_length = 99, .font = nullptr};
    std::array<veyra::FontFallbackRun, 1> short_output{sentinel};
    veyra::FontFallbackResolver resolver(4);
    std::size_t run_count = 7;

    auto result = resolver.resolve_utf8(
        "A\xd7\x90"
        "B",
        fonts,
        short_output,
        run_count
    );
    require(result == veyra::FontFallbackResult::insufficient_output, "short output is reported");
    require(run_count == 3, "short output reports required run capacity");
    require(short_output[0].byte_offset == sentinel.byte_offset, "short output stays unchanged");

    constexpr std::array<std::string_view, 4> invalid_inputs{
        "bad\xff",
        "\xc0\xaf",
        "\xed\xa0\x80",
        "\xf4\x90\x80\x80",
    };
    std::array<veyra::FontFallbackRun, 4> output{};
    for (const auto input : invalid_inputs) {
        result = resolver.resolve_utf8(input, fonts, output, run_count);
        require(result == veyra::FontFallbackResult::invalid_utf8, "invalid UTF-8 is rejected");
        require(run_count == 0, "invalid text resets the run count");
    }

    result = resolver.resolve_utf8("\xd7\x90", fonts, output, run_count);
    require(result == veyra::FontFallbackResult::complete, "resolver works after invalid input");
    require(
        run_count == 1 && output[0].font == hebrew.get(),
        "reused resolver returns current run"
    );

    result = resolver.resolve_utf8("A", {}, output, run_count);
    require(result == veyra::FontFallbackResult::invalid_fonts, "empty font list is rejected");
    std::array<hb_font_t*, 1> null_font{nullptr};
    result = resolver.resolve_utf8("A", null_font, output, run_count);
    require(result == veyra::FontFallbackResult::invalid_fonts, "null fonts are rejected");

    result = resolver.resolve_utf8("\xf0\x9f\x98\x80", fonts, output, run_count);
    require(result == veyra::FontFallbackResult::missing_glyph, "uncovered graphemes are reported");
    require(run_count == 0, "missing glyph resets the run count");

    result = resolver.resolve_utf8("longer", fonts, output, run_count);
    require(result == veyra::FontFallbackResult::input_limit_exceeded, "input limit is enforced");
    require(run_count == 0, "input limit errors reset the run count");

    result = resolver.resolve_utf8("", fonts, output, run_count);
    require(result == veyra::FontFallbackResult::complete, "empty text resolves");
    require(run_count == 0, "empty text has no font runs");
}

}

int main() {
    try {
        check_runs_respect_font_order_and_graphemes();
        check_errors_preserve_output_and_allow_reuse();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
