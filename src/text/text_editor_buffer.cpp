#include "veyra/text/text_editor_buffer.hpp"

#include <cstring>
#include <functional>

namespace veyra {
namespace {

bool valid_utf8(std::string_view text) noexcept {
    std::size_t index = 0;
    while (index < text.size()) {
        const auto first = static_cast<unsigned char>(text[index]);
        if (first <= 0x7fU) {
            ++index;
            continue;
        }

        std::size_t continuation_count = 0;
        unsigned char second_minimum = 0x80U;
        unsigned char second_maximum = 0xbfU;
        if (first >= 0xc2U && first <= 0xdfU) {
            continuation_count = 1;
        } else if (first >= 0xe0U && first <= 0xefU) {
            continuation_count = 2;
            if (first == 0xe0U) {
                second_minimum = 0xa0U;
            } else if (first == 0xedU) {
                second_maximum = 0x9fU;
            }
        } else if (first >= 0xf0U && first <= 0xf4U) {
            continuation_count = 3;
            if (first == 0xf0U) {
                second_minimum = 0x90U;
            } else if (first == 0xf4U) {
                second_maximum = 0x8fU;
            }
        } else {
            return false;
        }

        if (continuation_count > text.size() - index - 1) {
            return false;
        }
        const auto second = static_cast<unsigned char>(text[index + 1]);
        if (second < second_minimum || second > second_maximum) {
            return false;
        }
        for (std::size_t offset = 2; offset <= continuation_count; ++offset) {
            const auto continuation = static_cast<unsigned char>(text[index + offset]);
            if (continuation < 0x80U || continuation > 0xbfU) {
                return false;
            }
        }
        index += continuation_count + 1;
    }
    return true;
}

bool is_utf8_boundary(std::string_view text, std::size_t byte_offset) noexcept {
    return byte_offset <= text.size() &&
        (byte_offset == text.size() ||
         (static_cast<unsigned char>(text[byte_offset]) & 0xc0U) != 0x80U);
}

bool overlaps(std::span<char> storage, const char* data, std::size_t length) noexcept {
    if (storage.empty() || length == 0) {
        return false;
    }

    const std::less<const char*> less;
    const auto* storage_begin = storage.data();
    const auto* storage_end = storage_begin + storage.size();
    const auto* input_end = data + length;
    return less(storage_begin, input_end) && less(data, storage_end);
}

}

TextEditResult utf8_byte_offset(
    std::string_view text,
    std::size_t character_offset,
    std::size_t& byte_offset
) noexcept {
    byte_offset = 0;
    if (!valid_utf8(text)) {
        return TextEditResult::invalid_utf8;
    }

    std::size_t character = 0;
    while (byte_offset < text.size() && character < character_offset) {
        const auto byte = static_cast<unsigned char>(text[byte_offset]);
        std::size_t sequence_length = 1;
        if (byte >= 0xc2U && byte <= 0xdfU) {
            sequence_length = 2;
        } else if (byte >= 0xe0U && byte <= 0xefU) {
            sequence_length = 3;
        } else if (byte >= 0xf0U && byte <= 0xf4U) {
            sequence_length = 4;
        }
        byte_offset += sequence_length;
        ++character;
    }
    if (character != character_offset) {
        byte_offset = 0;
        return TextEditResult::invalid_cursor;
    }
    return TextEditResult::complete;
}

TextEditorBuffer::TextEditorBuffer(std::span<char> storage, std::string_view initial_text) noexcept
    : storage_(storage), gap_end_(storage.size()) {
    if (!valid_utf8(initial_text)) {
        initialization_result_ = TextEditResult::invalid_utf8;
        return;
    }
    if (initial_text.size() > storage_.size()) {
        initialization_result_ = TextEditResult::insufficient_capacity;
        return;
    }

    if (!initial_text.empty()) {
        std::memmove(storage_.data(), initial_text.data(), initial_text.size());
    }
    gap_start_ = initial_text.size();
    selection_anchor_ = gap_start_;
}

TextEditResult TextEditorBuffer::initialization_result() const noexcept {
    return initialization_result_;
}

std::string_view TextEditorBuffer::text_before_cursor() const noexcept {
    if (initialization_result_ != TextEditResult::complete || gap_start_ == 0) {
        return {};
    }
    return {storage_.data(), gap_start_};
}

std::string_view TextEditorBuffer::text_after_cursor() const noexcept {
    if (initialization_result_ != TextEditResult::complete || gap_end_ == storage_.size()) {
        return {};
    }
    return {storage_.data() + gap_end_, storage_.size() - gap_end_};
}

std::size_t TextEditorBuffer::size() const noexcept {
    return gap_start_ + storage_.size() - gap_end_;
}

std::size_t TextEditorBuffer::cursor() const noexcept {
    return gap_start_;
}

TextSelection TextEditorBuffer::selection() const noexcept {
    const auto start = gap_start_ < selection_anchor_ ? gap_start_ : selection_anchor_;
    const auto end = gap_start_ > selection_anchor_ ? gap_start_ : selection_anchor_;
    return {.start = start, .length = end - start};
}

TextEditResult
TextEditorBuffer::copy_text(std::span<char> output, std::size_t& length) const noexcept {
    length = size();
    if (initialization_result_ != TextEditResult::complete) {
        length = 0;
        return initialization_result_;
    }
    if (output.size() < length) {
        return TextEditResult::insufficient_capacity;
    }
    if (overlaps(storage_, output.data(), length)) {
        return TextEditResult::overlapping_output;
    }

    const auto after_length = storage_.size() - gap_end_;
    if (gap_start_ > 0) {
        std::memcpy(output.data(), storage_.data(), gap_start_);
    }
    if (after_length > 0) {
        std::memcpy(output.data() + gap_start_, storage_.data() + gap_end_, after_length);
    }
    return TextEditResult::complete;
}

TextEditResult
TextEditorBuffer::set_cursor(std::size_t byte_offset, bool extend_selection) noexcept {
    if (initialization_result_ != TextEditResult::complete) {
        return initialization_result_;
    }
    if (!is_boundary(byte_offset)) {
        return TextEditResult::invalid_cursor;
    }

    cancel_composition();
    move_gap(byte_offset);
    if (!extend_selection) {
        selection_anchor_ = gap_start_;
    }
    return TextEditResult::complete;
}

TextEditResult
TextEditorBuffer::move_cursor(TextCursorDirection direction, bool extend_selection) noexcept {
    if (initialization_result_ != TextEditResult::complete) {
        return initialization_result_;
    }
    if (direction != TextCursorDirection::backward && direction != TextCursorDirection::forward) {
        return TextEditResult::invalid_direction;
    }

    cancel_composition();
    const auto current_selection = selection();
    if (current_selection.length > 0 && !extend_selection) {
        const auto destination = direction == TextCursorDirection::backward
            ? current_selection.start
            : current_selection.start + current_selection.length;
        move_gap(destination);
        selection_anchor_ = gap_start_;
        return TextEditResult::complete;
    }

    auto destination = gap_start_;
    if (direction == TextCursorDirection::backward && gap_start_ > 0) {
        --destination;
        while (destination > 0 &&
               (static_cast<unsigned char>(byte_at(destination)) & 0xc0U) == 0x80U) {
            --destination;
        }
    } else if (direction == TextCursorDirection::forward && gap_start_ < size()) {
        ++destination;
        while (destination < size() &&
               (static_cast<unsigned char>(byte_at(destination)) & 0xc0U) == 0x80U) {
            ++destination;
        }
    }

    move_gap(destination);
    if (!extend_selection) {
        selection_anchor_ = gap_start_;
    }
    return TextEditResult::complete;
}

void TextEditorBuffer::select_all() noexcept {
    if (initialization_result_ != TextEditResult::complete) {
        return;
    }
    cancel_composition();
    selection_anchor_ = 0;
    move_gap(size());
}

TextEditResult TextEditorBuffer::insert(std::string_view text) noexcept {
    if (initialization_result_ != TextEditResult::complete) {
        return initialization_result_;
    }
    if (!valid_utf8(text)) {
        return TextEditResult::invalid_utf8;
    }
    if (overlaps(storage_, text.data(), text.size())) {
        return TextEditResult::overlapping_input;
    }
    const auto current_selection = selection();
    const auto remaining_length = size() - current_selection.length;
    const auto available_capacity = storage_.size() - remaining_length;
    if (text.size() > available_capacity) {
        return TextEditResult::insufficient_capacity;
    }
    cancel_composition();
    return replace_selection(text);
}

TextEditResult TextEditorBuffer::erase_backward() noexcept {
    if (initialization_result_ != TextEditResult::complete) {
        return initialization_result_;
    }
    cancel_composition();
    const auto current_selection = selection();
    if (current_selection.length > 0) {
        erase_range(current_selection.start, current_selection.start + current_selection.length);
        return TextEditResult::complete;
    }
    if (gap_start_ == 0) {
        return TextEditResult::complete;
    }

    auto start = gap_start_ - 1;
    while (start > 0 && (static_cast<unsigned char>(byte_at(start)) & 0xc0U) == 0x80U) {
        --start;
    }
    erase_range(start, gap_start_);
    return TextEditResult::complete;
}

TextEditResult TextEditorBuffer::erase_forward() noexcept {
    if (initialization_result_ != TextEditResult::complete) {
        return initialization_result_;
    }
    cancel_composition();
    const auto current_selection = selection();
    if (current_selection.length > 0) {
        erase_range(current_selection.start, current_selection.start + current_selection.length);
        return TextEditResult::complete;
    }
    if (gap_end_ == storage_.size()) {
        return TextEditResult::complete;
    }

    auto end = gap_start_ + 1;
    while (end < size() && (static_cast<unsigned char>(byte_at(end)) & 0xc0U) == 0x80U) {
        ++end;
    }
    erase_range(gap_start_, end);
    return TextEditResult::complete;
}

TextEditResult TextEditorBuffer::update_composition(
    std::span<char> storage,
    std::string_view text,
    std::size_t cursor,
    TextSelection selection
) noexcept {
    if (initialization_result_ != TextEditResult::complete) {
        return initialization_result_;
    }
    if (!valid_utf8(text)) {
        return TextEditResult::invalid_utf8;
    }
    if (cursor > text.size() || selection.start > text.size() ||
        selection.length > text.size() - selection.start || !is_utf8_boundary(text, cursor) ||
        !is_utf8_boundary(text, selection.start) ||
        !is_utf8_boundary(text, selection.start + selection.length)) {
        return TextEditResult::invalid_cursor;
    }
    if (text.size() > storage.size()) {
        return TextEditResult::insufficient_capacity;
    }
    if (overlaps(storage_, text.data(), text.size()) ||
        overlaps(storage_, storage.data(), storage.size()) ||
        overlaps(storage, text.data(), text.size())) {
        return TextEditResult::overlapping_input;
    }

    if (!composition_active_) {
        const auto current_selection = this->selection();
        original_cursor_ = gap_start_;
        original_selection_anchor_ = selection_anchor_;
        composition_replacement_ = current_selection;
        composition_active_ = true;
    }

    if (!text.empty()) {
        std::memcpy(storage.data(), text.data(), text.size());
    }
    composition_storage_ = storage;
    composition_length_ = text.size();
    composition_cursor_ = cursor;
    composition_selection_start_ = selection.start;
    composition_selection_length_ = selection.length;
    return TextEditResult::complete;
}

TextComposition TextEditorBuffer::composition() const noexcept {
    if (!composition_active_) {
        return {};
    }
    const auto composition_text = composition_length_ == 0
        ? std::string_view{}
        : std::string_view(composition_storage_.data(), composition_length_);
    return {
        .text = composition_text,
        .replacement = composition_replacement_,
        .cursor = composition_cursor_,
        .selection = {
            .start = composition_selection_start_,
            .length = composition_selection_length_,
        },
    };
}

TextEditResult TextEditorBuffer::commit_composition() noexcept {
    if (initialization_result_ != TextEditResult::complete) {
        return initialization_result_;
    }
    if (!composition_active_) {
        return TextEditResult::invalid_state;
    }

    const auto remaining_length = size() - composition_replacement_.length;
    const auto available_capacity = storage_.size() - remaining_length;
    if (composition_length_ > available_capacity) {
        return TextEditResult::insufficient_capacity;
    }

    const std::string_view text(composition_storage_.data(), composition_length_);
    const auto start = composition_replacement_.start;
    const auto end = start + composition_replacement_.length;
    replace_range(start, end, text);
    clear_composition();
    return TextEditResult::complete;
}

void TextEditorBuffer::cancel_composition() noexcept {
    if (!composition_active_) {
        return;
    }
    move_gap(original_cursor_);
    selection_anchor_ = original_selection_anchor_;
    clear_composition();
}

char TextEditorBuffer::byte_at(std::size_t byte_offset) const noexcept {
    if (byte_offset < gap_start_) {
        return storage_[byte_offset];
    }
    return storage_[gap_end_ + byte_offset - gap_start_];
}

bool TextEditorBuffer::is_boundary(std::size_t byte_offset) const noexcept {
    return byte_offset <= size() &&
        (byte_offset == size() ||
         (static_cast<unsigned char>(byte_at(byte_offset)) & 0xc0U) != 0x80U);
}

void TextEditorBuffer::move_gap(std::size_t byte_offset) noexcept {
    if (byte_offset < gap_start_) {
        const auto moved_length = gap_start_ - byte_offset;
        std::memmove(
            storage_.data() + gap_end_ - moved_length,
            storage_.data() + byte_offset,
            moved_length
        );
        gap_start_ -= moved_length;
        gap_end_ -= moved_length;
        return;
    }
    if (byte_offset > gap_start_) {
        const auto moved_length = byte_offset - gap_start_;
        std::memmove(storage_.data() + gap_start_, storage_.data() + gap_end_, moved_length);
        gap_start_ += moved_length;
        gap_end_ += moved_length;
    }
}

TextEditResult TextEditorBuffer::replace_selection(std::string_view text) noexcept {
    const auto current_selection = selection();
    const auto remaining_length = size() - current_selection.length;
    const auto available_capacity = storage_.size() - remaining_length;
    if (text.size() > available_capacity) {
        return TextEditResult::insufficient_capacity;
    }

    replace_range(
        current_selection.start,
        current_selection.start + current_selection.length,
        text
    );
    return TextEditResult::complete;
}

void TextEditorBuffer::replace_range(
    std::size_t start,
    std::size_t end,
    std::string_view text
) noexcept {
    move_gap(start);
    gap_end_ += end - start;
    if (!text.empty()) {
        std::memcpy(storage_.data() + gap_start_, text.data(), text.size());
        gap_start_ += text.size();
    }
    selection_anchor_ = gap_start_;
}

void TextEditorBuffer::clear_composition() noexcept {
    composition_storage_ = {};
    composition_length_ = 0;
    composition_cursor_ = 0;
    composition_selection_start_ = 0;
    composition_selection_length_ = 0;
    composition_replacement_ = {};
    original_cursor_ = 0;
    original_selection_anchor_ = 0;
    composition_active_ = false;
}

void TextEditorBuffer::erase_range(std::size_t start, std::size_t end) noexcept {
    move_gap(start);
    gap_end_ += end - start;
    selection_anchor_ = gap_start_;
}

}
