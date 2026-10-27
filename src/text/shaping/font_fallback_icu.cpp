#include "veyra/text/shaping/font_fallback.hpp"

#include "../encoding/utf8.hpp"

#include <unicode/uchar.h>
#include <unicode/utext.h>

#include <limits>

namespace veyra {
namespace {

hb_font_t*
first_covering_font(std::string_view cluster, std::span<hb_font_t* const> fonts) noexcept {
    for (auto* font : fonts) {
        auto byte_offset = std::size_t{0};
        auto covers_cluster = true;
        while (byte_offset < cluster.size()) {
            std::uint32_t code_point = 0;
            if (!detail::next_utf8_code_point(cluster, byte_offset, code_point)) {
                return nullptr;
            }
            if (u_hasBinaryProperty(
                    static_cast<UChar32>(code_point),
                    UCHAR_DEFAULT_IGNORABLE_CODE_POINT
                )) {
                continue;
            }

            hb_codepoint_t glyph = 0;
            if (!hb_font_get_nominal_glyph(font, code_point, &glyph) || glyph == 0) {
                covers_cluster = false;
                break;
            }
        }
        if (covers_cluster) {
            return font;
        }
    }
    return nullptr;
}

FontFallbackResult scan_graphemes(
    UBreakIterator* iterator,
    std::string_view utf8,
    std::span<hb_font_t* const> fonts,
    std::span<FontFallbackRun> output,
    bool write_runs,
    std::size_t& run_count
) noexcept {
    run_count = 0;
    auto run_start = std::size_t{0};
    auto grapheme_start = std::size_t{0};
    hb_font_t* run_font = nullptr;

    ubrk_first(iterator);
    for (auto boundary = ubrk_next(iterator); boundary != UBRK_DONE;
         boundary = ubrk_next(iterator)) {
        if (boundary < 0 || static_cast<std::size_t>(boundary) <= grapheme_start) {
            return FontFallbackResult::initialization_failed;
        }
        const auto grapheme_limit = static_cast<std::size_t>(boundary);
        const auto cluster = utf8.substr(grapheme_start, grapheme_limit - grapheme_start);
        auto* font = first_covering_font(cluster, fonts);
        if (font == nullptr) {
            return FontFallbackResult::missing_glyph;
        }

        if (font != run_font) {
            if (run_font != nullptr) {
                if (write_runs) {
                    output[run_count] = {
                        .byte_offset = run_start,
                        .byte_length = grapheme_start - run_start,
                        .font = run_font,
                    };
                }
                ++run_count;
            }
            run_start = grapheme_start;
            run_font = font;
        }
        grapheme_start = grapheme_limit;
    }

    if (run_font != nullptr) {
        if (write_runs) {
            output[run_count] = {
                .byte_offset = run_start,
                .byte_length = grapheme_start - run_start,
                .font = run_font,
            };
        }
        ++run_count;
    }
    return FontFallbackResult::complete;
}

}

void FontFallbackResolver::BreakIteratorDeleter::operator()(
    UBreakIterator* iterator
) const noexcept {
    if (iterator != nullptr) {
        ubrk_close(iterator);
    }
}

FontFallbackResolver::FontFallbackResolver(std::size_t maximum_input_bytes) noexcept :
    maximum_input_bytes_(maximum_input_bytes) {
    UErrorCode status = U_ZERO_ERROR;
    iterator_.reset(ubrk_open(UBRK_CHARACTER, "root", nullptr, 0, &status));
    if (U_FAILURE(status)) {
        iterator_.reset();
    }
}

FontFallbackResolver::~FontFallbackResolver() = default;

FontFallbackResult FontFallbackResolver::resolve_utf8(
    std::string_view utf8,
    std::span<hb_font_t* const> fonts,
    std::span<FontFallbackRun> output,
    std::size_t& run_count
) noexcept {
    run_count = 0;
    if (iterator_ == nullptr) {
        return FontFallbackResult::initialization_failed;
    }
    if (utf8.size() > maximum_input_bytes_ ||
        utf8.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) {
        return FontFallbackResult::input_limit_exceeded;
    }
    if (fonts.empty()) {
        return FontFallbackResult::invalid_fonts;
    }
    for (auto* font : fonts) {
        if (font == nullptr) {
            return FontFallbackResult::invalid_fonts;
        }
    }
    if (!detail::valid_utf8(utf8)) {
        return FontFallbackResult::invalid_utf8;
    }

    const auto* text = utf8.empty() ? "" : utf8.data();
    UText input_text = UTEXT_INITIALIZER;
    UErrorCode status = U_ZERO_ERROR;
    auto* input =
        utext_openUTF8(&input_text, text, static_cast<std::int32_t>(utf8.size()), &status);
    if (U_FAILURE(status) || input == nullptr) {
        utext_close(&input_text);
        return FontFallbackResult::initialization_failed;
    }

    const auto detach_text = [this]() noexcept {
        static constexpr UChar empty_text[] = {0};
        UErrorCode empty_status = U_ZERO_ERROR;
        ubrk_setText(iterator_.get(), empty_text, 0, &empty_status);
    };

    ubrk_setUText(iterator_.get(), input, &status);
    utext_close(&input_text);
    if (U_FAILURE(status)) {
        detach_text();
        return FontFallbackResult::initialization_failed;
    }

    auto result = scan_graphemes(iterator_.get(), utf8, fonts, {}, false, run_count);
    if (result != FontFallbackResult::complete) {
        run_count = 0;
        detach_text();
        return result;
    }
    if (output.size() < run_count) {
        detach_text();
        return FontFallbackResult::insufficient_output;
    }

    std::size_t written_count = 0;
    result = scan_graphemes(iterator_.get(), utf8, fonts, output, true, written_count);
    detach_text();
    if (result != FontFallbackResult::complete || written_count != run_count) {
        run_count = 0;
        return FontFallbackResult::initialization_failed;
    }
    return FontFallbackResult::complete;
}

}
