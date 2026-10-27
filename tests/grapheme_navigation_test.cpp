#include "veyra/text/shaping/grapheme.hpp"

#include <array>
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

void check_combining_marks() {
    std::array<char, 32> storage{};
    veyra::TextEditorBuffer buffer(storage, "e\xcc\x81x");
    require(
        veyra::move_cursor_by_grapheme(buffer, veyra::TextCursorDirection::backward) ==
            veyra::TextEditResult::complete,
        "backward movement crosses the final grapheme"
    );
    require(buffer.cursor() == 3, "cursor stops after the combining sequence");
    require(
        veyra::move_cursor_by_grapheme(buffer, veyra::TextCursorDirection::backward) ==
            veyra::TextEditResult::complete,
        "backward movement crosses the combined character"
    );
    require(buffer.cursor() == 0, "base character and combining mark move together");
    require(
        veyra::move_cursor_by_grapheme(buffer, veyra::TextCursorDirection::forward) ==
            veyra::TextEditResult::complete,
        "forward movement crosses the combined character"
    );
    require(buffer.cursor() == 3, "forward movement stops after the combining mark");

    require(buffer.set_cursor(1) == veyra::TextEditResult::complete, "cursor enters the cluster");
    require(
        veyra::move_cursor_by_grapheme(buffer, veyra::TextCursorDirection::forward) ==
            veyra::TextEditResult::complete,
        "forward movement leaves an interior cursor at the next boundary"
    );
    require(buffer.cursor() == 3, "interior cursor skips the remaining combining mark");
}

void check_emoji_and_regional_indicators() {
    std::array<char, 32> emoji_storage{};
    veyra::TextEditorBuffer emoji(emoji_storage, "\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x92\xbbx");
    require(
        veyra::move_cursor_by_grapheme(emoji, veyra::TextCursorDirection::backward) ==
            veyra::TextEditResult::complete,
        "emoji sequence can be crossed backward"
    );
    require(emoji.cursor() == 11, "joined emoji stays in one grapheme");
    require(
        veyra::move_cursor_by_grapheme(emoji, veyra::TextCursorDirection::backward) ==
            veyra::TextEditResult::complete,
        "joined emoji moves as one unit"
    );
    require(emoji.cursor() == 0, "joined emoji begins at the first byte");
    require(
        veyra::move_cursor_by_grapheme(emoji, veyra::TextCursorDirection::forward) ==
            veyra::TextEditResult::complete,
        "joined emoji can be crossed forward"
    );
    require(emoji.cursor() == 11, "forward movement crosses the whole emoji sequence");

    std::array<char, 16> flag_storage{};
    veyra::TextEditorBuffer flag(flag_storage, "\xf0\x9f\x87\xb5\xf0\x9f\x87\xb9x");
    require(
        veyra::move_cursor_by_grapheme(flag, veyra::TextCursorDirection::backward) ==
            veyra::TextEditResult::complete,
        "regional indicator pair is separated from following text"
    );
    require(flag.cursor() == 8, "regional indicator pair remains one grapheme");
    require(
        veyra::move_cursor_by_grapheme(flag, veyra::TextCursorDirection::backward) ==
            veyra::TextEditResult::complete,
        "regional indicator pair moves as one unit"
    );
    require(flag.cursor() == 0, "regional indicator pair starts at the beginning");
}

void check_indic_conjunct_and_selection() {
    std::array<char, 24> storage{};
    veyra::TextEditorBuffer buffer(storage, "\xe0\xa4\x95\xe0\xa5\x8d\xe0\xa4\x95x");
    require(
        veyra::move_cursor_by_grapheme(buffer, veyra::TextCursorDirection::backward) ==
            veyra::TextEditResult::complete,
        "Indic conjunct is separated from following text"
    );
    require(buffer.cursor() == 9, "Indic consonant and linker sequence stays together");
    require(
        veyra::move_cursor_by_grapheme(buffer, veyra::TextCursorDirection::backward) ==
            veyra::TextEditResult::complete,
        "Indic conjunct moves as one grapheme"
    );
    require(buffer.cursor() == 0, "Indic conjunct starts at the beginning");

    require(buffer.set_cursor(0) == veyra::TextEditResult::complete, "cursor returns to start");
    require(
        buffer.set_cursor(9, true) == veyra::TextEditResult::complete,
        "selection covers the conjunct"
    );
    require(
        veyra::move_cursor_by_grapheme(buffer, veyra::TextCursorDirection::backward) ==
            veyra::TextEditResult::complete,
        "backward movement collapses an existing selection"
    );
    require(
        buffer.cursor() == 0 && buffer.selection().length == 0,
        "selection collapses to its start"
    );
    require(buffer.set_cursor(0) == veyra::TextEditResult::complete, "cursor returns to start");
    require(
        buffer.set_cursor(9, true) == veyra::TextEditResult::complete,
        "selection covers the conjunct again"
    );
    require(
        veyra::move_cursor_by_grapheme(buffer, veyra::TextCursorDirection::forward) ==
            veyra::TextEditResult::complete,
        "forward movement collapses an existing selection"
    );
    require(
        buffer.cursor() == 9 && buffer.selection().length == 0,
        "selection collapses to its end"
    );
}

void check_boundary_cases() {
    std::array<char, 8> storage{};
    veyra::TextEditorBuffer empty(storage, "");
    require(
        veyra::move_cursor_by_grapheme(empty, veyra::TextCursorDirection::forward) ==
            veyra::TextEditResult::complete,
        "empty text can move forward"
    );
    require(empty.cursor() == 0, "empty text keeps its cursor at the start");

    std::array<char, 8> composition_text_storage{};
    std::array<char, 8> composition_storage{};
    veyra::TextEditorBuffer composing(composition_text_storage, "x");
    require(composing.set_cursor(0) == veyra::TextEditResult::complete, "cursor moves to start");
    require(
        composing.update_composition(composition_storage, "preedit", 7, {}) ==
            veyra::TextEditResult::complete,
        "composition starts at the document boundary"
    );
    require(
        veyra::move_cursor_by_grapheme(composing, veyra::TextCursorDirection::backward) ==
            veyra::TextEditResult::complete,
        "backward movement at the boundary succeeds during composition"
    );
    require(composing.composition().text.empty(), "boundary movement cancels composition");

    std::array<char, 4> invalid_storage{};
    veyra::TextEditorBuffer invalid(invalid_storage, "\xff");
    require(
        veyra::move_cursor_by_grapheme(invalid, veyra::TextCursorDirection::forward) ==
            veyra::TextEditResult::invalid_utf8,
        "invalid UTF-8 state is propagated"
    );

    std::array<char, 8> crlf_storage{};
    veyra::TextEditorBuffer crlf(crlf_storage, "\r\nx");
    require(
        veyra::move_cursor_by_grapheme(crlf, veyra::TextCursorDirection::backward) ==
            veyra::TextEditResult::complete,
        "CRLF moves as one grapheme"
    );
    require(crlf.cursor() == 2, "CRLF is kept together");

    std::array<char, 20> indicators_storage{};
    veyra::TextEditorBuffer indicators(
        indicators_storage,
        "\xf0\x9f\x87\xb5\xf0\x9f\x87\xb9\xf0\x9f\x87\xb5"
    );
    require(
        veyra::move_cursor_by_grapheme(indicators, veyra::TextCursorDirection::backward) ==
            veyra::TextEditResult::complete,
        "an odd regional indicator run splits from its final pair"
    );
    require(indicators.cursor() == 8, "regional indicators pair from the start of the run");
}

void check_grapheme_deletion() {
    std::array<char, 16> backward_storage{};
    veyra::TextEditorBuffer backward(backward_storage, "e\xcc\x81x");
    require(
        veyra::erase_grapheme(backward, veyra::TextCursorDirection::backward) ==
            veyra::TextEditResult::complete,
        "backward deletion removes the previous grapheme"
    );
    require(backward.text_before_cursor() == "e\xcc\x81", "backward deletion keeps prior text");
    require(
        veyra::erase_grapheme(backward, veyra::TextCursorDirection::backward) ==
            veyra::TextEditResult::complete,
        "backward deletion removes a combining sequence"
    );
    require(backward.size() == 0, "the combining sequence is fully removed");

    std::array<char, 16> forward_storage{};
    veyra::TextEditorBuffer forward(forward_storage, "e\xcc\x81x");
    require(forward.set_cursor(0) == veyra::TextEditResult::complete, "cursor moves to start");
    require(
        veyra::erase_grapheme(forward, veyra::TextCursorDirection::forward) ==
            veyra::TextEditResult::complete,
        "forward deletion removes the next grapheme"
    );
    require(forward.text_after_cursor() == "x", "forward deletion keeps following text");

    std::array<char, 16> interior_storage{};
    veyra::TextEditorBuffer interior(interior_storage, "e\xcc\x81x");
    require(interior.set_cursor(1) == veyra::TextEditResult::complete, "cursor enters the cluster");
    require(
        veyra::erase_grapheme(interior, veyra::TextCursorDirection::backward) ==
            veyra::TextEditResult::complete,
        "deletion from inside a grapheme removes the full cluster"
    );
    require(interior.text_after_cursor() == "x", "interior deletion leaves valid UTF-8");

    std::array<char, 16> selection_storage{};
    veyra::TextEditorBuffer selected(selection_storage, "e\xcc\x81x");
    require(selected.set_cursor(0) == veyra::TextEditResult::complete, "selection starts at zero");
    require(
        selected.set_cursor(3, true) == veyra::TextEditResult::complete,
        "selection covers one grapheme"
    );
    require(
        veyra::erase_grapheme(selected, veyra::TextCursorDirection::forward) ==
            veyra::TextEditResult::complete,
        "grapheme deletion removes an existing selection"
    );
    require(selected.text_before_cursor().empty(), "selection deletion leaves the cursor at start");
    require(selected.text_after_cursor() == "x", "selection deletion preserves following text");

    std::array<char, 16> composition_text_storage{};
    std::array<char, 8> composition_storage{};
    veyra::TextEditorBuffer composing(composition_text_storage, "e\xcc\x81x");
    require(
        composing.update_composition(composition_storage, "preedit", 7, {}) ==
            veyra::TextEditResult::complete,
        "composition starts before grapheme deletion"
    );
    require(
        veyra::erase_grapheme(composing, veyra::TextCursorDirection::backward) ==
            veyra::TextEditResult::complete,
        "grapheme deletion cancels composition before editing"
    );
    require(composing.composition().text.empty(), "deletion clears active composition");
    require(composing.text_before_cursor() == "e\xcc\x81", "deletion edits committed text");
}

void check_grapheme_selection_extension() {
    std::array<char, 16> forward_storage{};
    veyra::TextEditorBuffer forward(forward_storage, "e\xcc\x81x");
    require(forward.set_cursor(0) == veyra::TextEditResult::complete, "forward selection starts");
    require(
        veyra::move_cursor_by_grapheme(forward, veyra::TextCursorDirection::forward, true) ==
            veyra::TextEditResult::complete,
        "forward grapheme movement extends the selection"
    );
    require(forward.cursor() == 3, "forward selection ends after the combining grapheme");
    require(
        forward.selection().start == 0 && forward.selection().length == 3,
        "forward selection covers a complete combining grapheme"
    );

    std::array<char, 16> backward_storage{};
    veyra::TextEditorBuffer backward(backward_storage, "e\xcc\x81x");
    require(backward.set_cursor(4) == veyra::TextEditResult::complete, "reverse selection starts");
    require(
        veyra::move_cursor_by_grapheme(backward, veyra::TextCursorDirection::backward, true) ==
            veyra::TextEditResult::complete,
        "backward grapheme movement extends the selection"
    );
    require(backward.cursor() == 3, "backward selection reaches the combining grapheme");
    require(
        veyra::move_cursor_by_grapheme(backward, veyra::TextCursorDirection::backward, true) ==
            veyra::TextEditResult::complete,
        "backward grapheme movement continues extending the selection"
    );
    require(backward.cursor() == 0, "backward selection reaches the start");
    require(
        backward.selection().start == 0 && backward.selection().length == 4,
        "backward selection keeps its original anchor"
    );
}

}

int main() {
    try {
        check_combining_marks();
        check_emoji_and_regional_indicators();
        check_indic_conjunct_and_selection();
        check_boundary_cases();
        check_grapheme_deletion();
        check_grapheme_selection_extension();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
