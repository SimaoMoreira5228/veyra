#include "veyra/text/text_editor_buffer.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <span>
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

void check_selection_copy() {
    constexpr std::string_view selected_text = "\xc2\xa2\xe2\x82\xac";
    std::array<char, 16> storage{};
    veyra::TextEditorBuffer buffer(storage, "A\xc2\xa2\xe2\x82\xacZ");
    std::array<char, 8> output{};
    std::size_t length = 0;

    require(
        buffer.set_cursor(1) == veyra::TextEditResult::complete,
        "selection-copy cursor moves to the prefix"
    );
    require(
        buffer.set_cursor(6, true) == veyra::TextEditResult::complete,
        "selection-copy cursor extends forward"
    );
    require(
        buffer.copy_selection(output, length) == veyra::TextEditResult::complete,
        "forward selection copies into caller storage"
    );
    require(length == selected_text.size(), "selection copy reports its byte length");
    require(
        std::string_view(output.data(), length) == selected_text,
        "selection copy preserves multibyte UTF-8"
    );

    require(
        buffer.set_cursor(6) == veyra::TextEditResult::complete,
        "selection-copy cursor returns to the suffix"
    );
    require(
        buffer.set_cursor(1, true) == veyra::TextEditResult::complete,
        "selection-copy cursor extends backward"
    );
    require(
        buffer.copy_selection(output, length) == veyra::TextEditResult::complete,
        "backward selection copies into caller storage"
    );
    require(
        std::string_view(output.data(), length) == selected_text,
        "backward selection copies in document order"
    );

    std::array<char, 4> short_output{'x', 'x', 'x', 'x'};
    require(
        buffer.copy_selection(short_output, length) == veyra::TextEditResult::insufficient_capacity,
        "short selection output is rejected"
    );
    require(length == selected_text.size(), "short selection output reports required capacity");
    require(short_output[0] == 'x', "short selection output stays unchanged");
    require(
        buffer.copy_selection(storage, length) == veyra::TextEditResult::overlapping_output,
        "selection copy rejects the editor's own storage"
    );
    require(length == selected_text.size(), "overlapping selection copy reports its length");

    require(
        buffer.set_cursor(1) == veyra::TextEditResult::complete,
        "selection-copy cursor can clear its selection"
    );
    require(
        buffer.copy_selection(output, length) == veyra::TextEditResult::complete,
        "empty selection copies successfully"
    );
    require(length == 0, "empty selection reports zero bytes");
}

void check_undo_redo() {
    std::array<char, 32> storage{};
    std::array<char, 256> history{};
    veyra::TextEditorBuffer buffer(storage, "ab", history);
    require(!buffer.can_undo(), "new editor has no undo records");
    require(!buffer.can_redo(), "new editor has no redo records");
    require(
        buffer.undo() == veyra::TextEditResult::invalid_state,
        "undo without history is rejected"
    );
    require(buffer.insert("") == veyra::TextEditResult::complete, "empty insert is a no-op");
    require(!buffer.can_undo(), "empty insert does not add an undo record");

    require(buffer.insert("c") == veyra::TextEditResult::complete, "insert enters history");
    require(copied_text(buffer) == "abc", "insert updates the document");
    require(buffer.can_undo(), "insert can be undone");
    require(buffer.undo() == veyra::TextEditResult::complete, "insert can be undone");
    require(copied_text(buffer) == "ab", "undo restores deleted text");
    require(buffer.cursor() == 2, "undo restores the original cursor");
    require(buffer.can_redo(), "undone insert can be redone");
    require(buffer.redo() == veyra::TextEditResult::complete, "insert can be redone");
    require(copied_text(buffer) == "abc", "redo restores inserted text");
    require(buffer.cursor() == 3, "redo restores the edited cursor");

    require(buffer.undo() == veyra::TextEditResult::complete, "insert can be undone again");
    require(buffer.insert("X") == veyra::TextEditResult::complete, "new edit branches history");
    require(copied_text(buffer) == "abX", "branched edit replaces the redo path");
    require(!buffer.can_redo(), "new edit clears the redo path");
    require(buffer.undo() == veyra::TextEditResult::complete, "branched edit can be undone");
    require(copied_text(buffer) == "ab", "branch undo restores the prior document");

    require(buffer.set_cursor(1) == veyra::TextEditResult::complete, "delete cursor moves");
    require(buffer.erase_forward() == veyra::TextEditResult::complete, "delete enters history");
    require(copied_text(buffer) == "a", "delete removes the next character");
    require(buffer.undo() == veyra::TextEditResult::complete, "delete can be undone");
    require(copied_text(buffer) == "ab", "delete undo restores removed text");
    require(buffer.cursor() == 1, "delete undo restores the cursor");
    require(buffer.redo() == veyra::TextEditResult::complete, "delete can be redone");
    require(copied_text(buffer) == "a", "delete redo removes restored text");
}

void check_undo_restores_selection_and_composition() {
    std::array<char, 32> storage{};
    std::array<char, 256> history{};
    veyra::TextEditorBuffer buffer(storage, "A\xc2\xa2Z", history);
    require(
        buffer.set_cursor(1) == veyra::TextEditResult::complete,
        "selection starts before cent"
    );
    require(
        buffer.set_cursor(3, true) == veyra::TextEditResult::complete,
        "selection covers the multibyte character"
    );
    require(
        buffer.insert("\xe2\x82\xac") == veyra::TextEditResult::complete,
        "selection is replaced"
    );
    require(copied_text(buffer) == "A\xe2\x82\xacZ", "replacement keeps UTF-8 valid");
    require(buffer.undo() == veyra::TextEditResult::complete, "replacement can be undone");
    require(copied_text(buffer) == "A\xc2\xa2Z", "undo restores replaced UTF-8");
    require(buffer.cursor() == 3, "undo restores the active selection endpoint");
    require(
        buffer.selection().start == 1 && buffer.selection().length == 2,
        "undo restores the original selection"
    );
    require(buffer.redo() == veyra::TextEditResult::complete, "replacement can be redone");
    require(copied_text(buffer) == "A\xe2\x82\xacZ", "redo restores replacement text");

    require(
        buffer.set_cursor(4) == veyra::TextEditResult::complete,
        "reverse selection cursor moves"
    );
    require(
        buffer.set_cursor(1, true) == veyra::TextEditResult::complete,
        "reverse selection extends to the prefix"
    );
    require(buffer.insert("!") == veyra::TextEditResult::complete, "reverse selection is replaced");
    require(buffer.undo() == veyra::TextEditResult::complete, "reverse replacement can be undone");
    require(copied_text(buffer) == "A\xe2\x82\xacZ", "reverse undo restores replaced text");
    require(buffer.cursor() == 1, "reverse undo restores its active endpoint");
    require(
        buffer.selection().start == 1 && buffer.selection().length == 3,
        "reverse undo restores the original selection range"
    );

    std::array<char, 16> composition_text_storage{};
    std::array<char, 256> composition_history{};
    std::array<char, 8> composition_storage{};
    veyra::TextEditorBuffer composing(composition_text_storage, "A", composition_history);
    require(
        composing.update_composition(composition_storage, "日", 3, {}) ==
            veyra::TextEditResult::complete,
        "composition starts"
    );
    require(
        composing.commit_composition() == veyra::TextEditResult::complete,
        "composition commit enters history"
    );
    require(copied_text(composing) == "A日", "composition commits its UTF-8 text");
    require(
        composing.undo() == veyra::TextEditResult::complete,
        "composition commit can be undone"
    );
    require(copied_text(composing) == "A", "composition undo restores committed text");
}

void check_undo_capacity_and_eviction() {
    std::array<char, 16> storage{};
    std::array<char, 16> short_history{};
    veyra::TextEditorBuffer limited(storage, "a", short_history);
    require(
        limited.insert("b") == veyra::TextEditResult::insufficient_history_capacity,
        "edit fails when its undo record does not fit"
    );
    require(copied_text(limited) == "a", "history capacity failure leaves text unchanged");
    require(!limited.can_undo(), "failed edit does not create a history record");
    std::size_t copied_length = 0;
    require(
        limited.copy_text(short_history, copied_length) ==
            veyra::TextEditResult::overlapping_output,
        "copying into history storage is rejected"
    );

    std::array<char, 16> composition_text_storage{};
    std::array<char, 16> composition_history{};
    std::array<char, 8> composition_storage{};
    veyra::TextEditorBuffer composing(composition_text_storage, "a", composition_history);
    require(
        composing.update_composition(composition_storage, "b", 1, {}) ==
            veyra::TextEditResult::complete,
        "composition starts with limited history"
    );
    require(
        composing.commit_composition() == veyra::TextEditResult::insufficient_history_capacity,
        "composition commit reports an undersized history buffer"
    );
    require(composing.composition().text == "b", "failed history commit keeps composition active");
    require(copied_text(composing) == "a", "failed history commit leaves text unchanged");

    std::array<char, 16> overlap_storage{};
    veyra::TextEditorBuffer overlapping(
        overlap_storage,
        "a",
        std::span<char>(overlap_storage).subspan(8)
    );
    require(
        overlapping.initialization_result() == veyra::TextEditResult::invalid_state,
        "document and history storage cannot overlap"
    );

    std::array<char, 8> text_storage{};
    std::array<char, 100> bounded_history{};
    veyra::TextEditorBuffer bounded(text_storage, "", bounded_history);
    require(bounded.insert("A") == veyra::TextEditResult::complete, "first bounded edit fits");
    require(bounded.insert("B") == veyra::TextEditResult::complete, "second bounded edit fits");
    require(bounded.insert("C") == veyra::TextEditResult::complete, "oldest record is evicted");
    require(copied_text(bounded) == "ABC", "bounded history keeps all current text");
    require(bounded.undo() == veyra::TextEditResult::complete, "newest record remains undoable");
    require(
        bounded.undo() == veyra::TextEditResult::complete,
        "second newest record remains undoable"
    );
    require(copied_text(bounded) == "A", "eviction retains the newest history records");
    require(
        bounded.undo() == veyra::TextEditResult::invalid_state,
        "evicted record is no longer undoable"
    );
    require(bounded.redo() == veyra::TextEditResult::complete, "wrapped record can be redone");
    require(bounded.redo() == veyra::TextEditResult::complete, "wrapped payload can be redone");
    require(copied_text(bounded) == "ABC", "redo restores wrapped history text");
    require(
        bounded.undo() == veyra::TextEditResult::complete,
        "wrapped record can be undone again"
    );
    require(
        bounded.insert("D") == veyra::TextEditResult::complete,
        "wrapped redo path can be replaced"
    );
    require(copied_text(bounded) == "ABD", "new edit replaces the wrapped redo path");
    require(bounded.undo() == veyra::TextEditResult::complete, "wrapped branch can be undone");
    require(
        bounded.undo() == veyra::TextEditResult::complete,
        "older bounded edit remains undoable"
    );
    require(copied_text(bounded) == "A", "wrapped branch preserves the older record");
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

    const auto alias = buffer.text_before_cursor().substr(0, 1);
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

void check_utf8_encoding_boundaries() {
    std::array<char, 16> storage{};
    veyra::TextEditorBuffer buffer(storage, "safe");
    require(buffer.cursor() == 4, "boundary buffer starts after its text");

    const std::array<std::uint8_t, 3> e0_overlong{0xe0, 0x80, 0xaf};
    const std::string_view e0_overlong_text(
        reinterpret_cast<const char*>(e0_overlong.data()),
        e0_overlong.size()
    );
    require(
        buffer.insert(e0_overlong_text) == veyra::TextEditResult::invalid_utf8,
        "three-byte overlong encodings are rejected"
    );
    require(copied_text(buffer) == "safe", "overlong input leaves text unchanged");
    require(buffer.cursor() == 4, "overlong input leaves the cursor alone");
    require(buffer.selection().length == 0, "overlong input leaves the selection alone");

    require(
        buffer.insert("\xE0\xA0\x80") == veyra::TextEditResult::complete,
        "the three-byte lower boundary is accepted"
    );
    require(copied_text(buffer) == "safe\xE0\xA0\x80", "the lowest three-byte character is stored");

    std::array<char, 8> max_storage{};
    veyra::TextEditorBuffer max_init(max_storage, "\xF4\x8F\xBF\xBF");
    require(
        max_init.initialization_result() == veyra::TextEditResult::complete,
        "U+10FFFF is accepted during initialization"
    );
    require(max_init.size() == 4, "the maximum character occupies four bytes");

    const std::array<std::uint8_t, 4> beyond_max{0xf4, 0x90, 0x80, 0x80};
    const std::string_view beyond_max_text(
        reinterpret_cast<const char*>(beyond_max.data()),
        beyond_max.size()
    );
    require(
        buffer.insert(beyond_max_text) == veyra::TextEditResult::invalid_utf8,
        "encodings beyond U+10FFFF are rejected"
    );
    require(
        copied_text(buffer) == "safe\xE0\xA0\x80",
        "beyond-maximum input leaves text unchanged"
    );

    const std::array<std::uint8_t, 2> truncated{0xe2, 0x82};
    const std::string_view truncated_text(
        reinterpret_cast<const char*>(truncated.data()),
        truncated.size()
    );
    require(
        buffer.insert(truncated_text) == veyra::TextEditResult::invalid_utf8,
        "truncated sequences are rejected on insert"
    );
    require(copied_text(buffer) == "safe\xE0\xA0\x80", "truncated input leaves text unchanged");
    std::array<char, 4> truncated_storage{};
    veyra::TextEditorBuffer truncated_init(truncated_storage, "\xC2");
    require(
        truncated_init.initialization_result() == veyra::TextEditResult::invalid_utf8,
        "truncated sequences are rejected during initialization"
    );
    require(truncated_init.size() == 0, "truncated initialization exposes no text");

    const std::array<std::uint8_t, 3> bad_third{0xe2, 0x82, 0x28};
    const std::string_view bad_third_text(
        reinterpret_cast<const char*>(bad_third.data()),
        bad_third.size()
    );
    require(
        buffer.insert(bad_third_text) == veyra::TextEditResult::invalid_utf8,
        "invalid third bytes are rejected"
    );
    const std::array<std::uint8_t, 4> bad_fourth{0xf0, 0x90, 0x28, 0x8c};
    const std::string_view bad_fourth_text(
        reinterpret_cast<const char*>(bad_fourth.data()),
        bad_fourth.size()
    );
    require(
        buffer.insert(bad_fourth_text) == veyra::TextEditResult::invalid_utf8,
        "invalid fourth bytes are rejected"
    );
    require(
        copied_text(buffer) == "safe\xE0\xA0\x80",
        "bad continuation input leaves text unchanged"
    );

    const std::array<std::uint8_t, 3> surrogate{0xed, 0xa0, 0x80};
    const std::string_view surrogate_text(
        reinterpret_cast<const char*>(surrogate.data()),
        surrogate.size()
    );
    require(
        buffer.insert(surrogate_text) == veyra::TextEditResult::invalid_utf8,
        "surrogate encodings are rejected on insert"
    );
    require(copied_text(buffer) == "safe\xE0\xA0\x80", "surrogate input leaves text unchanged");

    std::size_t byte_offset = 99;
    require(
        veyra::utf8_byte_offset("\xFF", 0, byte_offset) == veyra::TextEditResult::invalid_utf8 &&
            byte_offset == 0,
        "character offsets reject invalid UTF-8"
    );
    const std::string_view supplementary("A\xF0\x90\x8D\x88Z");
    require(
        veyra::utf8_byte_offset(supplementary, 1, byte_offset) == veyra::TextEditResult::complete &&
            byte_offset == 1,
        "character offsets map before a four-byte character"
    );
    require(
        veyra::utf8_byte_offset(supplementary, 2, byte_offset) == veyra::TextEditResult::complete &&
            byte_offset == 5,
        "character offsets skip a four-byte character"
    );
    require(
        veyra::utf8_byte_offset(supplementary, 3, byte_offset) == veyra::TextEditResult::complete &&
            byte_offset == supplementary.size(),
        "character offsets map after a four-byte character"
    );
}

void check_aliasing_guards_cover_both_spans() {
    std::array<char, 16> storage{};
    std::array<char, 64> history{};
    veyra::TextEditorBuffer buffer(storage, "A\xc2\xa2Z", history);
    require(
        buffer.set_cursor(1) == veyra::TextEditResult::complete,
        "aliasing selection starts before the multibyte character"
    );
    require(
        buffer.set_cursor(3, true) == veyra::TextEditResult::complete,
        "aliasing selection covers the multibyte character"
    );

    std::size_t length = 99;
    require(
        buffer.copy_selection(history, length) == veyra::TextEditResult::overlapping_output,
        "selection copy rejects history storage as output"
    );
    require(length == 2, "history-overlapping selection copy reports its length");
    require(copied_text(buffer) == "A\xc2\xa2Z", "rejected selection copy leaves text unchanged");
    require(
        buffer.cursor() == 3 && buffer.selection().start == 1 && buffer.selection().length == 2,
        "rejected selection copy leaves cursor and selection alone"
    );

    history[0] = 'X';
    const std::string_view history_text(history.data(), 1);
    require(
        buffer.insert(history_text) == veyra::TextEditResult::overlapping_input,
        "insert rejects a source inside history storage"
    );
    require(copied_text(buffer) == "A\xc2\xa2Z", "history-sourced insert leaves text unchanged");
    require(
        buffer.cursor() == 3 && buffer.selection().start == 1 && buffer.selection().length == 2,
        "history-sourced insert leaves cursor and selection alone"
    );

    std::array<char, 8> composition_storage{};
    require(
        buffer.update_composition(composition_storage, history_text, 1, {}) ==
            veyra::TextEditResult::overlapping_input,
        "composition rejects preedit text inside history storage"
    );
    require(!buffer.has_composition(), "rejected preedit text leaves no live composition");
    require(
        copied_text(buffer) == "A\xc2\xa2Z",
        "rejected preedit text leaves committed text unchanged"
    );

    const auto committed_prefix = buffer.text_before_cursor().substr(0, 1);
    require(
        buffer.update_composition(composition_storage, committed_prefix, 1, {}) ==
            veyra::TextEditResult::overlapping_input,
        "composition rejects preedit text aliasing committed text"
    );
    require(!buffer.has_composition(), "aliased preedit text leaves no live composition");

    const auto history_scratch = std::span<char>(history).subspan(8, 8);
    require(
        buffer.update_composition(history_scratch, "ok", 2, {}) ==
            veyra::TextEditResult::overlapping_input,
        "composition rejects scratch storage inside history storage"
    );
    require(!buffer.has_composition(), "aliased scratch storage leaves no live composition");

    composition_storage[0] = 'o';
    composition_storage[1] = 'k';
    const std::string_view scratch_text(composition_storage.data(), 2);
    require(
        buffer.update_composition(composition_storage, scratch_text, 2, {}) ==
            veyra::TextEditResult::overlapping_input,
        "composition rejects preedit text inside its own scratch"
    );
    require(!buffer.has_composition(), "self-aliased preedit leaves no live composition");
    require(
        copied_text(buffer) == "A\xc2\xa2Z",
        "rejected composition leaves committed text unchanged"
    );
}

void check_cancel_without_composition_is_a_noop() {
    std::array<char, 16> storage{};
    veyra::TextEditorBuffer buffer(storage, "hello");
    require(
        buffer.set_cursor(2) == veyra::TextEditResult::complete,
        "cancel-noop cursor moves inside the text"
    );
    buffer.cancel_composition();
    require(!buffer.has_composition(), "cancel without preedit leaves no composition");
    require(copied_text(buffer) == "hello", "cancel without preedit leaves text unchanged");
    require(buffer.cursor() == 2, "cancel without preedit leaves the cursor alone");
    require(buffer.selection().length == 0, "cancel without preedit leaves the selection alone");
}

}

int main() {
    try {
        check_utf8_cursor_and_deletion();
        check_selection_replacement_and_capacity();
        check_selection_copy();
        check_undo_redo();
        check_undo_restores_selection_and_composition();
        check_undo_capacity_and_eviction();
        check_invalid_utf8_and_aliasing();
        check_composition_commit_and_cancel();
        check_composition_capacity_failure();
        check_utf8_character_offsets();
        check_utf8_encoding_boundaries();
        check_aliasing_guards_cover_both_spans();
        check_cancel_without_composition_is_a_noop();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
