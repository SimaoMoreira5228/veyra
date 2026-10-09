#include "text_edit_history.hpp"
#include "veyra/text/text_editor_buffer.hpp"

namespace veyra {

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

bool TextEditorBuffer::has_composition() const noexcept {
    return composition_active_;
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
    if (start == end && text.empty()) {
        clear_composition();
        return TextEditResult::complete;
    }

    const auto history_result = TextEditorHistory::record(*this, start, end, text);
    if (history_result != TextEditResult::complete) {
        return history_result;
    }

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

}
