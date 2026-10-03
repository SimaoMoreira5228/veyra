#include "veyra/text/grapheme.hpp"

#include <unicode/uchar.h>

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace veyra {
namespace {

class TextSegments {
  public:
    TextSegments(std::string_view before, std::string_view after) noexcept :
        before_(before), after_(after) {
    }

    std::size_t size() const noexcept {
        return before_.size() + after_.size();
    }

    unsigned char byte_at(std::size_t index) const noexcept {
        if (index < before_.size()) {
            return static_cast<unsigned char>(before_[index]);
        }
        return static_cast<unsigned char>(after_[index - before_.size()]);
    }

    UChar32 code_point_at(std::size_t index) const noexcept {
        const auto first = byte_at(index);
        if (first <= 0x7fU) {
            return first;
        }

        std::size_t sequence_length = 0;
        UChar32 value = 0;
        if (first <= 0xdfU) {
            sequence_length = 2;
            value = static_cast<UChar32>(first & 0x1fU);
        } else if (first <= 0xefU) {
            sequence_length = 3;
            value = static_cast<UChar32>(first & 0x0fU);
        } else {
            sequence_length = 4;
            value = static_cast<UChar32>(first & 0x07U);
        }
        for (std::size_t offset = 1; offset < sequence_length; ++offset) {
            value = static_cast<UChar32>((value << 6U) | (byte_at(index + offset) & 0x3fU));
        }
        return value;
    }

    std::size_t next_code_point(std::size_t index) const noexcept {
        const auto first = byte_at(index);
        if (first <= 0x7fU) {
            return index + 1;
        }
        if (first <= 0xdfU) {
            return index + 2;
        }
        if (first <= 0xefU) {
            return index + 3;
        }
        return index + 4;
    }

    std::size_t previous_code_point(std::size_t index) const noexcept {
        --index;
        while (index > 0 && (byte_at(index) & 0xc0U) == 0x80U) {
            --index;
        }
        return index;
    }

  private:
    std::string_view before_;
    std::string_view after_;
};

int grapheme_class(UChar32 code_point) noexcept {
    return u_getIntPropertyValue(code_point, UCHAR_GRAPHEME_CLUSTER_BREAK);
}

int indic_conjunct_class(UChar32 code_point) noexcept {
    return u_getIntPropertyValue(code_point, UCHAR_INDIC_CONJUNCT_BREAK);
}

bool is_control_class(int value) noexcept {
    return value == U_GCB_CONTROL || value == U_GCB_CR || value == U_GCB_LF;
}

bool breaks_before(const TextSegments& text, std::size_t left, std::size_t right) noexcept {
    const auto left_code_point = text.code_point_at(left);
    const auto right_code_point = text.code_point_at(right);
    const auto left_class = grapheme_class(left_code_point);
    const auto right_class = grapheme_class(right_code_point);

    if (left_class == U_GCB_CR && right_class == U_GCB_LF) {
        return false;
    }
    if (is_control_class(left_class) || is_control_class(right_class)) {
        return true;
    }
    if (left_class == U_GCB_L &&
        (right_class == U_GCB_L || right_class == U_GCB_V || right_class == U_GCB_LV ||
         right_class == U_GCB_LVT)) {
        return false;
    }
    if ((left_class == U_GCB_LV || left_class == U_GCB_V) &&
        (right_class == U_GCB_V || right_class == U_GCB_T)) {
        return false;
    }
    if ((left_class == U_GCB_LVT || left_class == U_GCB_T) && right_class == U_GCB_T) {
        return false;
    }
    if (right_class == U_GCB_E_MODIFIER) {
        auto candidate = left;
        while (true) {
            const auto candidate_class = grapheme_class(text.code_point_at(candidate));
            if (candidate_class == U_GCB_E_BASE || candidate_class == U_GCB_E_BASE_GAZ) {
                return false;
            }
            if (candidate_class != U_GCB_EXTEND || candidate == 0) {
                break;
            }
            candidate = text.previous_code_point(candidate);
        }
    }
    if (right_class == U_GCB_EXTEND || right_class == U_GCB_ZWJ) {
        return false;
    }
    if (right_class == U_GCB_SPACING_MARK || left_class == U_GCB_PREPEND) {
        return false;
    }
    if (indic_conjunct_class(right_code_point) == U_INCB_CONSONANT) {
        auto candidate = left;
        auto found_linker = false;
        while (true) {
            const auto candidate_class = indic_conjunct_class(text.code_point_at(candidate));
            if (candidate_class == U_INCB_LINKER) {
                found_linker = true;
            } else if (candidate_class == U_INCB_CONSONANT) {
                if (found_linker) {
                    return false;
                }
                break;
            } else if (candidate_class != U_INCB_EXTEND) {
                break;
            }
            if (candidate == 0) {
                break;
            }
            candidate = text.previous_code_point(candidate);
        }
    }
    if (u_hasBinaryProperty(right_code_point, UCHAR_EXTENDED_PICTOGRAPHIC) &&
        left_class == U_GCB_ZWJ && left > 0) {
        auto candidate = text.previous_code_point(left);
        while (grapheme_class(text.code_point_at(candidate)) == U_GCB_EXTEND && candidate > 0) {
            candidate = text.previous_code_point(candidate);
        }
        if (u_hasBinaryProperty(text.code_point_at(candidate), UCHAR_EXTENDED_PICTOGRAPHIC)) {
            return false;
        }
    }
    if (left_class == U_GCB_REGIONAL_INDICATOR && right_class == U_GCB_REGIONAL_INDICATOR) {
        auto candidate = left;
        auto regional_indicator_count = std::size_t{0};
        while (grapheme_class(text.code_point_at(candidate)) == U_GCB_REGIONAL_INDICATOR) {
            ++regional_indicator_count;
            if (candidate == 0) {
                break;
            }
            candidate = text.previous_code_point(candidate);
        }
        if ((regional_indicator_count & 1U) != 0U) {
            return false;
        }
    }
    return true;
}

std::size_t previous_grapheme_boundary(const TextSegments& text, std::size_t cursor) noexcept {
    auto right = text.previous_code_point(cursor);
    while (right > 0) {
        const auto left = text.previous_code_point(right);
        if (breaks_before(text, left, right)) {
            return right;
        }
        right = left;
    }
    return 0;
}

std::size_t next_grapheme_boundary(const TextSegments& text, std::size_t cursor) noexcept {
    const auto length = text.size();
    if (cursor == length) {
        return length;
    }

    auto left = cursor;
    auto right = text.next_code_point(left);
    while (right < length) {
        if (breaks_before(text, left, right)) {
            return right;
        }
        left = right;
        right = text.next_code_point(right);
    }
    return length;
}

bool is_grapheme_boundary(const TextSegments& text, std::size_t cursor) noexcept {
    if (cursor == 0 || cursor == text.size()) {
        return true;
    }
    return breaks_before(text, text.previous_code_point(cursor), cursor);
}

}

TextEditResult
move_cursor_by_grapheme(TextEditorBuffer& buffer, TextCursorDirection direction) noexcept {
    const auto initialization_result = buffer.initialization_result();
    if (initialization_result != TextEditResult::complete) {
        return initialization_result;
    }
    if (direction != TextCursorDirection::backward && direction != TextCursorDirection::forward) {
        return TextEditResult::invalid_direction;
    }

    const auto selection = buffer.selection();
    if (selection.length > 0) {
        const auto destination = direction == TextCursorDirection::backward
            ? selection.start
            : selection.start + selection.length;
        return buffer.set_cursor(destination);
    }

    if (direction == TextCursorDirection::backward && buffer.cursor() == 0) {
        return TextEditResult::complete;
    }
    if (direction == TextCursorDirection::forward && buffer.cursor() == buffer.size()) {
        return TextEditResult::complete;
    }

    const TextSegments text(buffer.text_before_cursor(), buffer.text_after_cursor());
    if (direction == TextCursorDirection::backward) {
        return buffer.set_cursor(previous_grapheme_boundary(text, buffer.cursor()));
    }
    return buffer.set_cursor(next_grapheme_boundary(text, buffer.cursor()));
}

TextEditResult erase_grapheme(TextEditorBuffer& buffer, TextCursorDirection direction) noexcept {
    const auto initialization_result = buffer.initialization_result();
    if (initialization_result != TextEditResult::complete) {
        return initialization_result;
    }
    if (direction != TextCursorDirection::backward && direction != TextCursorDirection::forward) {
        return TextEditResult::invalid_direction;
    }
    if (buffer.selection().length > 0) {
        return buffer.erase_backward();
    }
    if (direction == TextCursorDirection::backward && buffer.cursor() == 0) {
        return TextEditResult::complete;
    }
    if (direction == TextCursorDirection::forward && buffer.cursor() == buffer.size()) {
        return TextEditResult::complete;
    }

    const TextSegments text(buffer.text_before_cursor(), buffer.text_after_cursor());
    if (is_grapheme_boundary(text, buffer.cursor())) {
        const auto destination = direction == TextCursorDirection::backward
            ? previous_grapheme_boundary(text, buffer.cursor())
            : next_grapheme_boundary(text, buffer.cursor());
        const auto cursor_result = buffer.set_cursor(destination, true);
        if (cursor_result != TextEditResult::complete) {
            return cursor_result;
        }
        return buffer.erase_backward();
    }

    const auto start = previous_grapheme_boundary(text, buffer.cursor());
    const auto end = next_grapheme_boundary(text, buffer.cursor());
    const auto start_result = buffer.set_cursor(start);
    if (start_result != TextEditResult::complete) {
        return start_result;
    }
    const auto end_result = buffer.set_cursor(end, true);
    if (end_result != TextEditResult::complete) {
        return end_result;
    }
    return buffer.erase_backward();
}

}
