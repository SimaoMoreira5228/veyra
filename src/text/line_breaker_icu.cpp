#include "veyra/text/line_breaker.hpp"

#include <cstdint>
#include <limits>

namespace veyra {
namespace {

bool valid_utf8(std::string_view text) noexcept {
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(text.data());
    for (std::size_t offset = 0; offset < text.size();) {
        const auto first = bytes[offset];
        if (first <= 0x7fU) {
            ++offset;
            continue;
        }

        std::size_t sequence_length = 0;
        std::uint32_t code_point = 0;
        if (first >= 0xc2U && first <= 0xdfU) {
            sequence_length = 2;
            code_point = first & 0x1fU;
        } else if (first >= 0xe0U && first <= 0xefU) {
            sequence_length = 3;
            code_point = first & 0x0fU;
        } else if (first >= 0xf0U && first <= 0xf4U) {
            sequence_length = 4;
            code_point = first & 0x07U;
        } else {
            return false;
        }
        if (sequence_length > text.size() - offset) {
            return false;
        }

        for (std::size_t continuation = 1; continuation < sequence_length; ++continuation) {
            const auto byte = bytes[offset + continuation];
            if ((byte & 0xc0U) != 0x80U) {
                return false;
            }
            code_point = (code_point << 6U) | (byte & 0x3fU);
        }
        if ((sequence_length == 2 && code_point < 0x80U) ||
            (sequence_length == 3 && code_point < 0x800U) ||
            (sequence_length == 4 && code_point < 0x10000U) || code_point > 0x10ffffU ||
            (code_point >= 0xd800U && code_point <= 0xdfffU)) {
            return false;
        }
        offset += sequence_length;
    }
    return true;
}

}

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
    if (!valid_utf8(utf8)) {
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
        UText empty_text = UTEXT_INITIALIZER;
        UErrorCode empty_status = U_ZERO_ERROR;
        auto* empty = utext_openUTF8(&empty_text, "", 0, &empty_status);
        if (U_SUCCESS(empty_status) && empty != nullptr) {
            ubrk_setUText(iterator_.get(), empty, &empty_status);
        }
        utext_close(&empty_text);
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
