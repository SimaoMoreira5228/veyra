#include "veyra/text/line_breaker.hpp"

#include "../encoding/utf8.hpp"

#include <cstdint>
#include <limits>

namespace veyra {

void TextLineBreaker::BreakIteratorDeleter::operator()(UBreakIterator* iterator) const noexcept {
    if (iterator != nullptr) {
        ubrk_close(iterator);
    }
}

TextLineBreaker::TextLineBreaker(const char* locale) noexcept {
    UErrorCode status = U_ZERO_ERROR;
    iterator_.reset(ubrk_open(UBRK_LINE, locale, nullptr, 0, &status));
    if (U_FAILURE(status)) {
        iterator_.reset();
    }
}

TextLineBreaker::~TextLineBreaker() = default;

LineBreakResult TextLineBreaker::find_breaks(
    std::string_view utf8,
    std::span<TextLineBreak> output,
    std::size_t& break_count
) noexcept {
    break_count = 0;
    if (iterator_ == nullptr) {
        return LineBreakResult::initialization_failed;
    }
    if (utf8.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) {
        return LineBreakResult::input_too_large;
    }

    const auto length = static_cast<std::int32_t>(utf8.size());
    if (!detail::valid_utf8(utf8)) {
        return LineBreakResult::invalid_utf8;
    }

    const auto* text = utf8.empty() ? "" : utf8.data();
    UText input_text = UTEXT_INITIALIZER;
    UErrorCode status = U_ZERO_ERROR;
    auto* input = utext_openUTF8(&input_text, text, length, &status);
    if (U_FAILURE(status) || input == nullptr) {
        utext_close(&input_text);
        return LineBreakResult::initialization_failed;
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
        return LineBreakResult::initialization_failed;
    }

    ubrk_first(iterator_.get());
    for (auto boundary = ubrk_next(iterator_.get()); boundary != UBRK_DONE;
         boundary = ubrk_next(iterator_.get())) {
        ++break_count;
    }
    if (output.size() < break_count) {
        detach_text();
        return LineBreakResult::insufficient_output;
    }

    ubrk_first(iterator_.get());
    auto output_index = std::size_t{0};
    for (auto boundary = ubrk_next(iterator_.get()); boundary != UBRK_DONE;
         boundary = ubrk_next(iterator_.get())) {
        const auto status_value = ubrk_getRuleStatus(iterator_.get());
        const auto kind = status_value >= UBRK_LINE_HARD && status_value < UBRK_LINE_HARD_LIMIT
            ? TextLineBreakKind::hard
            : TextLineBreakKind::soft;
        output[output_index++] = {
            .byte_offset = static_cast<std::size_t>(boundary),
            .kind = kind,
        };
    }
    detach_text();
    return LineBreakResult::complete;
}

}
