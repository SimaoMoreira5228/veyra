#include "veyra/text/text_editor_buffer.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

std::string copied_text(const veyra::TextEditorBuffer& buffer) {
    std::array<char, 64> copy{};
    std::size_t length = 0;
    require(
        buffer.copy_text(copy, length) == veyra::TextEditResult::complete,
        "text copies into caller storage"
    );
    return {copy.data(), length};
}

void check_utf8_cursor_and_deletion() {
    std::array<char, 32> storage{};
    veyra::TextEditorBuffer buffer(storage, "A\xc2\xa2\xe2\x82\xacZ");
    require(
        buffer.initialization_result() == veyra::TextEditResult::complete,
        "valid UTF-8 fits in the caller buffer"
    );
    require(buffer.cursor() == 7, "initial cursor follows the text");
    require(
        buffer.set_cursor(2) == veyra::TextEditResult::invalid_cursor,
        "cursor cannot split a multibyte code point"
    );

    require(
        buffer.move_cursor(veyra::TextCursorDirection::backward) == veyra::TextEditResult::complete,
        "cursor moves backward"
    );
    require(buffer.cursor() == 6, "backward movement skips one complete code point");
    require(
        buffer.erase_backward() == veyra::TextEditResult::complete,
        "backspace removes the previous code point"
    );
    require(copied_text(buffer) == "A\xc2\xa2Z", "backspace preserves valid UTF-8 around the edit");
    require(
        buffer.erase_forward() == veyra::TextEditResult::complete,
        "delete removes the next code point"
    );
    require(copied_text(buffer) == "A\xc2\xa2", "delete preserves valid UTF-8 around the edit");

    std::array<char, 16> supplementary_storage{};
    veyra::TextEditorBuffer supplementary(supplementary_storage, "A\xf0\x90\x8d\x88Z");
    require(
        supplementary.initialization_result() == veyra::TextEditResult::complete,
        "supplementary Unicode code points are accepted"
    );
    require(
        supplementary.move_cursor(static_cast<veyra::TextCursorDirection>(99)) ==
            veyra::TextEditResult::invalid_direction,
        "unknown cursor directions are rejected"
    );
    require(
        supplementary.move_cursor(veyra::TextCursorDirection::backward) ==
            veyra::TextEditResult::complete,
        "cursor moves across a supplementary code point"
    );
    require(supplementary.cursor() == 5, "four-byte code point occupies one cursor step");
    require(
        supplementary.erase_backward() == veyra::TextEditResult::complete,
        "backspace removes a supplementary code point"
    );
    require(copied_text(supplementary) == "AZ", "supplementary deletion keeps valid UTF-8");
}

void check_selection_replacement_and_capacity() {
    std::array<char, 8> storage{};
    veyra::TextEditorBuffer buffer(storage, "abcd");
    require(buffer.set_cursor(1) == veyra::TextEditResult::complete, "cursor selects start");
    require(
        buffer.set_cursor(3, true) == veyra::TextEditResult::complete,
        "cursor extends the selection"
    );
    const auto selected = buffer.selection();
    require(selected.start == 1 && selected.length == 2, "selection reports byte offsets");

    require(
        buffer.insert("XYZ") == veyra::TextEditResult::complete,
        "replacement fits after selected text is removed"
    );
    require(buffer.text_before_cursor() == "aXYZ", "prefix ends at the cursor");
    require(buffer.text_after_cursor() == "d", "suffix starts after the cursor");
    require(copied_text(buffer) == "aXYZd", "insertion replaces the selected range");
    require(buffer.cursor() == 4 && buffer.selection().length == 0, "replacement resets selection");

    const auto before_failed_insert = copied_text(buffer);
    require(
        buffer.insert("12345") == veyra::TextEditResult::insufficient_capacity,
        "capacity failure is reported"
    );
    require(copied_text(buffer) == before_failed_insert, "capacity failure leaves text unchanged");

    require(
        buffer.set_cursor(1) == veyra::TextEditResult::complete,
        "cursor moves before selection"
    );
    require(
        buffer.set_cursor(3, true) == veyra::TextEditResult::complete,
        "selection can be made before a failed replacement"
    );
    const auto selection_before_failure = buffer.selection();
    require(
        buffer.insert("123456789") == veyra::TextEditResult::insufficient_capacity,
        "replacement capacity failure is reported"
    );
    require(
        copied_text(buffer) == before_failed_insert,
        "failed replacement leaves text unchanged"
    );
    require(
        buffer.selection().start == selection_before_failure.start &&
            buffer.selection().length == selection_before_failure.length,
        "failed replacement leaves selection unchanged"
    );

    buffer.select_all();
    require(buffer.selection().start == 0, "select all starts at byte zero");
    require(buffer.selection().length == buffer.size(), "select all covers the text");
    require(buffer.erase_backward() == veyra::TextEditResult::complete, "selection can be erased");
    require(buffer.size() == 0, "erasing selection clears the buffer");
}

void check_invalid_utf8_and_aliasing() {
    std::array<char, 8> storage{};
    veyra::TextEditorBuffer buffer(storage, "safe");
    const std::array<std::uint8_t, 2> invalid_utf8{0xc0, 0xaf};
    require(
        buffer.insert({reinterpret_cast<const char*>(invalid_utf8.data()), invalid_utf8.size()}) ==
            veyra::TextEditResult::invalid_utf8,
        "overlong UTF-8 is rejected"
    );
    require(copied_text(buffer) == "safe", "invalid input leaves text unchanged");

    const std::string_view alias(buffer.text_before_cursor().data(), 1);
    require(
        buffer.insert(alias) == veyra::TextEditResult::overlapping_input,
        "insertion rejects a source that aliases its backing storage"
    );

    std::size_t copied_length = 0;
    require(
        buffer.copy_text(storage, copied_length) == veyra::TextEditResult::overlapping_output,
        "copying into the backing storage is rejected"
    );
    require(copied_length == buffer.size(), "overlapping copy reports the required length");

    std::array<char, 3> short_output{'x', 'x', 'x'};
    require(
        buffer.copy_text(short_output, copied_length) ==
            veyra::TextEditResult::insufficient_capacity,
        "short output storage is reported"
    );
    require(copied_length == buffer.size(), "short output reports required capacity");
    require(short_output[0] == 'x', "short output is left unchanged");

    std::array<char, 2> short_storage{};
    const std::array<std::uint8_t, 3> surrogate_encoding{0xed, 0xa0, 0x80};
    const std::string_view surrogate_text(
        reinterpret_cast<const char*>(surrogate_encoding.data()),
        surrogate_encoding.size()
    );
    veyra::TextEditorBuffer invalid_initial_text(short_storage, surrogate_text);
    require(
        invalid_initial_text.initialization_result() == veyra::TextEditResult::invalid_utf8,
        "UTF-8 surrogate encodings are rejected during initialization"
    );
    require(invalid_initial_text.size() == 0, "invalid initialization exposes no text");

    veyra::TextEditorBuffer oversized_initial_text(short_storage, "long");
    require(
        oversized_initial_text.initialization_result() ==
            veyra::TextEditResult::insufficient_capacity,
        "initial text exceeding storage capacity is rejected"
    );
}

void check_composition_commit_and_cancel() {
    std::array<char, 16> storage{};
    std::array<char, 16> composition_storage{};
    veyra::TextEditorBuffer buffer(storage, "hello");
    require(buffer.set_cursor(1) == veyra::TextEditResult::complete, "composition range begins");
    require(
        buffer.set_cursor(4, true) == veyra::TextEditResult::complete,
        "composition replaces the current selection"
    );

    std::array<std::uint8_t, 6> event_text{0xe3, 0x81, 0xab, 0xe3, 0x81, 0xbb};
    const std::string_view event_text_view(
        reinterpret_cast<const char*>(event_text.data()),
        event_text.size()
    );
    require(
        buffer.update_composition(
            composition_storage,
            event_text_view,
            3,
            {.start = 3, .length = 3}
        ) == veyra::TextEditResult::complete,
        "preedit text is copied into caller storage"
    );
    event_text.fill('x');
    const auto preedit = buffer.composition();
    require(preedit.text == "\xe3\x81\xab\xe3\x81\xbb", "preedit survives event storage reuse");
    require(
        preedit.replacement.start == 1 && preedit.replacement.length == 3,
        "composition exposes the text range it replaces"
    );
    require(preedit.cursor == 3, "composition exposes its byte cursor");
    require(
        preedit.selection.start == 3 && preedit.selection.length == 3,
        "composition exposes its selected byte range"
    );
    require(copied_text(buffer) == "hello", "preedit doesn't modify committed text");

    const std::array<std::uint8_t, 1> invalid_preedit{0xff};
    require(
        buffer.update_composition(
            composition_storage,
            {reinterpret_cast<const char*>(invalid_preedit.data()), invalid_preedit.size()},
            0,
            {}
        ) == veyra::TextEditResult::invalid_utf8,
        "invalid preedit is rejected"
    );
    require(buffer.composition().text == preedit.text, "invalid updates keep the old composition");
    require(
        buffer.update_composition(composition_storage, "\xc2\xa2", 1, {}) ==
            veyra::TextEditResult::invalid_cursor,
        "composition cursors cannot split UTF-8 code points"
    );
    require(buffer.composition().text == preedit.text, "invalid cursor keeps the old composition");

    buffer.cancel_composition();
    require(buffer.composition().text.empty(), "cancel clears the composition");
    require(copied_text(buffer) == "hello", "cancel leaves committed text unchanged");
    require(buffer.cursor() == 4, "cancel restores the original cursor");
    require(
        buffer.selection().start == 1 && buffer.selection().length == 3,
        "cancel restores the original selection"
    );

    require(
        buffer.update_composition(composition_storage, "X", 1, {}) ==
            veyra::TextEditResult::complete,
        "a new composition can start after cancellation"
    );
    require(
        buffer.commit_composition() == veyra::TextEditResult::complete,
        "composition can replace selected text"
    );
    require(copied_text(buffer) == "hXo", "commit replaces the original selection");
    require(buffer.cursor() == 2, "commit places the cursor after inserted text");
    require(buffer.composition().text.empty(), "commit clears the composition");
    require(
        buffer.commit_composition() == veyra::TextEditResult::invalid_state,
        "commit without an active composition is rejected"
    );
}

void check_composition_capacity_failure() {
    std::array<char, 5> storage{};
    std::array<char, 8> composition_storage{};
    veyra::TextEditorBuffer buffer(storage, "abcd");
    require(buffer.set_cursor(1) == veyra::TextEditResult::complete, "selection starts");
    require(
        buffer.set_cursor(2, true) == veyra::TextEditResult::complete,
        "one code point is selected"
    );
    require(
        buffer.update_composition(composition_storage, "XYZ", 3, {}) ==
            veyra::TextEditResult::complete,
        "preedit is stored before commit"
    );
    require(
        buffer.update_composition(storage, "X", 1, {}) == veyra::TextEditResult::overlapping_input,
        "composition scratch storage cannot alias committed text"
    );
    require(buffer.composition().text == "XYZ", "overlapping scratch leaves composition intact");
    require(
        buffer.insert("XYZ") == veyra::TextEditResult::insufficient_capacity,
        "a failed committed insertion leaves active composition alone"
    );
    require(buffer.composition().text == "XYZ", "failed insertion preserves composition");
    require(
        buffer.commit_composition() == veyra::TextEditResult::insufficient_capacity,
        "composition commit reports insufficient document capacity"
    );
    require(copied_text(buffer) == "abcd", "failed commit leaves committed text unchanged");
    require(buffer.composition().text == "XYZ", "failed commit keeps preedit available");
    buffer.cancel_composition();
    require(buffer.cursor() == 2, "cancel after failed commit restores the cursor");
    require(
        buffer.selection().start == 1 && buffer.selection().length == 1,
        "cancel after failed commit restores selection"
    );
}

void check_utf8_character_offsets() {
    const std::string_view text("A\xc2\xa2\xe2\x82\xac");
    std::size_t byte_offset = 99;
    require(
        veyra::utf8_byte_offset(text, 0, byte_offset) == veyra::TextEditResult::complete &&
            byte_offset == 0,
        "first character maps to the first byte"
    );
    require(
        veyra::utf8_byte_offset(text, 2, byte_offset) == veyra::TextEditResult::complete &&
            byte_offset == 3,
        "UTF-8 character offsets map across multibyte text"
    );
    require(
        veyra::utf8_byte_offset(text, 3, byte_offset) == veyra::TextEditResult::complete &&
            byte_offset == text.size(),
        "the offset after the final character maps to the end"
    );
    require(
        veyra::utf8_byte_offset(text, 4, byte_offset) == veyra::TextEditResult::invalid_cursor &&
            byte_offset == 0,
        "character offsets past the end are rejected"
    );
}

}

int main() {
    try {
        check_utf8_cursor_and_deletion();
        check_selection_replacement_and_capacity();
        check_invalid_utf8_and_aliasing();
        check_composition_commit_and_cancel();
        check_composition_capacity_failure();
        check_utf8_character_offsets();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
