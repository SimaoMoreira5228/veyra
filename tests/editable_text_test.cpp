#include "veyra/behaviors/input/editable_text.hpp"
#include "veyra/platform/headless/null_window.hpp"

#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

#ifdef VEYRA_EDITABLE_TEXT_HAS_UNICODE
#include "veyra/text/shaping/grapheme.hpp"
#endif

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
    require(
        veyra::has_state(semantics.states, veyra::SemanticState::protected_value),
        "password semantics mark the value protected"
    );

    require(fix.field.set_reveal_password(true), "reveal is an explicit opt-in");
    require(fix.field.write_semantics("PIN", "1234", semantics), "revealed semantics write");
    require(semantics.value == "1234", "reveal exposes the caller snapshot only when asked");
    require(
        !veyra::has_state(semantics.states, veyra::SemanticState::protected_value),
        "revealing drops the protected state"
    );

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

void check_move_cursor_steps_code_points() {
    auto fix = Fixture{"a\xc3\xa9x"};
    require(
        fix.field.set_cursor(0).result == veyra::EditableTextResult::changed,
        "movement starts at the front of multibyte text"
    );
    require(
        fix.field.set_cursor(4).result == veyra::EditableTextResult::changed,
        "movement reaches the end of multibyte text"
    );
    require(
        fix.field.move_cursor(veyra::TextCursorDirection::backward).result ==
                veyra::EditableTextResult::changed &&
            fix.buffer.cursor() == 3,
        "backward movement crosses one code point"
    );
    require(
        fix.field.move_cursor(veyra::TextCursorDirection::backward).result ==
                veyra::EditableTextResult::changed &&
            fix.buffer.cursor() == 1,
        "backward movement crosses a two-byte code point in one step"
    );
    require(
        fix.field.move_cursor(veyra::TextCursorDirection::backward).result ==
                veyra::EditableTextResult::changed &&
            fix.buffer.cursor() == 0,
        "backward movement reaches the start"
    );
    const auto pinned = fix.field.move_cursor(veyra::TextCursorDirection::backward);
    require(
        pinned.result == veyra::EditableTextResult::handled &&
            pinned.edit == veyra::TextEditResult::complete && fix.buffer.cursor() == 0,
        "backward movement at the start reports no change"
    );
    require(
        fix.field.move_cursor(veyra::TextCursorDirection::forward).result ==
                veyra::EditableTextResult::changed &&
            fix.buffer.cursor() == 1,
        "forward movement crosses one code point"
    );
    require(
        fix.field.move_cursor(veyra::TextCursorDirection::forward, true).result ==
            veyra::EditableTextResult::changed,
        "forward movement can extend a selection"
    );
    require(
        fix.buffer.cursor() == 3 && fix.buffer.selection().start == 1 &&
            fix.buffer.selection().length == 2,
        "extended movement selects the whole multibyte code point"
    );
    require(
        fix.field.move_cursor(veyra::TextCursorDirection::backward).result ==
            veyra::EditableTextResult::changed,
        "movement without extend collapses a selection"
    );
    require(
        fix.buffer.cursor() == 1 && fix.buffer.selection().length == 0,
        "collapsed movement lands at the selection start"
    );

    const auto cursor_before_rejection = fix.buffer.cursor();
    const auto rejected = fix.field.move_cursor(static_cast<veyra::TextCursorDirection>(99));
    require(
        rejected.result == veyra::EditableTextResult::handled &&
            rejected.edit == veyra::TextEditResult::invalid_direction &&
            fix.buffer.cursor() == cursor_before_rejection,
        "an invalid direction reports no change and keeps the cursor"
    );

    require(
        fix.field.set_kind(veyra::EditableTextKind::search),
        "search kind tracks submit intent"
    );
    require(
        fix.field.set_focused(true) == veyra::EditableTextResult::handled,
        "intent checks need focus"
    );
    require(
        fix.field.move_cursor(veyra::TextCursorDirection::forward).result ==
            veyra::EditableTextResult::changed,
        "movement works while focused"
    );
    require(
        fix.field.submit_search() == veyra::EditableTextResult::handled,
        "caret moves never arm the search intent"
    );

    require(fix.field.set_disabled(true), "disabling reports a change");
    require(
        fix.field.move_cursor(veyra::TextCursorDirection::forward).result ==
            veyra::EditableTextResult::ignored,
        "movement is rejected while disabled"
    );
    require(fix.field.set_disabled(false), "enabling reports a change");
    require(fix.field.set_readonly(true), "readonly reports a change");
    require(
        fix.field.move_cursor(veyra::TextCursorDirection::forward).result ==
            veyra::EditableTextResult::changed,
        "movement still works while readonly"
    );
}

void check_move_to_line_edge_reports_visual_stops() {
    auto fix = Fixture{"word"};
    const std::array caret_stops{
        veyra::TextCaretStop{.byte_offset = 0, .x = 0.0F},
        veyra::TextCaretStop{.byte_offset = 1, .x = 12.0F},
        veyra::TextCaretStop{.byte_offset = 2, .x = 21.0F},
        veyra::TextCaretStop{.byte_offset = 3, .x = 31.0F},
        veyra::TextCaretStop{.byte_offset = 4, .x = 42.0F},
    };
    require(
        fix.field.set_cursor(2).result == veyra::EditableTextResult::changed,
        "edge movement starts inside the line"
    );

    auto selected_stop = std::size_t{99};
    require(
        fix.field.move_to_line_edge(veyra::TextVisualEdge::right, caret_stops, selected_stop)
                .result == veyra::EditableTextResult::changed,
        "edge movement reaches the rightmost caret stop"
    );
    require(fix.buffer.cursor() == 4 && selected_stop == 4, "right edge reports its visual stop");
    require(
        fix.field.move_to_line_edge(veyra::TextVisualEdge::left, caret_stops, selected_stop, true)
                .result == veyra::EditableTextResult::changed,
        "edge movement can extend a selection"
    );
    require(
        fix.buffer.cursor() == 0 && selected_stop == 0 && fix.buffer.selection().start == 0 &&
            fix.buffer.selection().length == 4,
        "extended edge movement selects through the left edge"
    );

    selected_stop = 99;
    const auto rejected =
        fix.field.move_to_line_edge(veyra::TextVisualEdge::left, {}, selected_stop);
    require(
        rejected.result == veyra::EditableTextResult::handled &&
            rejected.edit == veyra::TextEditResult::invalid_state && selected_stop == 99 &&
            fix.buffer.cursor() == 0,
        "an empty stop span reports no change and keeps its outputs"
    );

    const auto bad_direction = fix.field.move_to_line_edge(
        static_cast<veyra::TextVisualEdge>(99),
        caret_stops,
        selected_stop
    );
    require(
        bad_direction.result == veyra::EditableTextResult::handled &&
            bad_direction.edit == veyra::TextEditResult::invalid_direction &&
            fix.buffer.cursor() == 0,
        "an invalid edge reports no change and keeps the cursor"
    );

    require(fix.field.set_disabled(true), "disabling reports a change");
    require(
        fix.field.move_to_line_edge(veyra::TextVisualEdge::right, caret_stops, selected_stop)
                .result == veyra::EditableTextResult::ignored,
        "edge movement is rejected while disabled"
    );
}

void check_visual_order_composes_for_bidi() {
    auto fix = Fixture{"\xd7\x90\xd7\x91"};
    const std::array rtl_stops{
        veyra::TextCaretStop{.byte_offset = 4, .x = 0.0F},
        veyra::TextCaretStop{.byte_offset = 2, .x = 12.0F},
        veyra::TextCaretStop{.byte_offset = 0, .x = 24.0F},
    };

    auto selected_stop = std::size_t{99};
    require(
        fix.field.move_to_line_edge(veyra::TextVisualEdge::left, rtl_stops, selected_stop).result ==
            veyra::EditableTextResult::handled,
        "visual-left starts at the caller-ordered first stop"
    );
    require(
        fix.buffer.cursor() == 4 && selected_stop == 0,
        "visual-left is the logical end of a right-to-left line"
    );
    require(
        fix.field.move_to_line_edge(veyra::TextVisualEdge::right, rtl_stops, selected_stop)
                .result == veyra::EditableTextResult::changed,
        "visual-right reaches the caller-ordered last stop"
    );
    require(
        fix.buffer.cursor() == 0 && selected_stop == 2,
        "visual-right is the logical start of a right-to-left line"
    );
    require(
        fix.field.move_to_line_edge(veyra::TextVisualEdge::left, rtl_stops, selected_stop, true)
                .result == veyra::EditableTextResult::changed,
        "visual edge movement can extend across the Hebrew run"
    );
    require(
        fix.buffer.selection().start == 0 && fix.buffer.selection().length == 4,
        "extended visual movement selects the whole run"
    );
}

void check_move_vertically_threads_visual_positions() {
    auto fix = Fixture{"one\ntwo"};
    const std::array first_line_stops{
        veyra::TextCaretStop{.byte_offset = 0, .x = 0.0F},
        veyra::TextCaretStop{.byte_offset = 1, .x = 10.0F},
        veyra::TextCaretStop{.byte_offset = 2, .x = 20.0F},
        veyra::TextCaretStop{.byte_offset = 3, .x = 30.0F},
    };
    const std::array second_line_stops{
        veyra::TextCaretStop{.byte_offset = 4, .x = 0.0F},
        veyra::TextCaretStop{.byte_offset = 5, .x = 10.0F},
        veyra::TextCaretStop{.byte_offset = 6, .x = 20.0F},
        veyra::TextCaretStop{.byte_offset = 7, .x = 30.0F},
    };
    const std::array lines{
        veyra::TextVisualLine{.caret_stops = first_line_stops},
        veyra::TextVisualLine{.caret_stops = second_line_stops},
    };
    require(
        fix.field.set_cursor(2).result == veyra::EditableTextResult::changed,
        "vertical movement starts on the first line"
    );

    auto position = veyra::TextVisualPosition{.line_index = 0, .stop_index = 2};
    auto next_position = veyra::TextVisualPosition{.line_index = 99, .stop_index = 99};
    require(
        fix.field
                .move_vertically(
                    lines,
                    position,
                    veyra::TextVerticalDirection::down,
                    19.0F,
                    next_position,
                    true
                )
                .result == veyra::EditableTextResult::changed,
        "Down moves to the nearest caret on the next visual line"
    );
    require(
        next_position.line_index == 1 && next_position.stop_index == 2 && fix.buffer.cursor() == 6,
        "Down reports the selected visual position"
    );
    require(
        fix.buffer.selection().start == 2 && fix.buffer.selection().length == 4,
        "Down can extend the current selection"
    );

    position = next_position;
    require(
        fix.field
                .move_vertically(
                    lines,
                    position,
                    veyra::TextVerticalDirection::up,
                    19.0F,
                    next_position
                )
                .result == veyra::EditableTextResult::changed,
        "Up returns to the preceding visual line"
    );
    require(
        next_position.line_index == 0 && next_position.stop_index == 2 && fix.buffer.cursor() == 2,
        "Up keeps the preferred x position"
    );

    const auto stale_output = veyra::TextVisualPosition{.line_index = 77, .stop_index = 77};
    auto stale_next = stale_output;
    const auto cursor_before_stale = fix.buffer.cursor();
    require(
        fix.field
                .move_vertically(
                    lines,
                    {.line_index = 0, .stop_index = 1},
                    veyra::TextVerticalDirection::down,
                    19.0F,
                    stale_next
                )
                .result == veyra::EditableTextResult::handled,
        "a stale visual position reports no change"
    );
    require(
        fix.buffer.cursor() == cursor_before_stale &&
            stale_next.line_index == stale_output.line_index &&
            stale_next.stop_index == stale_output.stop_index,
        "a stale visual position keeps the cursor and its outputs"
    );
}

void check_pointer_drag_selects_whole_code_points() {
    auto fix = Fixture{"a\xc3\xa9x"};
    require(
        fix.field.set_cursor(0).result == veyra::EditableTextResult::changed,
        "press lands the caret"
    );
    require(
        fix.field.set_cursor(1, true).result == veyra::EditableTextResult::changed,
        "drag reaches the second code point"
    );
    require(
        fix.buffer.selection().start == 0 && fix.buffer.selection().length == 1,
        "drag covers the first code point"
    );
    require(
        fix.field.set_cursor(2, true).result == veyra::EditableTextResult::handled,
        "dragging into the middle of a code point reports no change"
    );
    require(
        fix.buffer.selection().start == 0 && fix.buffer.selection().length == 1,
        "a mid-code-point drag target leaves the selection alone"
    );
    require(
        fix.field.set_cursor(3, true).result == veyra::EditableTextResult::changed,
        "drag crosses the multibyte code point"
    );
    require(
        fix.buffer.selection().start == 0 && fix.buffer.selection().length == 3,
        "drag covers whole code points, never half of one"
    );
    require(
        fix.field.set_cursor(4, true).result == veyra::EditableTextResult::changed,
        "release covers the drag span"
    );
    require(
        fix.buffer.selection().start == 0 && fix.buffer.selection().length == 4,
        "the released selection spans the dragged text"
    );
}

void check_clipboard_recipe_through_null_window() {
    auto source = Fixture{"hello"};
    source.field.select_all();

    std::array<char, 256> copied{};
    std::size_t copied_length = 0;
    require(
        source.field.copy_selection(copied, copied_length) == veyra::TextEditResult::complete &&
            copied_length == 5,
        "copy reads the selection into caller storage"
    );
    copied[copied_length] = '\0';

    veyra::NullWindow window("clipboard", 320, 240, 7);
    require(window.creation_result() == veyra::WindowResult::ready, "null window is ready");
    require(
        window.set_clipboard_text(copied.data()) == veyra::WindowResult::ready,
        "copied text reaches the window clipboard"
    );

    std::array<char, 256> retrieved{};
    std::size_t retrieved_length = 0;
    require(
        window.get_clipboard_text(retrieved, retrieved_length) == veyra::WindowResult::ready &&
            retrieved_length == 5,
        "pasted text leaves the window clipboard"
    );

    auto target = Fixture{};
    require(
        target.field.paste({retrieved.data(), retrieved_length}).result ==
            veyra::EditableTextResult::changed,
        "paste inserts the clipboard bytes"
    );
    std::array<char, 16> flattened{};
    std::size_t flattened_length = 0;
    require(
        target.field.buffer().copy_text(flattened, flattened_length) ==
                veyra::TextEditResult::complete &&
            flattened_length == 5,
        "the target holds the pasted bytes"
    );
    require(
        std::string_view{flattened.data(), flattened_length} == "hello",
        "the clipboard round trip preserves the text"
    );

    std::array<char, 256> cut{};
    std::size_t cut_length = 0;
    const auto removed = source.field.cut_selection(cut, cut_length);
    require(
        removed.result == veyra::EditableTextResult::changed &&
            removed.edit == veyra::TextEditResult::complete && cut_length == 5 &&
            source.field.buffer().size() == 0,
        "cut copies the selection, then deletes it"
    );
    cut[cut_length] = '\0';
    require(
        window.set_clipboard_text(cut.data()) == veyra::WindowResult::ready,
        "cut text reaches the window clipboard"
    );
    require(
        window.get_clipboard_text(retrieved, retrieved_length) == veyra::WindowResult::ready &&
            retrieved_length == 5,
        "cut text leaves the window clipboard"
    );
    require(
        target.field.paste({retrieved.data(), retrieved_length}).result ==
            veyra::EditableTextResult::changed,
        "cut text pastes into the target"
    );
    require(
        target.field.buffer().copy_text(flattened, flattened_length) ==
                veyra::TextEditResult::complete &&
            std::string_view{flattened.data(), flattened_length} == "hellohello",
        "cut text appends after the earlier paste"
    );
}

#ifdef VEYRA_EDITABLE_TEXT_HAS_UNICODE
void check_grapheme_steps_compose_through_buffer() {
    auto fix = Fixture{"e\xcc\x81x"};
    require(
        veyra::move_cursor_by_grapheme(fix.field.buffer(), veyra::TextCursorDirection::backward) ==
            veyra::TextEditResult::complete,
        "grapheme movement crosses the trailing code point"
    );
    require(fix.field.buffer().cursor() == 3, "cursor stops after the combining sequence");
    require(
        veyra::move_cursor_by_grapheme(fix.field.buffer(), veyra::TextCursorDirection::backward) ==
            veyra::TextEditResult::complete,
        "grapheme movement crosses the combined character"
    );
    require(fix.field.buffer().cursor() == 0, "base character and combining mark move together");

    auto emoji = Fixture{"\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x92\xbbx"};
    require(
        veyra::move_cursor_by_grapheme(
            emoji.field.buffer(),
            veyra::TextCursorDirection::backward
        ) == veyra::TextEditResult::complete,
        "joined emoji separates from following text"
    );
    require(emoji.field.buffer().cursor() == 11, "joined emoji stays in one grapheme");
    require(
        veyra::move_cursor_by_grapheme(
            emoji.field.buffer(),
            veyra::TextCursorDirection::backward
        ) == veyra::TextEditResult::complete,
        "joined emoji moves as one unit"
    );
    require(emoji.field.buffer().cursor() == 0, "joined emoji begins at the first byte");

    require(fix.field.buffer().cursor() == 0, "grapheme drag anchors at the start");
    require(
        veyra::move_cursor_by_grapheme(
            fix.field.buffer(),
            veyra::TextCursorDirection::forward,
            true
        ) == veyra::TextEditResult::complete,
        "grapheme drag extends forward"
    );
    require(
        fix.field.buffer().selection().start == 0 && fix.field.buffer().selection().length == 3,
        "grapheme drag covers the whole cluster, never half"
    );
}
#endif

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
        check_move_cursor_steps_code_points();
        check_move_to_line_edge_reports_visual_stops();
        check_visual_order_composes_for_bidi();
        check_move_vertically_threads_visual_positions();
        check_pointer_drag_selects_whole_code_points();
        check_clipboard_recipe_through_null_window();
#ifdef VEYRA_EDITABLE_TEXT_HAS_UNICODE
        check_grapheme_steps_compose_through_buffer();
#endif
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
