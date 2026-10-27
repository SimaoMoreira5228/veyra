#include "veyra/behaviors/input/editable_text.hpp"

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

struct Fixture {
    std::array<char, 256> storage{};
    std::array<char, 64> preedit{};
    std::array<char, 256> history{};
    veyra::TextEditorBuffer buffer;
    veyra::EditableTextController field;

    explicit Fixture(std::string_view initial = {}) :
        buffer(std::span<char>{storage}, initial, std::span<char>{history}), field(buffer) {
    }
};

std::string snapshot_text(const veyra::TextEditorBuffer& buffer, std::array<char, 256>& scratch) {
    auto length = std::size_t{0};

    require(
        buffer.copy_text(scratch, length) == veyra::TextEditResult::complete,
        "textarea text copies into caller scratch"
    );

    return {scratch.data(), length};
}

void check_multiline_insert_lands_through_controller() {
    auto fix = Fixture{};

    require(
        fix.field.set_focused(true) == veyra::EditableTextResult::handled,
        "textarea takes focus"
    );

    const auto edit = fix.field.insert("one\ntwo\nthree");

    require(
        edit.result == veyra::EditableTextResult::changed &&
            edit.edit == veyra::TextEditResult::complete,
        "a newline-bearing insert lands as one edit"
    );

    auto scratch = std::array<char, 256>{};

    require(
        snapshot_text(fix.buffer, scratch) == "one\ntwo\nthree",
        "the insert holds two hard lines"
    );
    require(fix.buffer.cursor() == 13, "the caret rests past the final line");

    require(
        fix.field.set_cursor(5).edit == veyra::TextEditResult::complete,
        "caret lands mid second line through the controller"
    );
    require(
        fix.buffer.move_cursor_to_line_start() == veyra::TextEditResult::complete,
        "Home reaches the hard line start"
    );
    require(fix.buffer.cursor() == 4, "Home stops past the first newline");
    require(
        fix.buffer.move_cursor_to_line_end() == veyra::TextEditResult::complete,
        "End reaches the hard line end"
    );
    require(fix.buffer.cursor() == 7, "End stops before the second newline");
}

void check_wrapped_home_end_survives_an_edit() {
    auto fix = Fixture{"abcdef"};

    const std::array first_shard{
        veyra::TextCaretStop{.byte_offset = 0, .x = 0.0F},
        veyra::TextCaretStop{.byte_offset = 1, .x = 8.0F},
        veyra::TextCaretStop{.byte_offset = 2, .x = 16.0F},
        veyra::TextCaretStop{.byte_offset = 3, .x = 24.0F},
        veyra::TextCaretStop{.byte_offset = 4, .x = 32.0F},
    };

    auto selected_stop = std::size_t{99};

    require(
        fix.buffer.move_cursor_to_visual_edge(
            first_shard,
            veyra::TextVisualEdge::right,
            selected_stop
        ) == veyra::TextEditResult::complete,
        "wrapped End reaches the first visual line edge"
    );
    require(fix.buffer.cursor() == 4 && selected_stop == 4, "End reports its caret stop");

    const auto edit = fix.field.insert("X");

    require(
        edit.result == veyra::EditableTextResult::changed,
        "typing at the visual edge lands through the controller"
    );

    auto scratch = std::array<char, 256>{};

    require(snapshot_text(fix.buffer, scratch) == "abcdXef", "the edit splits the wrapped line");

    const std::array rebuilt_tail{
        veyra::TextCaretStop{.byte_offset = 5, .x = 0.0F},
        veyra::TextCaretStop{.byte_offset = 6, .x = 8.0F},
        veyra::TextCaretStop{.byte_offset = 7, .x = 16.0F},
    };

    require(
        fix.buffer.move_cursor_to_visual_edge(
            rebuilt_tail,
            veyra::TextVisualEdge::left,
            selected_stop
        ) == veyra::TextEditResult::complete,
        "wrapped Home reaches the rebuilt second line"
    );
    require(fix.buffer.cursor() == 5 && selected_stop == 0, "Home reports the rebuilt stop");
}

void check_scroll_anchor_holds_across_an_edit() {
    auto fix = Fixture{"aaaa\nbbbb\ncccc"};

    auto anchor = std::size_t{10};

    auto scratch = std::array<char, 256>{};

    require(
        snapshot_text(fix.buffer, scratch).substr(anchor, 4) == "cccc",
        "the anchor starts on the third line"
    );

    require(
        fix.field.set_cursor(0).edit == veyra::TextEditResult::complete,
        "caret moves to the document head"
    );

    const auto inserted = fix.field.insert(">>>");

    require(
        inserted.result == veyra::EditableTextResult::changed,
        "a head insert lands through the controller"
    );

    anchor += 3;

    require(
        snapshot_text(fix.buffer, scratch).substr(anchor, 4) == "cccc",
        "the shifted anchor still names the third line"
    );

    require(
        fix.field.set_cursor(0).edit == veyra::TextEditResult::complete,
        "caret returns to the document head"
    );
    require(
        fix.field.set_cursor(3, true).edit == veyra::TextEditResult::complete,
        "the inserted prefix selects cleanly"
    );

    const auto deleted = fix.field.erase_backward();

    require(
        deleted.result == veyra::EditableTextResult::changed,
        "the prefix deletes through the controller"
    );

    anchor -= 3;

    require(
        snapshot_text(fix.buffer, scratch) == "aaaa\nbbbb\ncccc",
        "the document returns to its anchored shape"
    );
    require(
        snapshot_text(fix.buffer, scratch).substr(anchor, 4) == "cccc",
        "the restored anchor still names the third line"
    );
}

void check_ime_commits_newlines_as_one_step() {
    auto fix = Fixture{"aa\nbb\ncc"};

    require(
        fix.field.set_focused(true) == veyra::EditableTextResult::handled,
        "multiline field takes focus"
    );
    require(
        fix.field.set_cursor(3).edit == veyra::TextEditResult::complete,
        "ime range starts on the second line"
    );
    require(
        fix.field.set_cursor(5, true).edit == veyra::TextEditResult::complete,
        "ime range covers the second line"
    );

    const auto marked = fix.field.update_composition(fix.preedit, "X\nY", 3, {});

    require(
        marked.result == veyra::EditableTextResult::handled &&
            marked.edit == veyra::TextEditResult::complete,
        "a newline-bearing preedit touches no committed text"
    );
    require(fix.field.composing(), "the preedit opens a session inside the document");

    const auto commit = fix.field.commit_composition();

    require(
        commit.result == veyra::EditableTextResult::changed && !fix.field.composing(),
        "commit lands the multiline preedit through the controller"
    );

    auto scratch = std::array<char, 256>{};

    require(
        snapshot_text(fix.buffer, scratch) == "aa\nX\nY\ncc",
        "commit replaces the marked line with two lines"
    );
    require(fix.buffer.undo() == veyra::TextEditResult::complete, "the multiline commit undoes");
    require(
        snapshot_text(fix.buffer, scratch) == "aa\nbb\ncc",
        "one undo removes the whole multiline commit"
    );
}

}

int main() {
    try {
        check_multiline_insert_lands_through_controller();
        check_wrapped_home_end_survives_an_edit();
        check_scroll_anchor_holds_across_an_edit();
        check_ime_commits_newlines_as_one_step();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
