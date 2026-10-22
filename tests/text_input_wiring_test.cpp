#include "veyra/platform/text_input.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

veyra::WindowTextCommit make_commit(std::string_view text, bool truncated = false) {
    veyra::WindowTextCommit commit{};

    require(text.size() <= commit.text.size(), "commit text fits the inline buffer");

    for (std::size_t index = 0; index < text.size(); ++index) {
        commit.text[index] = text[index];
    }

    commit.length = static_cast<std::uint32_t>(text.size());
    commit.truncated = truncated;

    return commit;
}

veyra::WindowTextEditing make_editing(
    std::string_view text,
    std::int32_t cursor,
    std::int32_t selection_length,
    bool truncated = false
) {
    veyra::WindowTextEditing editing{};

    require(text.size() <= editing.text.size(), "preedit text fits the inline buffer");

    for (std::size_t index = 0; index < text.size(); ++index) {
        editing.text[index] = text[index];
    }

    editing.length = static_cast<std::uint32_t>(text.size());
    editing.cursor = cursor;
    editing.selection_length = selection_length;
    editing.truncated = truncated;

    return editing;
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

void check_commit_inserts_text() {
    std::array<char, 32> storage{};
    veyra::TextEditorBuffer buffer(storage, "hello");
    veyra::TextCommitApplied applied{};

    veyra::apply_text_commit(buffer, make_commit(" world"), applied);

    require(applied.edit == veyra::TextEditResult::complete, "commit inserts");
    require(!applied.truncated, "fitting commit reports no truncation");
    require(copied_text(buffer) == "hello world", "commit lands at the cursor");
}

void check_commit_truncation_inserts_and_reports() {
    std::array<char, 32> storage{};
    veyra::TextEditorBuffer buffer(storage, "ab");
    veyra::TextCommitApplied applied{};

    veyra::apply_text_commit(buffer, make_commit("cd", true), applied);

    require(applied.edit == veyra::TextEditResult::complete, "truncated commit still inserts");
    require(applied.truncated, "truncation flag passes through for resync");
    require(copied_text(buffer) == "abcd", "received bytes land before the resync");

    veyra::apply_text_commit(buffer, make_commit("\xff"), applied);

    require(applied.edit == veyra::TextEditResult::invalid_utf8, "bad bytes are rejected");
    require(copied_text(buffer) == "abcd", "rejected commit leaves text unchanged");
}

void check_commit_drops_live_preedit() {
    std::array<char, 32> storage{};
    std::array<char, 16> scratch{};
    veyra::TextEditorBuffer buffer(storage, "hello");
    veyra::TextCommitApplied applied{};

    require(
        buffer.update_composition(scratch, "XY", 2, {}) == veyra::TextEditResult::complete,
        "preedit starts"
    );

    veyra::apply_text_commit(buffer, make_commit("!"), applied);

    require(applied.edit == veyra::TextEditResult::complete, "commit during preedit inserts");
    require(!buffer.has_composition(), "commit drops the marked text");
    require(copied_text(buffer) == "hello!", "commit lands once without doubling");
}

void check_editing_maps_cursor_and_selection() {
    std::array<char, 32> storage{};
    std::array<char, 16> scratch{};
    veyra::TextEditorBuffer buffer(storage, "hello");
    veyra::TextEditingApplied applied{};

    veyra::apply_text_editing(buffer, scratch, make_editing("pre", 1, 0), applied);

    require(applied.edit == veyra::TextEditResult::complete, "preedit applies");
    require(!applied.truncated, "fitting preedit reports no truncation");

    const auto composition = buffer.composition();
    require(composition.text == "pre", "preedit text copies into caller storage");
    require(composition.cursor == 1, "character cursor maps to the byte cursor");
    require(
        composition.selection.start == 1 && composition.selection.length == 0,
        "zero selection length means caret only"
    );
}

void check_editing_maps_multibyte_selection() {
    std::array<char, 32> storage{};
    std::array<char, 16> scratch{};
    veyra::TextEditorBuffer buffer(storage, "");
    veyra::TextEditingApplied applied{};

    veyra::apply_text_editing(buffer, scratch, make_editing("\xc2\xa2X", 1, 1), applied);

    require(applied.edit == veyra::TextEditResult::complete, "multibyte preedit applies");

    const auto composition = buffer.composition();
    require(composition.cursor == 2, "cursor skips the two-byte character");
    require(
        composition.selection.start == 2 && composition.selection.length == 1,
        "selection length converts characters to bytes"
    );
}

void check_editing_negative_cursor_hides_caret() {
    std::array<char, 32> storage{};
    std::array<char, 16> scratch{};
    veyra::TextEditorBuffer buffer(storage, "");
    veyra::TextEditingApplied applied{};

    veyra::apply_text_editing(buffer, scratch, make_editing("pre", -1, -1), applied);

    require(applied.edit == veyra::TextEditResult::complete, "hidden-caret preedit applies");

    const auto composition = buffer.composition();
    require(composition.cursor == 0, "negative cursor parks the caret at zero");
    require(
        composition.selection.start == 0 && composition.selection.length == 0,
        "negative selection length means no selection"
    );
}

void check_editing_rejects_bad_offsets() {
    std::array<char, 32> storage{};
    std::array<char, 16> scratch{};
    veyra::TextEditorBuffer buffer(storage, "");
    veyra::TextEditingApplied applied{};

    veyra::apply_text_editing(buffer, scratch, make_editing("pre", 1, 0), applied);
    require(applied.edit == veyra::TextEditResult::complete, "baseline preedit applies");

    veyra::apply_text_editing(buffer, scratch, make_editing("pre", 99, 0), applied);
    require(applied.edit == veyra::TextEditResult::invalid_cursor, "past-end cursor is rejected");
    require(buffer.composition().text == "pre", "rejected cursor keeps the old preedit");

    veyra::apply_text_editing(buffer, scratch, make_editing("pre", 2, 5), applied);
    require(
        applied.edit == veyra::TextEditResult::invalid_cursor,
        "selection past the end is rejected"
    );
    require(buffer.composition().text == "pre", "rejected selection keeps the old preedit");

    veyra::apply_text_editing(buffer, scratch, make_editing("pre", -1, 2), applied);
    require(
        applied.edit == veyra::TextEditResult::invalid_cursor,
        "selection without a caret is rejected"
    );
    require(buffer.composition().text == "pre", "caretless selection keeps the old preedit");
}

void check_empty_editing_cancels() {
    std::array<char, 32> storage{};
    std::array<char, 16> scratch{};
    veyra::TextEditorBuffer buffer(storage, "hello");
    veyra::TextEditingApplied applied{};

    require(
        buffer.update_composition(scratch, "XY", 2, {}) == veyra::TextEditResult::complete,
        "preedit starts"
    );

    veyra::apply_text_editing(buffer, scratch, make_editing("", 0, 0), applied);

    require(applied.edit == veyra::TextEditResult::complete, "empty preedit cancels cleanly");
    require(!buffer.has_composition(), "empty preedit clears the composition");
    require(copied_text(buffer) == "hello", "cancel leaves committed text unchanged");
    require(buffer.cursor() == 5, "cancel restores the cursor");

    buffer.cancel_composition();
    require(!buffer.has_composition(), "explicit cancel stays idempotent");
}

void check_area_tracks_caret() {
    const veyra::LayoutRect caret{10.4F, 20.6F, 2.0F, 18.0F};
    const auto area = veyra::text_area_for_caret(caret, 5);

    require(area.x == 10 && area.y == 21, "area rounds the caret origin");
    require(area.width == 2 && area.height == 18, "area keeps the caret extent");
    require(area.cursor == 5, "area carries the preedit byte cursor");

    const veyra::LayoutRect flat{4.0F, 8.0F, 0.0F, 0.0F};
    const auto degenerate = veyra::text_area_for_caret(flat, 0);
    require(degenerate.width == 1 && degenerate.height == 1, "flat rects clamp to one pixel");

    const veyra::LayoutRect broken{
        std::numeric_limits<float>::quiet_NaN(),
        0.0F,
        2.0F,
        2.0F,
    };
    const auto invalid = veyra::text_area_for_caret(broken, 0);
    require(invalid.width == 0 && invalid.height == 0, "non-finite rects report zeros");
    require(invalid.cursor == 0, "non-finite rects carry no cursor");

    const veyra::LayoutRect far{0.0F, 0.0F, 4.0F, 4.0F};
    const auto saturated = veyra::text_area_for_caret(far, std::numeric_limits<std::size_t>::max());
    require(saturated.cursor == std::numeric_limits<int>::max(), "oversized cursor bytes saturate");
}

}

int main() {
    try {
        check_commit_inserts_text();
        check_commit_truncation_inserts_and_reports();
        check_commit_drops_live_preedit();
        check_editing_maps_cursor_and_selection();
        check_editing_maps_multibyte_selection();
        check_editing_negative_cursor_hides_caret();
        check_editing_rejects_bad_offsets();
        check_empty_editing_cancels();
        check_area_tracks_caret();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }

    return 0;
}
