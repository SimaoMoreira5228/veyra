#include "veyra/text/bidi_resolver.hpp"

#include "../encoding/utf8.hpp"

#include <algorithm>
#include <limits>
#include <new>
#include <stdexcept>

namespace veyra {
namespace {

bool to_icu_paragraph_level(TextBidiParagraphDirection direction, UBiDiLevel& level) noexcept {
    switch (direction) {
    case TextBidiParagraphDirection::left_to_right:
        level = UBIDI_LTR;
        return true;
    case TextBidiParagraphDirection::right_to_left:
        level = UBIDI_RTL;
        return true;
    case TextBidiParagraphDirection::automatic_left_to_right:
        level = UBIDI_DEFAULT_LTR;
        return true;
    case TextBidiParagraphDirection::automatic_right_to_left:
        level = UBIDI_DEFAULT_RTL;
        return true;
    }
    return false;
}

}

void TextBidiResolver::BiDiDeleter::operator()(UBiDi* bidi) const noexcept {
    if (bidi != nullptr) {
        ubidi_close(bidi);
    }
}

TextBidiResolver::TextBidiResolver(std::size_t maximum_input_bytes) noexcept :
    maximum_input_bytes_(maximum_input_bytes) {
    if (maximum_input_bytes > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) {
        return;
    }

    try {
        utf16_.resize(maximum_input_bytes);
        utf8_offsets_.resize(maximum_input_bytes + 1);
    } catch (const std::bad_alloc&) {
        std::vector<UChar>{}.swap(utf16_);
        std::vector<std::uint32_t>{}.swap(utf8_offsets_);
        return;
    } catch (const std::length_error&) {
        std::vector<UChar>{}.swap(utf16_);
        std::vector<std::uint32_t>{}.swap(utf8_offsets_);
        return;
    }

    const auto maximum_length = static_cast<std::int32_t>(maximum_input_bytes);
    UErrorCode status = U_ZERO_ERROR;
    paragraph_.reset(ubidi_openSized(maximum_length, 0, &status));
    if (U_FAILURE(status) || paragraph_ == nullptr) {
        paragraph_.reset();
        return;
    }

    status = U_ZERO_ERROR;
    line_.reset(ubidi_openSized(maximum_length, 0, &status));
    if (U_FAILURE(status)) {
        line_.reset();
        paragraph_.reset();
    }
}

TextBidiResolver::~TextBidiResolver() = default;

TextBidiResult TextBidiResolver::set_paragraph(
    std::string_view utf8,
    TextBidiParagraphDirection direction
) noexcept {
    paragraph_ready_ = false;
    input_bytes_ = 0;
    utf16_length_ = 0;
    if (maximum_input_bytes_ > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) {
        return TextBidiResult::input_limit_exceeded;
    }
    if (paragraph_ == nullptr) {
        return TextBidiResult::initialization_failed;
    }
    if (utf8.size() > maximum_input_bytes_ ||
        utf8.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) {
        return TextBidiResult::input_limit_exceeded;
    }

    UBiDiLevel level = UBIDI_LTR;
    if (!to_icu_paragraph_level(direction, level)) {
        return TextBidiResult::invalid_direction;
    }

    std::size_t byte_offset = 0;
    std::size_t utf16_offset = 0;
    utf8_offsets_[0] = 0;
    while (byte_offset < utf8.size()) {
        const auto code_point_byte_offset = byte_offset;
        std::uint32_t code_point = 0;
        if (!detail::next_utf8_code_point(utf8, byte_offset, code_point)) {
            return TextBidiResult::invalid_utf8;
        }
        if (code_point <= 0xffffU) {
            utf16_[utf16_offset] = static_cast<UChar>(code_point);
            ++utf16_offset;
            utf8_offsets_[utf16_offset] = static_cast<std::uint32_t>(byte_offset);
            continue;
        }

        code_point -= 0x10000U;
        utf16_[utf16_offset] = static_cast<UChar>(0xd800U + (code_point >> 10U));
        utf8_offsets_[utf16_offset] = static_cast<std::uint32_t>(code_point_byte_offset);
        ++utf16_offset;
        utf16_[utf16_offset] = static_cast<UChar>(0xdc00U + (code_point & 0x3ffU));
        utf8_offsets_[utf16_offset] = static_cast<std::uint32_t>(code_point_byte_offset);
        ++utf16_offset;
        utf8_offsets_[utf16_offset] = static_cast<std::uint32_t>(byte_offset);
    }

    if (line_ != nullptr) {
        static constexpr UChar empty_text = 0;
        UErrorCode line_status = U_ZERO_ERROR;
        ubidi_setPara(line_.get(), &empty_text, 0, UBIDI_LTR, nullptr, &line_status);
        if (U_FAILURE(line_status)) {
            line_.reset();
        }
    }

    if (!utf8.empty()) {
        UErrorCode status = U_ZERO_ERROR;
        ubidi_setPara(
            paragraph_.get(),
            utf16_.data(),
            static_cast<std::int32_t>(utf16_offset),
            level,
            nullptr,
            &status
        );
        if (U_FAILURE(status) || ubidi_countParagraphs(paragraph_.get()) != 1) {
            return U_FAILURE(status) ? TextBidiResult::initialization_failed
                                     : TextBidiResult::multiple_paragraphs;
        }
    }

    if (line_ == nullptr) {
        UErrorCode line_status = U_ZERO_ERROR;
        line_.reset(
            ubidi_openSized(static_cast<std::int32_t>(maximum_input_bytes_), 0, &line_status)
        );
        if (U_FAILURE(line_status) || line_ == nullptr) {
            line_.reset();
            return TextBidiResult::initialization_failed;
        }
    }

    input_bytes_ = utf8.size();
    utf16_length_ = utf16_offset;
    paragraph_ready_ = true;
    return TextBidiResult::complete;
}

TextBidiResult TextBidiResolver::visual_runs_for_line(
    std::size_t byte_offset,
    std::size_t byte_length,
    std::span<TextBidiRun> output,
    std::size_t& run_count
) noexcept {
    run_count = 0;
    if (!paragraph_ready_ || line_ == nullptr) {
        return TextBidiResult::initialization_failed;
    }
    if (byte_offset > input_bytes_ || byte_length > input_bytes_ - byte_offset) {
        return TextBidiResult::invalid_line_range;
    }
    if (utf16_length_ == 0 && byte_offset == 0 && byte_length == 0) {
        return TextBidiResult::complete;
    }

    const auto find_utf16_offset = [this](std::size_t byte_index, std::int32_t& unit_index) {
        const auto limit = utf8_offsets_.begin() + static_cast<std::ptrdiff_t>(utf16_length_ + 1);
        const auto match =
            std::lower_bound(utf8_offsets_.begin(), limit, static_cast<std::uint32_t>(byte_index));
        if (match == limit || *match != byte_index) {
            return false;
        }
        unit_index = static_cast<std::int32_t>(match - utf8_offsets_.begin());
        return true;
    };

    std::int32_t line_start = 0;
    std::int32_t line_limit = 0;
    if (!find_utf16_offset(byte_offset, line_start) ||
        !find_utf16_offset(byte_offset + byte_length, line_limit) || line_start >= line_limit) {
        return TextBidiResult::invalid_line_range;
    }

    UErrorCode status = U_ZERO_ERROR;
    ubidi_setLine(paragraph_.get(), line_start, line_limit, line_.get(), &status);
    if (U_FAILURE(status)) {
        return TextBidiResult::invalid_line_range;
    }

    const auto required_runs = ubidi_countRuns(line_.get(), &status);
    if (U_FAILURE(status) || required_runs < 0) {
        return TextBidiResult::initialization_failed;
    }
    run_count = static_cast<std::size_t>(required_runs);
    if (output.size() < run_count) {
        return TextBidiResult::insufficient_output;
    }

    for (std::int32_t index = 0; index < required_runs; ++index) {
        std::int32_t logical_start = 0;
        std::int32_t run_length = 0;
        const auto direction = ubidi_getVisualRun(line_.get(), index, &logical_start, &run_length);
        logical_start += line_start;
        const auto logical_limit =
            static_cast<std::size_t>(logical_start) + static_cast<std::size_t>(run_length);
        const auto run_byte_start = utf8_offsets_[static_cast<std::size_t>(logical_start)];
        const auto run_byte_limit = utf8_offsets_[logical_limit];
        output[static_cast<std::size_t>(index)] = {
            .byte_offset = run_byte_start,
            .byte_length = run_byte_limit - run_byte_start,
            .direction = direction == UBIDI_RTL ? TextDirection::right_to_left
                                                : TextDirection::left_to_right,
        };
    }
    return TextBidiResult::complete;
}

}
