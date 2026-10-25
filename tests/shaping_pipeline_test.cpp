#include "veyra/text/bidi_resolver.hpp"
#include "veyra/text/font_fallback.hpp"
#include "veyra/text/line_breaker.hpp"
#include "veyra/text/shaper.hpp"

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
    bool cjk{};
    bool emoji{};
};

bool covers_code_point(const Coverage& coverage, std::uint32_t code_point) noexcept {
    if (coverage.latin && code_point < 0x80U) {
        return true;
    }
    if (coverage.hebrew && code_point >= 0x0590U && code_point <= 0x05ffU) {
        return true;
    }
    if (coverage.combining && code_point == 0x0301U) {
        return true;
    }
    if (coverage.cjk && code_point >= 0x4e00U && code_point <= 0x4e0fU) {
        return true;
    }
    if (coverage.emoji && (code_point == 0x1f600U || code_point == 0x200dU)) {
        return true;
    }
    return false;
}

hb_bool_t map_covered_code_point(
    hb_font_t*,
    void*,
    hb_codepoint_t code_point,
    hb_codepoint_t* glyph,
    void* user_data
) {
    if (!covers_code_point(*static_cast<const Coverage*>(user_data), code_point)) {
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
        hb_font_set_scale(font_.get(), 1000, 1000);
    }

    hb_font_t* get() const noexcept {
        return font_.get();
    }

  private:
    Coverage coverage_;
    std::unique_ptr<hb_font_t, decltype(&hb_font_destroy)> font_;
    std::unique_ptr<hb_font_funcs_t, decltype(&hb_font_funcs_destroy)> functions_;
};

struct PipelineRun {
    std::size_t byte_offset{};
    std::size_t byte_length{};
    hb_font_t* font{};
    veyra::TextDirection direction{};
};

std::size_t intersect_runs(
    std::span<const veyra::FontFallbackRun> font_runs,
    std::span<const veyra::TextBidiRun> visual_runs,
    std::span<PipelineRun> output
) noexcept {
    auto count = std::size_t{0};
    for (const auto& seen : visual_runs) {
        const auto seen_end = seen.byte_offset + seen.byte_length;
        for (const auto& run : font_runs) {
            const auto run_end = run.byte_offset + run.byte_length;
            const auto start =
                seen.byte_offset > run.byte_offset ? seen.byte_offset : run.byte_offset;
            const auto end = seen_end < run_end ? seen_end : run_end;
            if (start >= end || count >= output.size()) {
                continue;
            }
            output[count] = PipelineRun{
                .byte_offset = start,
                .byte_length = end - start,
                .font = run.font,
                .direction = seen.direction,
            };
            ++count;
        }
    }
    return count;
}

bool is_code_point_boundary(std::string_view text, std::size_t offset) noexcept {
    if (offset >= text.size()) {
        return offset == text.size();
    }
    return (static_cast<unsigned char>(text[offset]) & 0xc0U) != 0x80U;
}

std::size_t shape_pipeline_run(
    veyra::TextShaper& shaper,
    std::string_view text,
    const PipelineRun& run,
    std::span<veyra::ShapedGlyph> glyphs,
    std::string_view message
) {
    auto glyph_count = std::size_t{0};
    const auto result = shaper.shape_utf8(
        run.font,
        text.substr(run.byte_offset, run.byte_length),
        {.direction = run.direction},
        glyphs,
        glyph_count
    );
    require(result == veyra::ShapeResult::complete, message);
    require(glyph_count > 0, "every pipeline run shapes at least one glyph");
    for (auto index = std::size_t{0}; index < glyph_count; ++index) {
        require(
            glyphs[index].cluster < run.byte_length,
            "shaped clusters stay inside their run byte range"
        );
    }
    return glyph_count;
}

void check_latin_and_cjk_split_runs_by_coverage() {
    constexpr std::string_view text = "Hi \xe4\xb8\x80\xe4\xb8\x81!";
    SyntheticFont latin({.latin = true});
    SyntheticFont cjk({.cjk = true});
    std::array<hb_font_t*, 2> fonts{latin.get(), cjk.get()};

    veyra::FontFallbackResolver fallback(text.size());
    std::array<veyra::FontFallbackRun, 8> font_runs{};
    auto font_run_count = std::size_t{0};
    require(
        fallback.resolve_utf8(text, fonts, font_runs, font_run_count) ==
            veyra::FontFallbackResult::complete,
        "Latin and CJK paragraph resolves fallback fonts"
    );
    require(font_run_count == 3, "fallback splits the paragraph by coverage");
    require(
        font_runs[0].byte_offset == 0 && font_runs[0].byte_length == 3 &&
            font_runs[0].font == latin.get(),
        "Latin lead keeps its byte range and preferred font"
    );
    require(
        font_runs[1].byte_offset == 3 && font_runs[1].byte_length == 6 &&
            font_runs[1].font == cjk.get(),
        "CJK pair keeps its byte range and fallback font"
    );
    require(
        font_runs[2].byte_offset == 9 && font_runs[2].byte_length == 1 &&
            font_runs[2].font == latin.get(),
        "trailing mark returns to the preferred font in logical order"
    );

    veyra::TextBidiResolver bidi(text.size());
    require(
        bidi.set_paragraph(text, veyra::TextBidiParagraphDirection::automatic_left_to_right) ==
            veyra::TextBidiResult::complete,
        "Latin and CJK paragraph sets its bidi paragraph"
    );
    std::array<veyra::TextBidiRun, 8> visual_runs{};
    auto visual_run_count = std::size_t{0};
    require(
        bidi.visual_runs_for_line(0, text.size(), visual_runs, visual_run_count) ==
            veyra::TextBidiResult::complete,
        "single-line paragraph returns its visual runs"
    );
    auto visual_bytes = std::size_t{0};
    for (auto index = std::size_t{0}; index < visual_run_count; ++index) {
        require(
            visual_runs[index].direction == veyra::TextDirection::left_to_right,
            "uniform paragraph keeps one shaping direction"
        );
        visual_bytes += visual_runs[index].byte_length;
    }
    require(visual_bytes == text.size(), "visual runs cover the paragraph exactly");

    std::array<PipelineRun, 16> pipeline{};
    const auto pipeline_count = intersect_runs(
        std::span<const veyra::FontFallbackRun>(font_runs).first(font_run_count),
        std::span<const veyra::TextBidiRun>(visual_runs).first(visual_run_count),
        pipeline
    );
    require(pipeline_count == 3, "intersection keeps the three coverage runs");
    require(
        pipeline[0].byte_offset == 0 && pipeline[1].byte_offset == 3 &&
            pipeline[2].byte_offset == 9,
        "visual order preserves logical order for left-to-right text"
    );

    veyra::TextShaper shaper(text.size());
    std::array<veyra::ShapedGlyph, 64> glyphs{};
    require(
        shape_pipeline_run(shaper, text, pipeline[0], glyphs, "Latin run shapes") == 3,
        "Latin run shapes one glyph per character"
    );
    require(
        glyphs[0].cluster == 0 && glyphs[2].cluster == 2,
        "Latin clusters use run byte offsets"
    );
    require(
        shape_pipeline_run(shaper, text, pipeline[1], glyphs, "CJK run shapes") == 2,
        "CJK run shapes one glyph per character"
    );
    require(glyphs[0].cluster == 0 && glyphs[1].cluster == 3, "CJK clusters use run byte offsets");
    shape_pipeline_run(shaper, text, pipeline[2], glyphs, "trailing run shapes");

    veyra::TextLineBreaker breaker;
    std::array<veyra::TextLineBreak, 8> breaks{};
    auto break_count = std::size_t{0};
    require(
        breaker.find_breaks(text, breaks, break_count) == veyra::LineBreakResult::complete,
        "paragraph breaks into lines"
    );
    require(break_count >= 2, "space and end of text give line opportunities");
    require(breaks[0].byte_offset == 3, "first break follows the space");
    require(breaks[break_count - 1].byte_offset == text.size(), "breaks end at the paragraph end");
    for (auto index = std::size_t{1}; index < break_count; ++index) {
        require(
            breaks[index].byte_offset > breaks[index - 1].byte_offset,
            "break offsets increase through the paragraph"
        );
        require(
            is_code_point_boundary(text, breaks[index].byte_offset),
            "breaks never split a UTF-8 code point"
        );
    }
}

void check_hebrew_digits_reorder_with_directions() {
    constexpr std::string_view text = "abc \xd7\x90\xd7\x91\xd7\x92 123";
    SyntheticFont font({.latin = true, .hebrew = true});
    std::array<hb_font_t*, 1> fonts{font.get()};

    veyra::FontFallbackResolver fallback(text.size());
    std::array<veyra::FontFallbackRun, 8> font_runs{};
    auto font_run_count = std::size_t{0};
    require(
        fallback.resolve_utf8(text, fonts, font_runs, font_run_count) ==
            veyra::FontFallbackResult::complete,
        "mixed-direction paragraph resolves to one covering font"
    );
    require(
        font_run_count == 1 && font_runs[0].byte_offset == 0 &&
            font_runs[0].byte_length == text.size(),
        "one font covers the whole mixed-direction paragraph"
    );

    veyra::TextBidiResolver bidi(text.size());
    require(
        bidi.set_paragraph(text, veyra::TextBidiParagraphDirection::automatic_left_to_right) ==
            veyra::TextBidiResult::complete,
        "mixed-direction paragraph sets its bidi paragraph"
    );
    std::array<veyra::TextBidiRun, 8> visual_runs{};
    auto visual_run_count = std::size_t{0};
    require(
        bidi.visual_runs_for_line(0, text.size(), visual_runs, visual_run_count) ==
            veyra::TextBidiResult::complete,
        "mixed-direction paragraph returns its visual runs"
    );
    require(visual_run_count == 3, "mixed paragraph has three visual runs");
    require(
        visual_runs[0].byte_offset == 0 && visual_runs[0].byte_length == 4 &&
            visual_runs[0].direction == veyra::TextDirection::left_to_right,
        "Latin lead comes first in visual order"
    );
    require(
        visual_runs[1].byte_offset == 11 && visual_runs[1].byte_length == 3 &&
            visual_runs[1].direction == veyra::TextDirection::left_to_right,
        "digits stay left-to-right inside right-to-left text"
    );
    require(
        visual_runs[2].byte_offset == 4 && visual_runs[2].byte_length == 7 &&
            visual_runs[2].direction == veyra::TextDirection::right_to_left,
        "Hebrew run follows the digits in visual order"
    );

    std::array<PipelineRun, 16> pipeline{};
    const auto pipeline_count = intersect_runs(
        std::span<const veyra::FontFallbackRun>(font_runs).first(font_run_count),
        std::span<const veyra::TextBidiRun>(visual_runs).first(visual_run_count),
        pipeline
    );
    require(pipeline_count == 3, "one font run meets three visual runs");
    for (auto index = std::size_t{0}; index < pipeline_count; ++index) {
        require(pipeline[index].font == font.get(), "every run keeps its fallback font");
    }

    veyra::TextShaper shaper(text.size());
    std::array<veyra::ShapedGlyph, 64> glyphs{};
    require(
        shape_pipeline_run(shaper, text, pipeline[0], glyphs, "Latin run shapes") == 4,
        "Latin run shapes one glyph per character"
    );
    require(
        shape_pipeline_run(shaper, text, pipeline[1], glyphs, "digit run shapes") == 3,
        "digit run shapes one glyph per character"
    );
    require(
        glyphs[0].cluster == 0 && glyphs[2].cluster == 2,
        "digit clusters use run byte offsets"
    );
    require(
        shape_pipeline_run(shaper, text, pipeline[2], glyphs, "Hebrew run shapes") == 4,
        "Hebrew run shapes one glyph per character plus its trailing space"
    );
    auto hebrew_mask = 0U;
    for (auto index = std::size_t{0}; index < 4; ++index) {
        require(glyphs[index].cluster < 7, "Hebrew clusters stay inside their run byte range");
        hebrew_mask |= 1U << glyphs[index].cluster;
    }
    require(hebrew_mask == 0b1010101U, "Hebrew clusters cover each character start");
}

void check_graphemes_survive_fallback_and_shaping() {
    constexpr std::string_view text = "e\xcc\x81\xf0\x9f\x98\x80\xe2\x80\x8d\xf0\x9f\x98\x80";
    SyntheticFont latin({.latin = true, .emoji = true});
    SyntheticFont combining({.latin = true, .combining = true});
    std::array<hb_font_t*, 2> fonts{latin.get(), combining.get()};

    veyra::FontFallbackResolver fallback(text.size());
    std::array<veyra::FontFallbackRun, 8> font_runs{};
    auto font_run_count = std::size_t{0};
    require(
        fallback.resolve_utf8(text, fonts, font_runs, font_run_count) ==
            veyra::FontFallbackResult::complete,
        "combining and emoji paragraph resolves fallback fonts"
    );
    require(font_run_count == 2, "two graphemes give two font runs");
    require(
        font_runs[0].byte_offset == 0 && font_runs[0].byte_length == 3 &&
            font_runs[0].font == combining.get(),
        "combining sequence stays one run in its covering font"
    );
    require(
        font_runs[1].byte_offset == 3 && font_runs[1].byte_length == 11 &&
            font_runs[1].font == latin.get(),
        "joined emoji stays one run in its covering font"
    );

    veyra::TextBidiResolver bidi(text.size());
    require(
        bidi.set_paragraph(text, veyra::TextBidiParagraphDirection::automatic_left_to_right) ==
            veyra::TextBidiResult::complete,
        "grapheme paragraph sets its bidi paragraph"
    );
    std::array<veyra::TextBidiRun, 8> visual_runs{};
    auto visual_run_count = std::size_t{0};
    require(
        bidi.visual_runs_for_line(0, text.size(), visual_runs, visual_run_count) ==
            veyra::TextBidiResult::complete,
        "grapheme paragraph returns its visual runs"
    );
    auto visual_bytes = std::size_t{0};
    for (auto index = std::size_t{0}; index < visual_run_count; ++index) {
        require(
            visual_runs[index].direction == veyra::TextDirection::left_to_right,
            "grapheme paragraph keeps one shaping direction"
        );
        visual_bytes += visual_runs[index].byte_length;
    }
    require(visual_bytes == text.size(), "visual runs cover the grapheme paragraph");

    std::array<PipelineRun, 16> pipeline{};
    const auto pipeline_count = intersect_runs(
        std::span<const veyra::FontFallbackRun>(font_runs).first(font_run_count),
        std::span<const veyra::TextBidiRun>(visual_runs).first(visual_run_count),
        pipeline
    );
    require(pipeline_count == 2, "intersection keeps the two grapheme runs");
    require(
        pipeline[0].byte_length == 3 && pipeline[1].byte_offset == 3,
        "no bidi boundary splits a grapheme"
    );

    veyra::TextShaper shaper(text.size());
    std::array<veyra::ShapedGlyph, 64> glyphs{};
    require(
        shape_pipeline_run(shaper, text, pipeline[0], glyphs, "combining run shapes") == 2,
        "combining run shapes base and mark"
    );
    require(
        glyphs[0].cluster == 0 && glyphs[1].cluster == 1,
        "combining clusters stay inside their grapheme"
    );
    require(
        shape_pipeline_run(shaper, text, pipeline[1], glyphs, "emoji run shapes") == 3,
        "joined emoji shapes each code point"
    );
    require(
        glyphs[0].cluster == 0 && glyphs[1].cluster == 4 && glyphs[2].cluster == 7,
        "emoji clusters stay inside their grapheme"
    );

    veyra::TextLineBreaker breaker;
    std::array<veyra::TextLineBreak, 8> breaks{};
    auto break_count = std::size_t{0};
    require(
        breaker.find_breaks(text, breaks, break_count) == veyra::LineBreakResult::complete,
        "grapheme paragraph breaks into lines"
    );
    for (auto index = std::size_t{0}; index < break_count; ++index) {
        require(
            breaks[index].byte_offset == 3 || breaks[index].byte_offset == text.size(),
            "line breaks never split a grapheme"
        );
    }
}

void check_long_paragraph_breaks_into_shaped_lines() {
    const std::string text =
        "one two three four five six cafe\xcc\x81 seven eight nine ten eleven twelve";
    const auto paragraph = std::string_view(text);
    const auto combining_offset = paragraph.find("e\xcc\x81");
    require(combining_offset != std::string_view::npos, "fixture holds a combining sequence");

    SyntheticFont latin({.latin = true});
    SyntheticFont combining({.latin = true, .combining = true});
    std::array<hb_font_t*, 2> fonts{latin.get(), combining.get()};

    veyra::FontFallbackResolver fallback(paragraph.size());
    std::array<veyra::FontFallbackRun, 16> font_runs{};
    auto font_run_count = std::size_t{0};
    require(
        fallback.resolve_utf8(paragraph, fonts, font_runs, font_run_count) ==
            veyra::FontFallbackResult::complete,
        "long paragraph resolves fallback fonts"
    );
    require(font_run_count == 3, "combining grapheme splits the Latin paragraph");
    auto combining_run = std::size_t{99};
    for (auto index = std::size_t{0}; index < font_run_count; ++index) {
        if (font_runs[index].byte_offset <= combining_offset &&
            combining_offset + 3 <= font_runs[index].byte_offset + font_runs[index].byte_length) {
            combining_run = index;
        }
    }
    require(combining_run != 99, "combining sequence sits inside one fallback run");
    require(
        font_runs[combining_run].byte_length == 3 &&
            font_runs[combining_run].font == combining.get(),
        "combining run keeps its grapheme and covering font"
    );

    veyra::TextLineBreaker breaker;
    std::array<veyra::TextLineBreak, 32> breaks{};
    auto break_count = std::size_t{0};
    require(
        breaker.find_breaks(paragraph, breaks, break_count) == veyra::LineBreakResult::complete,
        "long paragraph breaks into lines"
    );
    require(break_count >= 4, "long paragraph offers several line opportunities");
    require(
        breaks[break_count - 1].byte_offset == paragraph.size(),
        "breaks end at the paragraph end"
    );
    for (auto index = std::size_t{0}; index < break_count; ++index) {
        if (index > 0) {
            require(
                breaks[index].byte_offset > breaks[index - 1].byte_offset,
                "break offsets increase through the paragraph"
            );
        }
        require(
            is_code_point_boundary(paragraph, breaks[index].byte_offset),
            "breaks never split a UTF-8 code point"
        );
        if (breaks[index].byte_offset < paragraph.size()) {
            require(
                paragraph[breaks[index].byte_offset - 1] == ' ',
                "soft breaks land after spaces"
            );
            require(breaks[index].kind == veyra::TextLineBreakKind::soft, "space breaks are soft");
        }
    }

    veyra::TextBidiResolver bidi(paragraph.size());
    require(
        bidi.set_paragraph(paragraph, veyra::TextBidiParagraphDirection::automatic_left_to_right) ==
            veyra::TextBidiResult::complete,
        "long paragraph sets its bidi paragraph"
    );
    veyra::TextShaper shaper(paragraph.size());
    std::array<veyra::TextBidiRun, 8> visual_runs{};
    std::array<PipelineRun, 16> pipeline{};
    std::array<veyra::ShapedGlyph, 64> glyphs{};

    auto line_start = std::size_t{0};
    auto line_count = std::size_t{0};
    auto covered = std::size_t{0};
    for (auto index = std::size_t{0}; index < break_count; ++index) {
        const auto line_end = breaks[index].byte_offset;
        if (line_end > line_start) {
            auto visual_run_count = std::size_t{0};
            require(
                bidi.visual_runs_for_line(
                    line_start,
                    line_end - line_start,
                    visual_runs,
                    visual_run_count
                ) == veyra::TextBidiResult::complete,
                "each break-delimited line returns its visual runs"
            );
            const auto pipeline_count = intersect_runs(
                std::span<const veyra::FontFallbackRun>(font_runs).first(font_run_count),
                std::span<const veyra::TextBidiRun>(visual_runs).first(visual_run_count),
                pipeline
            );
            require(pipeline_count > 0, "each line produces at least one shaped run");
            auto line_bytes = std::size_t{0};
            for (auto run = std::size_t{0}; run < pipeline_count; ++run) {
                require(
                    pipeline[run].byte_offset >= line_start &&
                        pipeline[run].byte_offset + pipeline[run].byte_length <= line_end,
                    "line runs stay inside their line"
                );
                require(
                    is_code_point_boundary(paragraph, pipeline[run].byte_offset),
                    "no line run splits a UTF-8 code point"
                );
                shape_pipeline_run(shaper, paragraph, pipeline[run], glyphs, "line run shapes");
                line_bytes += pipeline[run].byte_length;
            }
            require(line_bytes == line_end - line_start, "line runs cover their line exactly");
            for (auto run = std::size_t{0}; run < pipeline_count; ++run) {
                if (pipeline[run].byte_offset <= combining_offset &&
                    combining_offset < pipeline[run].byte_offset + pipeline[run].byte_length) {
                    require(
                        combining_offset + 3 <=
                            pipeline[run].byte_offset + pipeline[run].byte_length,
                        "no line run splits the combining grapheme"
                    );
                }
            }
            covered += line_end - line_start;
            ++line_count;
        }
        line_start = line_end;
    }
    require(line_start == paragraph.size(), "lines partition the paragraph");
    require(covered == paragraph.size(), "every paragraph byte is shaped once");
    require(line_count == break_count, "each break opportunity starts a shaped line");
}

void check_invalid_utf8_stops_before_shaping() {
    constexpr std::string_view text = "bad\xff";
    SyntheticFont latin({.latin = true});
    std::array<hb_font_t*, 1> fonts{latin.get()};

    veyra::FontFallbackResolver fallback(8);
    std::array<veyra::FontFallbackRun, 4> font_runs{};
    auto font_run_count = std::size_t{9};
    require(
        fallback.resolve_utf8(text, fonts, font_runs, font_run_count) ==
            veyra::FontFallbackResult::invalid_utf8,
        "fallback rejects invalid UTF-8 at the boundary"
    );
    require(font_run_count == 0, "failed fallback reports no runs");

    veyra::TextBidiResolver bidi(8);
    require(
        bidi.set_paragraph(text, veyra::TextBidiParagraphDirection::automatic_left_to_right) ==
            veyra::TextBidiResult::invalid_utf8,
        "bidi rejects invalid UTF-8 at the boundary"
    );

    veyra::TextLineBreaker breaker;
    std::array<veyra::TextLineBreak, 4> breaks{};
    auto break_count = std::size_t{9};
    require(
        breaker.find_breaks(text, breaks, break_count) == veyra::LineBreakResult::invalid_utf8,
        "line breaking rejects invalid UTF-8 at the boundary"
    );
    require(break_count == 0, "failed line breaking reports no breaks");

    auto reached_shaper = false;
    if (fallback.resolve_utf8(text, fonts, font_runs, font_run_count) ==
            veyra::FontFallbackResult::complete &&
        bidi.set_paragraph(text, veyra::TextBidiParagraphDirection::automatic_left_to_right) ==
            veyra::TextBidiResult::complete &&
        breaker.find_breaks(text, breaks, break_count) == veyra::LineBreakResult::complete) {
        reached_shaper = true;
    }
    require(!reached_shaper, "invalid UTF-8 stops the pipeline before shaping");
}

}

int main() {
    try {
        check_latin_and_cjk_split_runs_by_coverage();
        check_hebrew_digits_reorder_with_directions();
        check_graphemes_survive_fallback_and_shaping();
        check_long_paragraph_breaks_into_shaped_lines();
        check_invalid_utf8_stops_before_shaping();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
