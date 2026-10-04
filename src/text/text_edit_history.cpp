#include "text_edit_history.hpp"

#include <algorithm>
#include <cstring>
#include <functional>
#include <limits>

namespace veyra {
namespace {

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

TextEditResult TextEditorHistory::record(
    TextEditorBuffer& buffer,
    std::size_t start,
    std::size_t end,
    std::string_view inserted_text
) noexcept {
    if (buffer.history_storage_.empty()) {
        return TextEditResult::complete;
    }
    if (overlaps(buffer.history_storage_, inserted_text.data(), inserted_text.size())) {
        return TextEditResult::overlapping_input;
    }

    const auto removed_length = end - start;
    const auto capacity = buffer.history_storage_.size();
    constexpr auto record_overhead = sizeof(Record) + sizeof(std::size_t);
    if (removed_length > std::numeric_limits<std::size_t>::max() - inserted_text.size() ||
        removed_length + inserted_text.size() >
            std::numeric_limits<std::size_t>::max() - record_overhead) {
        return TextEditResult::insufficient_history_capacity;
    }
    const auto record_size = record_overhead + removed_length + inserted_text.size();
    if (record_size > capacity) {
        return TextEditResult::insufficient_history_capacity;
    }

    if (buffer.history_undo_count_ < buffer.history_record_count_) {
        const auto prefix_size = buffer.history_cursor_ >= buffer.history_start_
            ? buffer.history_cursor_ - buffer.history_start_
            : capacity - buffer.history_start_ + buffer.history_cursor_;
        buffer.history_used_ = prefix_size;
        buffer.history_end_ = buffer.history_cursor_;
        buffer.history_record_count_ = buffer.history_undo_count_;
    }

    while (capacity - buffer.history_used_ < record_size) {
        Record oldest{};
        read(buffer, buffer.history_start_, &oldest, sizeof(oldest));
        const auto oldest_size =
            sizeof(oldest) + oldest.removed_length + oldest.inserted_length + sizeof(std::size_t);
        buffer.history_start_ = advance(buffer, buffer.history_start_, oldest_size);
        buffer.history_used_ -= oldest_size;
        --buffer.history_record_count_;
        --buffer.history_undo_count_;
    }

    const auto record = Record{
        .start = start,
        .cursor_before = buffer.gap_start_,
        .selection_anchor_before = buffer.selection_anchor_,
        .removed_length = removed_length,
        .inserted_length = inserted_text.size(),
    };
    const auto record_offset = buffer.history_end_;
    write(buffer, record_offset, &record, sizeof(record));
    auto payload_offset = advance(buffer, record_offset, sizeof(record));
    copy_text_range(buffer, start, removed_length, payload_offset);
    payload_offset = advance(buffer, payload_offset, removed_length);
    if (!inserted_text.empty()) {
        write(buffer, payload_offset, inserted_text.data(), inserted_text.size());
    }
    const auto record_end = advance(buffer, record_offset, record_size);
    write(
        buffer,
        retreat(buffer, record_end, sizeof(record_size)),
        &record_size,
        sizeof(record_size)
    );

    buffer.history_end_ = record_end;
    buffer.history_cursor_ = record_end;
    buffer.history_used_ += record_size;
    ++buffer.history_record_count_;
    ++buffer.history_undo_count_;
    return TextEditResult::complete;
}

TextEditResult TextEditorHistory::undo(TextEditorBuffer& buffer) noexcept {
    if (buffer.initialization_result_ != TextEditResult::complete) {
        return buffer.initialization_result_;
    }
    if (!buffer.can_undo()) {
        return TextEditResult::invalid_state;
    }

    buffer.cancel_composition();
    const auto record_size_offset = retreat(buffer, buffer.history_cursor_, sizeof(std::size_t));
    std::size_t record_size = 0;
    read(buffer, record_size_offset, &record_size, sizeof(record_size));
    const auto record_offset = retreat(buffer, buffer.history_cursor_, record_size);
    Record record{};
    read(buffer, record_offset, &record, sizeof(record));

    const auto removed_text_offset = advance(buffer, record_offset, sizeof(record));
    replace_range(
        buffer,
        record.start,
        record.start + record.inserted_length,
        removed_text_offset,
        record.removed_length
    );
    buffer.move_gap(record.cursor_before);
    buffer.selection_anchor_ = record.selection_anchor_before;
    buffer.history_cursor_ = record_offset;
    --buffer.history_undo_count_;
    return TextEditResult::complete;
}

TextEditResult TextEditorHistory::redo(TextEditorBuffer& buffer) noexcept {
    if (buffer.initialization_result_ != TextEditResult::complete) {
        return buffer.initialization_result_;
    }
    if (!buffer.can_redo()) {
        return TextEditResult::invalid_state;
    }

    buffer.cancel_composition();
    Record record{};
    read(buffer, buffer.history_cursor_, &record, sizeof(record));
    const auto inserted_text_offset =
        advance(buffer, buffer.history_cursor_, sizeof(record) + record.removed_length);
    replace_range(
        buffer,
        record.start,
        record.start + record.removed_length,
        inserted_text_offset,
        record.inserted_length
    );
    buffer.move_gap(record.start + record.inserted_length);
    buffer.selection_anchor_ = buffer.gap_start_;
    buffer.history_cursor_ = advance(
        buffer,
        buffer.history_cursor_,
        sizeof(record) + record.removed_length + record.inserted_length + sizeof(std::size_t)
    );
    ++buffer.history_undo_count_;
    return TextEditResult::complete;
}

std::size_t TextEditorHistory::advance(
    const TextEditorBuffer& buffer,
    std::size_t offset,
    std::size_t amount
) noexcept {
    const auto capacity = buffer.history_storage_.size();
    const auto remaining = capacity - offset;
    return amount >= remaining ? amount - remaining : offset + amount;
}

std::size_t TextEditorHistory::retreat(
    const TextEditorBuffer& buffer,
    std::size_t offset,
    std::size_t amount
) noexcept {
    const auto capacity = buffer.history_storage_.size();
    return amount > offset ? capacity - (amount - offset) : offset - amount;
}

void TextEditorHistory::write(
    TextEditorBuffer& buffer,
    std::size_t offset,
    const void* source,
    std::size_t length
) noexcept {
    if (length == 0) {
        return;
    }

    const auto first_length = std::min(length, buffer.history_storage_.size() - offset);
    std::memcpy(buffer.history_storage_.data() + offset, source, first_length);
    if (first_length < length) {
        std::memcpy(
            buffer.history_storage_.data(),
            static_cast<const char*>(source) + first_length,
            length - first_length
        );
    }
}

void TextEditorHistory::read(
    const TextEditorBuffer& buffer,
    std::size_t offset,
    void* output,
    std::size_t length
) noexcept {
    if (length == 0) {
        return;
    }

    const auto first_length = std::min(length, buffer.history_storage_.size() - offset);
    std::memcpy(output, buffer.history_storage_.data() + offset, first_length);
    if (first_length < length) {
        std::memcpy(
            static_cast<char*>(output) + first_length,
            buffer.history_storage_.data(),
            length - first_length
        );
    }
}

void TextEditorHistory::copy_text_range(
    TextEditorBuffer& buffer,
    std::size_t start,
    std::size_t length,
    std::size_t history_offset
) noexcept {
    auto text_offset = start;
    auto remaining = length;
    while (remaining > 0) {
        const auto before_gap = text_offset < buffer.gap_start_;
        const auto segment_length =
            before_gap ? std::min(remaining, buffer.gap_start_ - text_offset) : remaining;
        const auto* source = before_gap
            ? buffer.storage_.data() + text_offset
            : buffer.storage_.data() + buffer.gap_end_ + text_offset - buffer.gap_start_;
        write(buffer, history_offset, source, segment_length);
        history_offset = advance(buffer, history_offset, segment_length);
        text_offset += segment_length;
        remaining -= segment_length;
    }
}

void TextEditorHistory::replace_range(
    TextEditorBuffer& buffer,
    std::size_t start,
    std::size_t end,
    std::size_t history_offset,
    std::size_t length
) noexcept {
    buffer.move_gap(start);
    buffer.gap_end_ += end - start;
    if (length == 0) {
        buffer.selection_anchor_ = buffer.gap_start_;
        return;
    }

    const auto first_length = std::min(length, buffer.history_storage_.size() - history_offset);
    std::memcpy(
        buffer.storage_.data() + buffer.gap_start_,
        buffer.history_storage_.data() + history_offset,
        first_length
    );
    if (first_length < length) {
        std::memcpy(
            buffer.storage_.data() + buffer.gap_start_ + first_length,
            buffer.history_storage_.data(),
            length - first_length
        );
    }
    buffer.gap_start_ += length;
    buffer.selection_anchor_ = buffer.gap_start_;
}

}
