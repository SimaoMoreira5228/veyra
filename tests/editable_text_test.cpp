#include "veyra/behaviors/editable_text.hpp"

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
    veyra::TextEditorBuffer buffer;
    veyra::EditableTextController field;

    explicit Fixture(std::string_view initial = {}) :
        buffer(std::span<char>{storage}, initial), field(buffer) {
    }
};

void check_readonly_rejects_edits_allows_reads() {
    auto fix = Fixture{"hello"};
    require(fix.field.set_readonly(true), "entering readonly reports a change");
    require(!fix.field.set_readonly(true), "repeating readonly reports no change");

    require(
        fix.field.insert("!").result == veyra::EditableTextResult::ignored,
        "insert is rejected while readonly"
    );
    require(
        fix.field.paste("!").result == veyra::EditableTextResult::ignored,
        "paste is rejected while readonly"
    );
    require(
        fix.field.erase_backward().result == veyra::EditableTextResult::ignored,
        "erase is rejected while readonly"
    );
    require(
        fix.field.clear().result == veyra::EditableTextResult::ignored,
        "clear is rejected while readonly"
    );
    require(
        fix.field.update_composition(fix.preedit, "x", 1, {}).result ==
            veyra::EditableTextResult::ignored,
        "composition updates are rejected while readonly"
    );

    std::array<char, 16> output{};
    std::size_t length = 0;
    fix.field.select_all();
    require(
        fix.field.copy_selection(output, length) == veyra::TextEditResult::complete && length == 5,
        "copy still works while readonly"
    );
    require(
        fix.field.select_all() == veyra::EditableTextResult::handled,
        "selecting an already-selected span reports no change"
    );
    require(
        fix.field.cut_selection(output, length).result == veyra::EditableTextResult::ignored,
        "cut is rejected while readonly"
    );
    require(
        fix.field.set_cursor(1).result == veyra::EditableTextResult::changed,
        "caret movement still works while readonly"
    );

    auto semantics = veyra::SemanticNodeData{};
    require(fix.field.write_semantics("Name", "hello", semantics), "semantics still write");
    require(
        veyra::has_state(semantics.states, veyra::SemanticState::read_only) &&
            !veyra::has_action(semantics.actions, veyra::SemanticAction::set_value),
        "readonly semantics expose the state and drop set_value"
    );
}

void check_disabled_rejects_edits_clears_focus() {
    auto fix = Fixture{"hello"};
    require(
        fix.field.set_focused(true) == veyra::EditableTextResult::handled,
        "focus can be reflected into the controller"
    );
    require(
        fix.field.insert("!").result == veyra::EditableTextResult::changed,
        "insert works while enabled"
    );

    require(fix.field.set_disabled(true), "disabling reports a change");
    require(!fix.field.focused(), "disabling drops focus");
    require(
        fix.field.insert("?").result == veyra::EditableTextResult::ignored,
        "insert is rejected while disabled"
    );
    require(
        fix.field.erase_forward().result == veyra::EditableTextResult::ignored,
        "erase is rejected while disabled"
    );
    require(
        fix.field.select_all() == veyra::EditableTextResult::ignored,
        "select-all is rejected while disabled"
    );
    require(
        fix.field.set_focused(true) == veyra::EditableTextResult::ignored,
        "focusing a disabled field is rejected"
    );
    require(
        fix.field.set_cursor(1).result == veyra::EditableTextResult::ignored,
        "caret movement is rejected while disabled"
    );

    std::array<char, 16> output{};
    std::size_t length = 0;
    require(
        fix.field.copy_selection(output, length) == veyra::TextEditResult::complete,
        "copy is a read and still works while disabled"
    );

    auto semantics = veyra::SemanticNodeData{};
    require(fix.field.write_semantics("Name", "hello!", semantics), "semantics still write");
    require(
        veyra::has_state(semantics.states, veyra::SemanticState::disabled) &&
            semantics.actions == 0,
        "disabled semantics expose the state and drop every action"
    );

    require(fix.field.set_disabled(false), "enabling reports a change");
    require(
        fix.field.set_focused(true) == veyra::EditableTextResult::handled,
        "focusing works again once enabled"
    );
}

void check_invalid_flag_independent_of_ime() {
    auto fix = Fixture{"abc"};
    fix.field.set_focused(true);
    fix.field.select_all();

    const auto update =
        fix.field.update_composition(fix.preedit, "X", 1, {.start = 0, .length = 0});
    require(
        update.result == veyra::EditableTextResult::handled &&
            update.edit == veyra::TextEditResult::complete,
        "a preedit update lands without touching committed text"
    );
    require(fix.field.composing(), "the controller reports the live preedit");

    require(fix.field.set_invalid(true), "invalid can be flagged mid-composition");
    require(fix.field.composing(), "flagging invalid leaves the preedit alone");
    require(!fix.field.set_invalid(true), "repeating invalid reports no change");

    auto flagged = veyra::SemanticNodeData{};
    require(fix.field.write_semantics("Name", "abc", flagged), "flagged semantics write");
    require(
        veyra::has_state(flagged.states, veyra::SemanticState::invalid),
        "flagged semantics expose the invalid state"
    );

    const auto commit = fix.field.commit_composition();
    require(
        commit.result == veyra::EditableTextResult::changed && !fix.field.composing(),
        "commit applies the preedit after validation was flagged"
    );
    require(fix.field.invalid(), "commit never clears the app-owned invalid flag");
    require(fix.buffer.size() == 1, "the committed preedit replaced the selection");
}

void check_composition_rejected_unfocused() {
    auto fix = Fixture{"abc"};

    require(
        fix.field.update_composition(fix.preedit, "X", 1, {}).result ==
            veyra::EditableTextResult::ignored,
        "composition updates need focus"
    );
    require(
        fix.field.commit_composition().result == veyra::EditableTextResult::ignored,
        "composition commits need focus"
    );
    require(
        fix.field.cancel_composition() == veyra::EditableTextResult::handled,
        "cancelling nothing reports no change"
    );

    fix.field.set_focused(true);
    require(
        fix.field.commit_composition().result == veyra::EditableTextResult::handled,
        "committing nothing reports no change"
    );
    fix.field.update_composition(fix.preedit, "X", 1, {});
    require(
        fix.field.cancel_composition() == veyra::EditableTextResult::changed,
        "cancelling a live preedit reports a change"
    );
    require(!fix.field.composing(), "cancellation drops the preedit");

    std::array<char, 1> cramped{};
    const auto starved =
        fix.field.update_composition(cramped, "toolong", 7, {.start = 0, .length = 0});
    require(
        starved.result == veyra::EditableTextResult::handled &&
            starved.edit == veyra::TextEditResult::insufficient_capacity && !fix.field.composing(),
        "a preedit larger than its storage reports no change and stores nothing"
    );

    fix.field.update_composition(fix.preedit, "X", 1, {});
    require(
        fix.field.set_focused(false) == veyra::EditableTextResult::handled,
        "focus loss updates controller state"
    );
    require(!fix.field.composing(), "focus loss cancels the live preedit");
}

void check_password_semantics_suppression() {
    auto fix = Fixture{};
    require(
        fix.field.configure({.kind = veyra::EditableTextKind::password}),
        "a field can be configured as a password"
    );
    require(!fix.field.configure({.kind = veyra::EditableTextKind::password}), "same kind dup");

    auto semantics = veyra::SemanticNodeData{};
    require(fix.field.write_semantics("PIN", "1234", semantics), "password semantics write");
    require(
        semantics.role == veyra::SemanticRole::text_input && semantics.label == "PIN" &&
            semantics.value.empty(),
        "password semantics suppress the value by default"
    );

    require(fix.field.set_reveal_password(true), "reveal is an explicit opt-in");
    require(fix.field.write_semantics("PIN", "1234", semantics), "revealed semantics write");
    require(semantics.value == "1234", "reveal exposes the caller snapshot only when asked");

    require(fix.field.set_kind(veyra::EditableTextKind::text), "kind can switch back");
    require(!fix.field.reveal_password(), "leaving password kind drops the reveal opt-in");
    require(fix.field.write_semantics("Name", "ada", semantics), "plain semantics write");
    require(semantics.value == "ada", "plain text exposes the caller snapshot");
}

void check_search_clear_and_intent_exactly_once() {
    auto fix = Fixture{};
    fix.field.set_kind(veyra::EditableTextKind::search);

    require(
        fix.field.submit_search() == veyra::EditableTextResult::ignored,
        "search submit needs focus"
    );
    fix.field.set_focused(true);
    require(
        fix.field.submit_search() == veyra::EditableTextResult::handled,
        "submit with no edits reports no change"
    );

    require(
        fix.field.insert("query").result == veyra::EditableTextResult::changed,
        "typing arms the search intent"
    );
    require(
        fix.field.submit_search() == veyra::EditableTextResult::changed,
        "the first submit after an edit reports the intent"
    );
    require(
        fix.field.submit_search() == veyra::EditableTextResult::handled,
        "the intent fires exactly once per edit"
    );

    require(
        fix.field.set_cursor(0).result == veyra::EditableTextResult::changed,
        "caret moves report their own change"
    );
    require(
        fix.field.submit_search() == veyra::EditableTextResult::handled,
        "caret moves never re-arm the search intent"
    );

    const auto cleared = fix.field.clear();
    require(
        cleared.result == veyra::EditableTextResult::changed && fix.buffer.size() == 0,
        "clear empties the field for the search clear-action"
    );
    require(
        fix.field.clear().result == veyra::EditableTextResult::handled,
        "clearing an empty field reports no change"
    );

    auto plain = Fixture{};
    plain.field.set_focused(true);
    plain.field.insert("query");
    require(
        plain.field.submit_search() == veyra::EditableTextResult::ignored,
        "plain text fields never report search intent"
    );
}

void check_commit_then_validate_ordering() {
    auto fix = Fixture{"helo"};
    fix.field.set_focused(true);
    fix.field.select_all();

    fix.field.update_composition(fix.preedit, "hello", 5, {.start = 0, .length = 0});
    const auto commit = fix.field.commit_composition();
    require(
        commit.result == veyra::EditableTextResult::changed && fix.buffer.size() == 5,
        "commit lands the preedit as committed text"
    );

    std::array<char, 16> committed{};
    std::size_t length = 0;
    require(
        fix.buffer.copy_text(committed, length) == veyra::TextEditResult::complete && length == 5,
        "the app can read the committed text"
    );
    const bool typo_gone = length == 5 && committed[4] == 'o';
    require(fix.field.set_invalid(true), "a stale flag can stand while typing");
    require(fix.field.set_invalid(!typo_gone), "validation runs on committed text, after commit");
    require(!fix.field.invalid(), "a fixed word clears the flag");
}

void check_empty_name_semantics_rejection() {
    auto fix = Fixture{"hello"};
    fix.field.set_focused(true);

    auto semantics = veyra::SemanticNodeData{
        .role = veyra::SemanticRole::text,
        .label = "unchanged",
    };
    require(!fix.field.write_semantics({}, "hello", semantics), "an empty name is rejected");
    require(
        semantics.role == veyra::SemanticRole::text && semantics.label == "unchanged",
        "invalid semantic output stays unchanged"
    );

    require(fix.field.write_semantics("Name", "hello", semantics), "a named field writes");
    require(
        semantics.role == veyra::SemanticRole::text_input && semantics.label == "Name" &&
            semantics.value == "hello" &&
            veyra::has_action(semantics.actions, veyra::SemanticAction::set_value),
        "plain semantics expose the value and set_value"
    );

    fix.field.set_kind(veyra::EditableTextKind::search);
    require(fix.field.write_semantics("Find", "q", semantics), "search semantics write");
    require(
        veyra::has_action(semantics.actions, veyra::SemanticAction::set_value) &&
            veyra::has_action(semantics.actions, veyra::SemanticAction::dismiss),
        "search semantics add dismiss for the clear-action"
    );
}

void check_paste_and_erase_apply_edits() {
    auto fix = Fixture{"hello"};
    require(
        fix.field.paste("!!").result == veyra::EditableTextResult::changed &&
            fix.buffer.size() == 7,
        "paste inserts at the caret"
    );
    require(
        fix.field.erase_backward().result == veyra::EditableTextResult::changed &&
            fix.buffer.size() == 6,
        "erase-backward deletes behind the caret"
    );
    require(
        fix.field.erase_forward().result == veyra::EditableTextResult::handled &&
            fix.buffer.size() == 6,
        "erase-forward at the end of text reports no change"
    );

    require(
        fix.field.set_cursor(0).result == veyra::EditableTextResult::changed,
        "caret moves to the front"
    );
    require(
        fix.field.erase_forward().result == veyra::EditableTextResult::changed &&
            fix.buffer.size() == 5,
        "erase-forward deletes ahead of the caret"
    );
}

void check_platform_commit_flow() {
    auto fix = Fixture{"hi"};
    require(
        fix.field.set_focused(true) == veyra::EditableTextResult::handled,
        "platform input needs focus"
    );

    require(
        fix.field.commit_text("!").result == veyra::EditableTextResult::changed &&
            fix.buffer.size() == 3,
        "direct commits insert without a preedit"
    );

    const auto preview = fix.field.update_composition(fix.preedit, "x", 1, {});
    require(
        preview.result == veyra::EditableTextResult::handled &&
            preview.edit == veyra::TextEditResult::complete,
        "preedit previews without touching committed text"
    );
    require(fix.field.composing(), "preedit marks the field composing");

    require(
        fix.field.commit_text("x").result == veyra::EditableTextResult::changed &&
            fix.buffer.size() == 4,
        "platform confirmation replaces the preedit once"
    );
    require(!fix.field.composing(), "confirmed preedit clears");

    fix.field.select_all();

    std::array<char, 16> output{};
    std::size_t length = 0;

    require(
        fix.field.copy_selection(output, length) == veyra::TextEditResult::complete,
        "confirmed text copies out"
    );
    require(std::string_view(output.data(), length) == "hi!x", "preedit text lands exactly once");

    require(fix.field.set_focused(false) == veyra::EditableTextResult::handled, "blur drops focus");
    require(
        fix.field.commit_text("?").result == veyra::EditableTextResult::ignored &&
            fix.buffer.size() == 4,
        "commits are ignored while blurred"
    );

    require(fix.field.set_disabled(true), "disabling reports a change");
    require(
        fix.field.commit_text("?").result == veyra::EditableTextResult::ignored &&
            fix.buffer.size() == 4,
        "commits are ignored while disabled"
    );

    auto locked = Fixture{"hi"};
    require(
        locked.field.set_focused(true) == veyra::EditableTextResult::handled,
        "locked field focuses"
    );
    require(locked.field.set_readonly(true), "locked field turns readonly");
    require(
        locked.field.commit_text("?").result == veyra::EditableTextResult::ignored &&
            locked.buffer.size() == 2,
        "commits are ignored while readonly"
    );

    auto invalid = Fixture{};
    require(
        invalid.field.set_focused(true) == veyra::EditableTextResult::handled,
        "fresh field focuses"
    );
    require(
        invalid.field.commit_text("\xff").result == veyra::EditableTextResult::handled,
        "malformed commits report landing without applying"
    );
    require(invalid.buffer.size() == 0, "malformed commits change nothing");
}

void check_cut_copies_then_deletes() {
    auto fix = Fixture{"hello"};
    fix.field.select_all();

    std::array<char, 16> output{};
    std::size_t length = 0;
    const auto cut = fix.field.cut_selection(output, length);
    require(
        cut.result == veyra::EditableTextResult::changed &&
            cut.edit == veyra::TextEditResult::complete && length == 5,
        "cut reports the copied byte count"
    );
    require(
        std::string_view{output.data(), length} == "hello" && fix.buffer.size() == 0,
        "cut copies the selection into the caller buffer, then deletes it"
    );

    length = 0;
    require(
        fix.field.cut_selection(output, length).result == veyra::EditableTextResult::handled,
        "cutting an empty selection reports no change"
    );

    auto cramped = Fixture{"hello"};
    cramped.field.select_all();
    std::array<char, 2> small{};
    std::size_t small_length = 0;
    const auto truncated = cramped.field.cut_selection(small, small_length);
    require(
        truncated.result == veyra::EditableTextResult::handled &&
            truncated.edit == veyra::TextEditResult::insufficient_capacity,
        "cut into a short buffer reports no change with the capacity error"
    );
    require(cramped.buffer.size() == 5, "a failed cut keeps the text");
}

}

int main() {
    try {
        check_readonly_rejects_edits_allows_reads();
        check_disabled_rejects_edits_clears_focus();
        check_invalid_flag_independent_of_ime();
        check_composition_rejected_unfocused();
        check_password_semantics_suppression();
        check_search_clear_and_intent_exactly_once();
        check_commit_then_validate_ordering();
        check_empty_name_semantics_rejection();
        check_paste_and_erase_apply_edits();
        check_platform_commit_flow();
        check_cut_copies_then_deletes();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
