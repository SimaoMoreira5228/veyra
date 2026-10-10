#include "veyra/behaviors/editable_text.hpp"
#include "veyra/text/text_editor_buffer.hpp"

#include <array>
#include <cstddef>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr std::size_t kHardLines = 400;
constexpr std::size_t kLineLength = 61;
constexpr std::size_t kWrapColumns = 32;
constexpr float kAdvance = 8.0F;

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

std::string seed_document() {
    constexpr std::string_view kFiller =
        "abcdefghijklmnopqrstuvwxyz0123456789 .:-_+~=,;!?/#%@*()[]{}";
    auto document = std::string{};
    document.reserve(kHardLines * (kLineLength + 1));
    for (std::size_t line = 0; line < kHardLines; ++line) {
        auto head = std::string{"L"};
        const auto digits = std::to_string(line);
        while (head.size() + digits.size() < 5) {
            head.push_back('0');
        }
        head += digits;
        head.push_back(' ');
        document += head;
        for (std::size_t column = head.size(); column < kLineLength; ++column) {
            document.push_back(kFiller[(line + column) % kFiller.size()]);
        }
        if (line + 1 < kHardLines) {
            document.push_back('\n');
        }
    }
    return document;
}

std::string snapshot_text(const veyra::TextEditorBuffer& buffer, std::vector<char>& scratch) {
    auto length = std::size_t{0};
    const auto result = buffer.copy_text(scratch, length);
    require(result == veyra::TextEditResult::complete, "workload text copies into scratch");
    return {scratch.data(), length};
}

void require_changed(veyra::EditableTextEdit edit, std::string_view message) {
    require(edit.result == veyra::EditableTextResult::changed, message);
    require(edit.edit == veyra::TextEditResult::complete, message);
}

void require_ignored(veyra::EditableTextEdit edit, std::string_view message) {
    require(edit.result == veyra::EditableTextResult::ignored, message);
}

void require_placed(veyra::EditableTextEdit edit, std::string_view message) {
    require(edit.result != veyra::EditableTextResult::ignored, message);
    require(edit.edit == veyra::TextEditResult::complete, message);
}

void type_at(
    veyra::EditableTextController& field,
    std::size_t byte_offset,
    std::string_view text,
    std::string_view message
) {
    require_placed(field.set_cursor(byte_offset), message);
    require_changed(field.insert(text), message);
}

void check_typing_records_every_undo_step(
    veyra::TextEditorBuffer& buffer,
    veyra::EditableTextController& field,
    std::vector<char>& scratch
) {
    require(
        field.set_focused(true) == veyra::EditableTextResult::handled,
        "workload field takes focus"
    );

    const auto base = snapshot_text(buffer, scratch);
    require(base.size() > 20000, "seeded document holds tens of thousands of chars");

    type_at(field, base.size(), " typed-tail", "tail typing lands through the controller");
    const auto first = snapshot_text(buffer, scratch);

    type_at(field, 0, "head: ", "head typing lands through the controller");
    const auto second = snapshot_text(buffer, scratch);

    type_at(field, second.size() / 2, "<mid>", "middle typing lands through the controller");
    const auto third = snapshot_text(buffer, scratch);

    require(first != base, "tail typing changes the document");
    require(second != first, "head typing changes the document");
    require(third != second, "middle typing changes the document");
    require(buffer.can_undo(), "typed edits are undoable");

    require(buffer.undo() == veyra::TextEditResult::complete, "middle typing undoes");
    require(snapshot_text(buffer, scratch) == second, "undo drops the middle typing first");

    require(buffer.undo() == veyra::TextEditResult::complete, "head typing undoes");
    require(snapshot_text(buffer, scratch) == first, "undo drops the head typing next");

    require(buffer.undo() == veyra::TextEditResult::complete, "tail typing undoes");
    require(snapshot_text(buffer, scratch) == base, "undo returns to the seeded document");
    require(!buffer.can_undo(), "seeded text holds no undo record");

    require(buffer.redo() == veyra::TextEditResult::complete, "tail typing redoes");
    require(snapshot_text(buffer, scratch) == first, "redo restores the tail typing");

    require(buffer.redo() == veyra::TextEditResult::complete, "head typing redoes");
    require(snapshot_text(buffer, scratch) == second, "redo restores the head typing");

    require(buffer.redo() == veyra::TextEditResult::complete, "middle typing redoes");
    require(snapshot_text(buffer, scratch) == third, "redo restores the middle typing");
}

void check_ime_session_commits_as_one_step(
    veyra::TextEditorBuffer& buffer,
    veyra::EditableTextController& field,
    std::vector<char>& scratch,
    std::array<char, 256>& preedit
) {
    require(
        field.set_focused(false) == veyra::EditableTextResult::handled,
        "workload field loses focus"
    );
    require_ignored(field.update_composition(preedit, "nope", 4, {}), "preedit updates need focus");
    require_ignored(field.commit_composition(), "preedit commits need focus");
    require(!field.composing(), "rejected preedit leaves no session");
    require(
        field.set_focused(true) == veyra::EditableTextResult::handled,
        "workload field regains focus"
    );

    require(field.set_cursor(100).edit == veyra::TextEditResult::complete, "ime range starts");
    require(
        field.set_cursor(105, true).edit == veyra::TextEditResult::complete,
        "ime range covers five bytes"
    );
    const auto before = snapshot_text(buffer, scratch);

    const auto first_mark = field.update_composition(preedit, "ab", 2, {});
    require(
        first_mark.result == veyra::EditableTextResult::handled,
        "preedit revision touches no committed text"
    );
    require(field.composing(), "first preedit revision opens a session");
    require(snapshot_text(buffer, scratch) == before, "open preedit leaves committed text alone");

    const auto second_mark = field.update_composition(preedit, "abcd", 4, {});
    require(
        second_mark.result == veyra::EditableTextResult::handled,
        "preedit re-mark touches no committed text"
    );
    require(snapshot_text(buffer, scratch) == before, "re-mark leaves committed text alone");

    require_changed(field.commit_composition(), "ime commit lands through the controller");
    require(!field.composing(), "commit closes the session");
    const auto committed = snapshot_text(buffer, scratch);
    require(
        committed == before.substr(0, 100) + "abcd" + before.substr(105),
        "commit replaces the marked range"
    );

    require(buffer.undo() == veyra::TextEditResult::complete, "ime commit undoes");
    require(snapshot_text(buffer, scratch) == before, "one undo removes the whole commit");

    require(buffer.redo() == veyra::TextEditResult::complete, "ime commit redoes");
    require(snapshot_text(buffer, scratch) == committed, "redo restores the committed preedit");
}

struct VisualMap {
    std::vector<std::vector<veyra::TextCaretStop>> stops;
    std::vector<veyra::TextVisualLine> lines;
};

VisualMap build_visual_map(const std::string& text) {
    auto map = VisualMap{};
    auto hard_start = std::size_t{0};
    while (true) {
        auto hard_end = text.find('\n', hard_start);
        if (hard_end == std::string::npos) {
            hard_end = text.size();
        }
        auto offset = hard_start;
        while (true) {
            auto shard_length = hard_end - offset;
            if (shard_length > kWrapColumns) {
                shard_length = kWrapColumns;
            }
            auto shard = std::vector<veyra::TextCaretStop>{};
            for (std::size_t column = 0; column <= shard_length; ++column) {
                shard.push_back(
                    veyra::TextCaretStop{
                        .byte_offset = offset + column,
                        .x = static_cast<float>(column) * kAdvance,
                    }
                );
            }
            map.stops.push_back(shard);
            offset += shard_length;
            if (offset == hard_end) {
                break;
            }
        }
        if (hard_end == text.size()) {
            break;
        }
        hard_start = hard_end + 1;
    }
    for (const auto& shard : map.stops) {
        map.lines.push_back(veyra::TextVisualLine{.caret_stops = shard});
    }
    return map;
}

void check_vertical_navigation_keeps_preferred_x(
    veyra::TextEditorBuffer& buffer,
    veyra::EditableTextController& field,
    std::vector<char>& scratch
) {
    const auto text = snapshot_text(buffer, scratch);
    const auto map = build_visual_map(text);
    require(map.lines.size() > 40, "wrapped document spans many visual lines");

    constexpr std::size_t kEntryLine = 30;
    constexpr std::size_t kColumn = 10;
    const float preferred_x = static_cast<float>(kColumn) * kAdvance;
    require(
        map.stops[kEntryLine].size() > kColumn + 1,
        "entry visual line covers the travel column"
    );

    const auto entry_offset = map.stops[kEntryLine][kColumn].byte_offset;
    require_placed(field.set_cursor(entry_offset), "entry caret lands through the controller");

    auto position = veyra::TextVisualPosition{.line_index = kEntryLine, .stop_index = kColumn};
    for (std::size_t step = 0; step < 3; ++step) {
        const auto target = position.line_index + 1;
        require(map.stops[target].size() > kColumn + 1, "target visual line keeps the column");
        auto next = veyra::TextVisualPosition{};
        require(
            buffer.move_cursor_vertically(
                map.lines,
                position,
                veyra::TextVerticalDirection::down,
                preferred_x,
                next
            ) == veyra::TextEditResult::complete,
            "down step moves across visual lines"
        );
        require(next.line_index == target, "down step advances one visual line");
        require(next.stop_index == kColumn, "down step keeps the preferred column");
        require(
            buffer.cursor() == map.stops[target][kColumn].byte_offset,
            "down step lands on the measured stop"
        );
        position = next;
    }
    for (std::size_t step = 0; step < 3; ++step) {
        auto next = veyra::TextVisualPosition{};
        require(
            buffer.move_cursor_vertically(
                map.lines,
                position,
                veyra::TextVerticalDirection::up,
                preferred_x,
                next
            ) == veyra::TextEditResult::complete,
            "up step returns across visual lines"
        );
        require(next.line_index == position.line_index - 1, "up step retreats one visual line");
        require(next.stop_index == kColumn, "up step keeps the preferred column");
        position = next;
    }
    require(buffer.cursor() == entry_offset, "up steps return to the entry caret");
    require(
        position.line_index == kEntryLine && position.stop_index == kColumn,
        "up steps return to the entry visual position"
    );
}

void check_selection_spans_edits_around_it(
    veyra::TextEditorBuffer& buffer,
    veyra::EditableTextController& field,
    std::vector<char>& scratch
) {
    const auto text = snapshot_text(buffer, scratch);
    auto line_start = std::size_t{0};
    for (std::size_t line = 0; line < 200; ++line) {
        const auto feed = text.find('\n', line_start);
        require(feed != std::string::npos, "seeded document reaches line 200");
        line_start = feed + 1;
    }

    const auto anchor = line_start + 20;
    require_placed(field.set_cursor(anchor), "selection anchor lands");
    require_placed(field.set_cursor(anchor + 20, true), "selection extends twenty bytes");
    const auto selected = buffer.selection();
    require(selected.start == anchor && selected.length == 20, "selection covers twenty bytes");

    auto selected_bytes = std::array<char, 32>{};
    auto selected_length = std::size_t{0};
    require(
        buffer.copy_selection(selected_bytes, selected_length) == veyra::TextEditResult::complete,
        "kept span copies out"
    );
    require(selected_length == 20, "kept span reports twenty bytes");
    const auto kept = std::string(selected_bytes.data(), selected_length);

    require_placed(field.set_cursor(anchor - 6, true), "caret extends backward, anchor holds");
    const auto extended = buffer.selection();
    require(
        extended.start == anchor - 6 && extended.length == 6,
        "backward extension keeps the anchor"
    );

    const auto before_insert = snapshot_text(buffer, scratch);
    require_changed(field.insert(">>>"), "prefix insert lands ahead of the kept span");
    const auto after_insert = snapshot_text(buffer, scratch);
    require(
        after_insert.substr(anchor - 3, kept.size()) == kept,
        "insert ahead shifts the kept span intact"
    );
    require(buffer.selection().length == 0, "replacement collapses the selection");
    require(buffer.cursor() == anchor - 3, "caret rests after the inserted prefix");

    require(buffer.undo() == veyra::TextEditResult::complete, "prefix insert undoes");
    require(
        snapshot_text(buffer, scratch) == before_insert,
        "undo restores text around the selection"
    );
    const auto restored = buffer.selection();
    require(
        restored.start == anchor - 6 && restored.length == 6,
        "undo restores the extended selection"
    );

    require(buffer.redo() == veyra::TextEditResult::complete, "prefix insert redoes");
    require(snapshot_text(buffer, scratch) == after_insert, "redo restores the shifted span");

    const auto cut_at = line_start + 44;
    require_placed(field.set_cursor(cut_at), "delete range starts");
    require_placed(field.set_cursor(cut_at + 8, true), "delete range covers eight bytes");
    const auto before_delete = snapshot_text(buffer, scratch);
    require_changed(field.erase_backward(), "span delete lands through the controller");
    const auto after_delete = snapshot_text(buffer, scratch);
    require(
        after_delete == before_delete.substr(0, cut_at) + before_delete.substr(cut_at + 8),
        "neighbors join across the deleted span"
    );

    require(buffer.undo() == veyra::TextEditResult::complete, "span delete undoes");
    require(snapshot_text(buffer, scratch) == before_delete, "undo restores the deleted span");
}

void check_cursor_moves_add_no_steps_and_branch_clears_redo(
    veyra::TextEditorBuffer& buffer,
    veyra::EditableTextController& field,
    std::vector<char>& scratch
) {
    const auto base = snapshot_text(buffer, scratch);

    type_at(field, base.size(), " one", "chain edit one lands");
    const auto first = snapshot_text(buffer, scratch);

    type_at(field, 10, "two ", "chain edit two lands");
    const auto second = snapshot_text(buffer, scratch);

    type_at(field, 500, "three ", "chain edit three lands");
    const auto third = snapshot_text(buffer, scratch);

    require_placed(field.set_cursor(0), "caret roams to the front");
    require_placed(field.set_cursor(700, true), "caret roams with its selection");
    require_placed(field.set_cursor(third.size()), "caret roams to the end");

    require(buffer.undo() == veyra::TextEditResult::complete, "chain edit three undoes");
    require(snapshot_text(buffer, scratch) == second, "caret moves add no undo step");

    require_placed(field.set_cursor(5), "caret roams between undos");
    require(buffer.undo() == veyra::TextEditResult::complete, "chain edit two undoes");
    require(snapshot_text(buffer, scratch) == first, "roaming never branches the chain");

    require_placed(field.set_cursor(9000, true), "caret roams far with its selection");
    require(buffer.undo() == veyra::TextEditResult::complete, "chain edit one undoes");
    require(snapshot_text(buffer, scratch) == base, "chain undoes back to its base");
    require(buffer.can_undo(), "earlier flow steps stay undoable below the chain");
    require(buffer.can_redo(), "undone chain offers redo");

    require(buffer.redo() == veyra::TextEditResult::complete, "chain edit one redoes");
    require(snapshot_text(buffer, scratch) == first, "redo restores edit one");
    require_placed(field.set_cursor(3), "caret roams between redos");
    require(buffer.redo() == veyra::TextEditResult::complete, "chain edit two redoes");
    require(snapshot_text(buffer, scratch) == second, "redo restores edit two");
    require(buffer.redo() == veyra::TextEditResult::complete, "chain edit three redoes");
    require(snapshot_text(buffer, scratch) == third, "redo restores edit three");
    require(!buffer.can_redo(), "chain tip holds no redo");

    require_placed(field.set_cursor(20), "branch caret lands");
    require_changed(field.insert("BRANCH"), "branch edit lands");
    require(!buffer.can_redo(), "new edit clears the redo path");
    require(
        buffer.redo() == veyra::TextEditResult::invalid_state,
        "cleared redo reports its state"
    );

    require(buffer.undo() == veyra::TextEditResult::complete, "branch edit undoes");
    require(snapshot_text(buffer, scratch) == third, "branch undo returns to the chain tip");
}

void check_gates_reject_edits_but_keep_reads(
    veyra::TextEditorBuffer& buffer,
    veyra::EditableTextController& field,
    std::vector<char>& scratch,
    std::array<char, 256>& preedit
) {
    const auto steady = snapshot_text(buffer, scratch);
    require(field.set_readonly(true), "readonly engages mid-flow");

    require_ignored(field.insert("nope"), "insert waits out readonly");
    require_ignored(field.paste("nope"), "paste waits out readonly");
    require_ignored(field.erase_backward(), "erase waits out readonly");
    require_ignored(field.erase_forward(), "forward erase waits out readonly");
    require_ignored(field.clear(), "clear waits out readonly");
    require_ignored(field.update_composition(preedit, "x", 1, {}), "preedit waits out readonly");
    require_ignored(field.commit_composition(), "commit waits out readonly");
    require(
        field.select_all() != veyra::EditableTextResult::ignored,
        "select-all still works while readonly"
    );

    auto probe_length = std::size_t{0};
    require(
        field.copy_selection(scratch, probe_length) == veyra::TextEditResult::complete,
        "copy still reads while readonly"
    );
    require(probe_length == steady.size(), "readonly copy still sees the whole document");
    require(snapshot_text(buffer, scratch) == steady, "rejected edits leave text alone");

    auto semantics = veyra::SemanticNodeData{};
    require(field.write_semantics("Workload", "snapshot", semantics), "semantics still write");
    require(
        veyra::has_state(semantics.states, veyra::SemanticState::read_only),
        "readonly semantics flag the state"
    );
    require(
        !veyra::has_action(semantics.actions, veyra::SemanticAction::set_value),
        "readonly semantics drop set_value"
    );

    const auto roam = field.set_cursor(42);
    require(roam.edit == veyra::TextEditResult::complete, "caret still roams while readonly");

    require(field.set_readonly(false), "readonly releases");
    require_changed(field.insert("!"), "typing resumes after readonly");
    require(buffer.undo() == veyra::TextEditResult::complete, "resume edit undoes cleanly");
    require(snapshot_text(buffer, scratch) == steady, "flow ends on steady text");
}

}

int main() {
    try {
        const auto document = seed_document();

        auto storage = std::vector<char>(49152);
        auto history = std::vector<char>(32768);
        auto scratch = std::vector<char>(49152);
        auto preedit = std::array<char, 256>{};

        auto buffer =
            veyra::TextEditorBuffer(std::span<char>{storage}, document, std::span<char>{history});
        require(
            buffer.initialization_result() == veyra::TextEditResult::complete,
            "seeded document fits its storage"
        );
        auto field = veyra::EditableTextController(buffer);

        check_typing_records_every_undo_step(buffer, field, scratch);
        check_ime_session_commits_as_one_step(buffer, field, scratch, preedit);
        check_vertical_navigation_keeps_preferred_x(buffer, field, scratch);
        check_selection_spans_edits_around_it(buffer, field, scratch);
        check_cursor_moves_add_no_steps_and_branch_clears_redo(buffer, field, scratch);
        check_gates_reject_edits_but_keep_reads(buffer, field, scratch, preedit);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
