#include "veyra/platform/text_input.hpp"
#include "veyra/text/editor_viewport.hpp"
#include "veyra/text/text_editor_buffer.hpp"

#include <array>
#include <cstddef>
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

bool near(float actual, float expected) {
    return (actual > expected ? actual - expected : expected - actual) < 0.001F;
}

veyra::WindowTextCommit make_commit(std::string_view text) {
    veyra::WindowTextCommit commit{};

    require(text.size() <= commit.text.size(), "commit text fits the inline buffer");

    for (std::size_t index = 0; index < text.size(); ++index) {
        commit.text[index] = text[index];
    }

    commit.length = static_cast<std::uint32_t>(text.size());

    return commit;
}

veyra::WindowTextEditing make_editing(std::string_view text, std::int32_t cursor) {
    veyra::WindowTextEditing editing{};

    require(text.size() <= editing.text.size(), "preedit text fits the inline buffer");

    for (std::size_t index = 0; index < text.size(); ++index) {
        editing.text[index] = text[index];
    }

    editing.length = static_cast<std::uint32_t>(text.size());
    editing.cursor = cursor;

    return editing;
}

std::size_t count_lines(const veyra::TextEditorBuffer& buffer) {
    std::array<char, 256> copy{};
    std::size_t length = 0;

    require(
        buffer.copy_text(copy, length) == veyra::TextEditResult::complete,
        "document copies into caller storage"
    );

    if (length == 0) {
        return 1;
    }

    std::size_t lines = 1;

    for (std::size_t index = 0; index < length; ++index) {
        if (copy[index] == '\n') {
            ++lines;
        }
    }

    return lines;
}

void check_type_scroll_and_area_chain() {
    std::array<char, 256> storage{};
    veyra::TextEditorBuffer buffer(storage, "");
    veyra::EditorViewport viewport;

    require(
        viewport.set_line_height(16.0F) == veyra::EditorViewportResult::changed,
        "line height sticks"
    );
    require(
        viewport.set_view_height(64.0F) == veyra::EditorViewportResult::changed,
        "view height shows four lines"
    );

    veyra::TextCommitApplied commit_applied{};

    for (int line = 0; line < 8; ++line) {
        veyra::apply_text_commit(buffer, make_commit("ab\n"), commit_applied);
        require(commit_applied.edit == veyra::TextEditResult::complete, "typed line commits");
        require(!commit_applied.truncated, "fitting commits report no truncation");

        require(
            viewport.set_total_lines(count_lines(buffer)) == veyra::EditorViewportResult::changed,
            "caller recounts lines after each commit"
        );
    }

    require(count_lines(buffer) == 9, "eight newlines make nine lines");
    require(buffer.cursor() == 24, "caret sits after the last newline");

    require(
        viewport.ensure_visible(8, 1) == veyra::EditorViewportResult::changed,
        "ensure follows the caret line"
    );
    require(viewport.first_line() == 5, "caret line lands at the bottom of the window");

    std::size_t first = 0;
    std::size_t line_count = 0;
    viewport.visible_range(first, line_count);
    require(first == 5 && line_count == 4, "visible range covers the caret line");

    const float caret_y = static_cast<float>(8 - viewport.first_line()) * viewport.line_height();
    const veyra::LayoutRect caret_rect{4.0F, caret_y, 2.0F, 16.0F};
    const auto area = veyra::text_area_for_caret(caret_rect, buffer.cursor());

    require(area.x == 4 && area.y == 48, "area tracks the caret line on screen");
    require(area.width == 2 && area.height == 16, "area keeps the caret extent");

    require(
        viewport.ensure_visible(0, 0) == veyra::EditorViewportResult::changed,
        "ensure returns to the top"
    );

    const veyra::LayoutRect top_rect{4.0F, 0.0F, 2.0F, 16.0F};
    const auto top_area = veyra::text_area_for_caret(top_rect, 0);
    require(top_area.y == 0, "area follows the caret back to the top");
}

void check_preedit_commit_chain() {
    std::array<char, 256> storage{};
    std::array<char, 32> scratch{};
    veyra::TextEditorBuffer buffer(storage, "ab\ncd\n");
    veyra::EditorViewport viewport;

    require(
        viewport.set_line_height(16.0F) == veyra::EditorViewportResult::changed,
        "line height sticks"
    );
    require(
        viewport.set_view_height(32.0F) == veyra::EditorViewportResult::changed,
        "view height shows two lines"
    );
    require(
        viewport.set_total_lines(count_lines(buffer)) == veyra::EditorViewportResult::changed,
        "line count starts at three"
    );

    veyra::TextEditingApplied editing_applied{};
    veyra::apply_text_editing(buffer, scratch, make_editing("xy", 2), editing_applied);

    require(editing_applied.edit == veyra::TextEditResult::complete, "preedit starts");
    require(buffer.has_composition(), "preedit marks the buffer composing");

    veyra::TextCommitApplied commit_applied{};
    veyra::apply_text_commit(buffer, make_commit("XY"), commit_applied);

    require(commit_applied.edit == veyra::TextEditResult::complete, "commit lands once");
    require(!buffer.has_composition(), "commit clears the preedit");
    require(buffer.cursor() == 8, "caret sits after the committed text");

    require(
        viewport.set_total_lines(count_lines(buffer)) == veyra::EditorViewportResult::handled,
        "commit without newlines keeps the line count"
    );
    require(
        viewport.ensure_visible(2, 0) == veyra::EditorViewportResult::changed,
        "ensure reaches the last line"
    );
    require(viewport.first_line() == 1, "last line lands at the bottom of the window");

    std::size_t first = 99;
    std::size_t line_count = 99;
    viewport.visible_range(first, line_count);
    require(first == 1 && line_count == 2, "clamped range covers the last window");
}

void check_prepend_keeps_caret_visible() {
    veyra::EditorViewport viewport;

    require(
        viewport.set_line_height(10.0F) == veyra::EditorViewportResult::changed,
        "line height sticks"
    );
    require(
        viewport.set_view_height(40.0F) == veyra::EditorViewportResult::changed,
        "view height shows four lines"
    );
    require(
        viewport.set_total_lines(10) == veyra::EditorViewportResult::changed,
        "document starts at ten lines"
    );
    require(
        viewport.scroll_to_line(5) == veyra::EditorViewportResult::changed,
        "caret line scrolls into view"
    );

    require(
        viewport.set_total_lines(13, 3) == veyra::EditorViewportResult::changed,
        "three lines arrive above the window"
    );
    require(viewport.first_line() == 8, "anchor shifts past the prepended lines");

    std::size_t first = 0;
    std::size_t line_count = 0;
    viewport.visible_range(first, line_count);
    require(first == 8 && line_count == 4, "caret content stays inside the range");
    require(near(viewport.offset_in_first(), 0.0F), "prepend keeps the pixel alignment");
}

}

int main() {
    try {
        check_type_scroll_and_area_chain();
        check_preedit_commit_chain();
        check_prepend_keeps_caret_visible();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }

    return 0;
}
