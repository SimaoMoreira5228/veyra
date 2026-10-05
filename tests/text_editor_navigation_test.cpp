#include "veyra/text/text_editor_buffer.hpp"

#include <array>
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

void check_line_boundary_navigation() {
    constexpr std::string_view text = "one\r\ntw\xc3\xb6\nlast";
    std::array<char, 32> storage{};
    veyra::TextEditorBuffer buffer(storage, text);
    const auto second_line_start = std::string_view("one\r\n").size();
    const auto second_line_end = std::string_view("one\r\ntw\xc3\xb6").size();

    require(
        buffer.set_cursor(second_line_start + 2) == veyra::TextEditResult::complete,
        "line navigation cursor moves within the second line"
    );
    require(
        buffer.move_cursor_to_line_end() == veyra::TextEditResult::complete,
        "End moves to the current line boundary"
    );
    require(buffer.cursor() == second_line_end, "End stops before the line feed");
    require(
        buffer.move_cursor_to_line_start() == veyra::TextEditResult::complete,
        "Home moves to the current line boundary"
    );
    require(buffer.cursor() == second_line_start, "Home starts after the CRLF sequence");

    require(
        buffer.set_cursor(second_line_start) == veyra::TextEditResult::complete,
        "line navigation cursor moves to the start of the second line"
    );
    require(
        buffer.move_cursor_to_line_end(true) == veyra::TextEditResult::complete,
        "End can extend a selection"
    );
    require(
        buffer.cursor() == second_line_end && buffer.selection().start == second_line_start &&
            buffer.selection().length == second_line_end - second_line_start,
        "line boundary movement extends selection over UTF-8 text"
    );

    require(
        buffer.set_cursor(text.size()) == veyra::TextEditResult::complete,
        "line navigation cursor moves to the document end"
    );
    require(
        buffer.move_cursor_to_line_start() == veyra::TextEditResult::complete,
        "Home handles the final line"
    );
    require(buffer.cursor() == second_line_end + 1, "Home stops after the final line feed");
    require(
        buffer.move_cursor_to_line_end() == veyra::TextEditResult::complete,
        "End handles the final line"
    );
    require(buffer.cursor() == text.size(), "End stops at the document boundary");

    std::array<char, 10> carriage_return_storage{};
    veyra::TextEditorBuffer carriage_return(carriage_return_storage, "left\rright");
    require(
        carriage_return.move_cursor_to_line_start() == veyra::TextEditResult::complete,
        "Home recognizes a standalone carriage return"
    );
    require(carriage_return.cursor() == 5, "Home starts after a standalone carriage return");
    require(
        carriage_return.set_cursor(2) == veyra::TextEditResult::complete,
        "line navigation cursor moves within the first carriage-return line"
    );
    require(
        carriage_return.move_cursor_to_line_end() == veyra::TextEditResult::complete,
        "End recognizes a standalone carriage return"
    );
    require(carriage_return.cursor() == 4, "End stops before a standalone carriage return");
    require(
        carriage_return.set_cursor(7) == veyra::TextEditResult::complete,
        "line navigation cursor moves within the second carriage-return line"
    );
    require(
        carriage_return.move_cursor_to_line_end() == veyra::TextEditResult::complete,
        "End handles text after a standalone carriage return"
    );
    require(carriage_return.cursor() == 10, "End stops at the document boundary");

    require(
        buffer.set_cursor(second_line_start - 1) == veyra::TextEditResult::complete,
        "line navigation cursor can sit between a CRLF pair"
    );
    require(
        buffer.move_cursor_to_line_start() == veyra::TextEditResult::complete,
        "Home treats a cursor inside CRLF as part of the preceding line"
    );
    require(buffer.cursor() == 0, "Home leaves the CRLF pair intact");
    require(
        buffer.move_cursor_to_line_end() == veyra::TextEditResult::complete,
        "End handles a cursor inside CRLF"
    );
    require(buffer.cursor() == 3, "End stops before the CRLF pair");

    constexpr std::string_view unicode_breaks = "a\xc2\x85"
                                                "b\xe2\x80\xa8"
                                                "c\xe2\x80\xa9"
                                                "d";
    std::array<char, 16> unicode_storage{};
    veyra::TextEditorBuffer unicode_lines(unicode_storage, unicode_breaks);
    require(
        unicode_lines.set_cursor(3) == veyra::TextEditResult::complete,
        "line navigation cursor moves past a Unicode next-line separator"
    );
    require(
        unicode_lines.move_cursor_to_line_start() == veyra::TextEditResult::complete,
        "Home recognizes the Unicode next-line separator"
    );
    require(unicode_lines.cursor() == 3, "Home starts after the next-line separator");
    require(
        unicode_lines.set_cursor(4) == veyra::TextEditResult::complete,
        "line navigation cursor moves before a Unicode line separator"
    );
    require(
        unicode_lines.move_cursor_to_line_end() == veyra::TextEditResult::complete,
        "End recognizes the Unicode line separator"
    );
    require(unicode_lines.cursor() == 4, "End stops before the line separator");
    require(
        unicode_lines.set_cursor(unicode_breaks.size()) == veyra::TextEditResult::complete,
        "line navigation cursor moves to the last Unicode paragraph"
    );
    require(
        unicode_lines.move_cursor_to_line_start() == veyra::TextEditResult::complete,
        "Home recognizes the Unicode paragraph separator"
    );
    require(unicode_lines.cursor() == 11, "Home starts after the paragraph separator");
}

void check_visual_line_navigation() {
    std::array<char, 16> storage{};
    veyra::TextEditorBuffer buffer(storage, "a\xc3\xa9z");
    const std::array caret_stops{
        veyra::TextCaretStop{.byte_offset = 0, .x = 0.0F},
        veyra::TextCaretStop{.byte_offset = 1, .x = 8.0F},
        veyra::TextCaretStop{.byte_offset = 3, .x = 20.0F},
        veyra::TextCaretStop{.byte_offset = 4, .x = 28.0F},
    };

    require(buffer.set_cursor(1) == veyra::TextEditResult::complete, "visual cursor starts at é");
    auto selected_stop = std::size_t{99};
    require(
        buffer.move_cursor_to_visual_line(caret_stops, 18.0F, selected_stop, true) ==
            veyra::TextEditResult::complete,
        "vertical navigation picks the nearest stop on a destination line"
    );
    require(selected_stop == 2, "vertical navigation returns the chosen visual stop");
    require(buffer.cursor() == 3, "visual navigation moves to a UTF-8 character boundary");
    require(
        buffer.selection().start == 1 && buffer.selection().length == 2,
        "visual navigation can extend a selection"
    );

    const std::array tied_stops{
        veyra::TextCaretStop{.byte_offset = 3, .x = 8.0F},
        veyra::TextCaretStop{.byte_offset = 1, .x = 24.0F},
    };
    require(
        buffer.move_cursor_to_visual_line(tied_stops, 16.0F, selected_stop) ==
            veyra::TextEditResult::complete,
        "visual navigation resolves equally distant caret stops"
    );
    require(selected_stop == 0 && buffer.cursor() == 3, "ties choose the first visual stop");

    const std::array ambiguous_stops{
        veyra::TextCaretStop{.byte_offset = 1, .x = 8.0F},
        veyra::TextCaretStop{.byte_offset = 1, .x = 24.0F},
    };
    require(
        buffer.move_cursor_to_visual_line(ambiguous_stops, 22.0F, selected_stop) ==
            veyra::TextEditResult::complete,
        "visual navigation distinguishes two visual stops at one logical offset"
    );
    require(
        selected_stop == 1 && buffer.cursor() == 1,
        "the selected stop keeps its visual identity"
    );

    const auto cursor_before_failure = buffer.cursor();
    const auto selected_stop_before_failure = selected_stop;
    const std::array invalid_geometry{
        veyra::TextCaretStop{.byte_offset = 0, .x = 0.0F},
        veyra::TextCaretStop{
            .byte_offset = 3,
            .x = std::numeric_limits<float>::infinity(),
        },
    };
    require(
        buffer.move_cursor_to_visual_line(invalid_geometry, 0.0F, selected_stop) ==
            veyra::TextEditResult::invalid_state,
        "visual navigation rejects non-finite layout positions"
    );
    require(buffer.cursor() == cursor_before_failure, "invalid geometry leaves the cursor alone");
    require(
        buffer.move_cursor_to_visual_line(
            caret_stops,
            std::numeric_limits<float>::infinity(),
            selected_stop
        ) == veyra::TextEditResult::invalid_state,
        "visual navigation rejects a non-finite preferred position"
    );

    const std::array invalid_cursor{
        veyra::TextCaretStop{.byte_offset = 2, .x = 0.0F},
    };
    require(
        buffer.move_cursor_to_visual_line(invalid_cursor, 0.0F, selected_stop) ==
            veyra::TextEditResult::invalid_cursor,
        "visual navigation rejects a stop inside a UTF-8 code point"
    );
    require(
        buffer.cursor() == cursor_before_failure,
        "invalid caret offsets leave the cursor alone"
    );
    require(
        selected_stop == selected_stop_before_failure,
        "failed visual navigation leaves its output alone"
    );
    require(
        buffer.move_cursor_to_visual_line({}, 0.0F, selected_stop) ==
            veyra::TextEditResult::invalid_state,
        "visual navigation rejects an empty destination line"
    );
}

void check_vertical_line_navigation() {
    std::array<char, 16> storage{};
    veyra::TextEditorBuffer buffer(storage, "one\ntwo");
    const std::array first_line_stops{
        veyra::TextCaretStop{.byte_offset = 0, .x = 0.0F},
        veyra::TextCaretStop{.byte_offset = 1, .x = 10.0F},
        veyra::TextCaretStop{.byte_offset = 2, .x = 20.0F},
        veyra::TextCaretStop{.byte_offset = 3, .x = 30.0F},
    };
    const std::array second_line_stops{
        veyra::TextCaretStop{.byte_offset = 4, .x = 0.0F},
        veyra::TextCaretStop{.byte_offset = 6, .x = 10.0F},
        veyra::TextCaretStop{.byte_offset = 6, .x = 20.0F},
        veyra::TextCaretStop{.byte_offset = 7, .x = 30.0F},
    };
    const std::array lines{
        veyra::TextVisualLine{.caret_stops = first_line_stops},
        veyra::TextVisualLine{.caret_stops = second_line_stops},
    };
    require(
        buffer.set_cursor(2) == veyra::TextEditResult::complete,
        "vertical cursor starts on line one"
    );

    auto position = veyra::TextVisualPosition{.line_index = 0, .stop_index = 2};
    auto next_position = veyra::TextVisualPosition{.line_index = 99, .stop_index = 99};
    require(
        buffer.move_cursor_vertically(
            lines,
            position,
            veyra::TextVerticalDirection::down,
            19.0F,
            next_position,
            true
        ) == veyra::TextEditResult::complete,
        "Down moves to the nearest caret on the next visual line"
    );
    require(
        next_position.line_index == 1 && next_position.stop_index == 2 && buffer.cursor() == 6,
        "Down reports the selected visual position, including duplicate offsets"
    );
    require(
        buffer.selection().start == 2 && buffer.selection().length == 4,
        "Down can extend the current selection"
    );

    position = next_position;
    require(
        buffer.move_cursor_vertically(
            lines,
            position,
            veyra::TextVerticalDirection::up,
            19.0F,
            next_position
        ) == veyra::TextEditResult::complete,
        "Up returns to the preceding visual line"
    );
    require(
        next_position.line_index == 0 && next_position.stop_index == 2 && buffer.cursor() == 2,
        "Up keeps the preferred x position"
    );

    const auto cursor_before_boundary = buffer.cursor();
    const auto position_before_boundary = position;
    require(
        buffer.move_cursor_vertically(
            lines,
            next_position,
            veyra::TextVerticalDirection::up,
            19.0F,
            position
        ) == veyra::TextEditResult::complete,
        "Up at the first visual line succeeds without moving"
    );
    require(buffer.cursor() == cursor_before_boundary, "a visual line boundary keeps the cursor");
    require(
        position.line_index == position_before_boundary.line_index &&
            position.stop_index == position_before_boundary.stop_index,
        "a visual line boundary leaves the output position alone"
    );

    require(
        buffer.move_cursor_vertically(
            lines,
            next_position,
            veyra::TextVerticalDirection::down,
            19.0F,
            position
        ) == veyra::TextEditResult::complete,
        "Down moves from the first visual line"
    );
    require(buffer.cursor() == 6, "Down reaches the last visual line");
    const auto cursor_before_last_line = buffer.cursor();
    const auto position_before_last_line = next_position;
    require(
        buffer.move_cursor_vertically(
            lines,
            position,
            veyra::TextVerticalDirection::down,
            19.0F,
            next_position
        ) == veyra::TextEditResult::complete,
        "Down at the last visual line succeeds without moving"
    );
    require(buffer.cursor() == cursor_before_last_line, "the last visual line keeps the cursor");
    require(
        next_position.line_index == position_before_last_line.line_index &&
            next_position.stop_index == position_before_last_line.stop_index,
        "the last visual line leaves the output position alone"
    );

    require(
        buffer.move_cursor_vertically(
            lines,
            {.line_index = 0, .stop_index = 2},
            veyra::TextVerticalDirection::down,
            19.0F,
            position
        ) == veyra::TextEditResult::invalid_state,
        "vertical movement rejects a stale visual position"
    );
    require(
        buffer.cursor() == cursor_before_last_line,
        "stale visual positions leave the cursor alone"
    );
}

void check_visual_edge_navigation() {
    std::array<char, 16> storage{};
    veyra::TextEditorBuffer buffer(storage, "word");
    const std::array caret_stops{
        veyra::TextCaretStop{.byte_offset = 0, .x = 0.0F},
        veyra::TextCaretStop{.byte_offset = 1, .x = 12.0F},
        veyra::TextCaretStop{.byte_offset = 2, .x = 21.0F},
        veyra::TextCaretStop{.byte_offset = 3, .x = 31.0F},
        veyra::TextCaretStop{.byte_offset = 4, .x = 42.0F},
    };
    auto selected_stop = std::size_t{99};

    require(
        buffer.set_cursor(2) == veyra::TextEditResult::complete,
        "visual edge navigation starts inside the line"
    );
    require(
        buffer.move_cursor_to_visual_edge(
            caret_stops,
            veyra::TextVisualEdge::right,
            selected_stop
        ) == veyra::TextEditResult::complete,
        "visual edge movement reaches the rightmost caret stop"
    );
    require(buffer.cursor() == 4 && selected_stop == 4, "right edge reports its caret stop");
    require(
        buffer.move_cursor_to_visual_edge(
            caret_stops,
            veyra::TextVisualEdge::left,
            selected_stop
        ) == veyra::TextEditResult::complete,
        "visual edge movement reaches the leftmost caret stop"
    );
    require(buffer.cursor() == 0 && selected_stop == 0, "left edge reports its caret stop");

    require(
        buffer.move_cursor_to_visual_edge(
            caret_stops,
            veyra::TextVisualEdge::right,
            selected_stop,
            true
        ) == veyra::TextEditResult::complete,
        "visual edge movement can extend a selection"
    );
    require(
        buffer.selection().start == 0 && buffer.selection().length == 4,
        "visual edge movement selects through the right edge"
    );

    const std::array invalid_edge{
        veyra::TextCaretStop{.byte_offset = 99, .x = 0.0F},
    };
    selected_stop = 99;
    require(
        buffer.move_cursor_to_visual_edge(
            invalid_edge,
            veyra::TextVisualEdge::left,
            selected_stop
        ) == veyra::TextEditResult::invalid_cursor,
        "visual edge navigation rejects a caret outside the buffer"
    );
    require(buffer.cursor() == 4, "invalid visual edge geometry leaves the cursor alone");
    require(selected_stop == 99, "invalid visual edge geometry leaves its output alone");

    selected_stop = 99;
    require(
        buffer.move_cursor_to_visual_edge({}, veyra::TextVisualEdge::left, selected_stop) ==
            veyra::TextEditResult::invalid_state,
        "visual edge navigation rejects an empty line"
    );
    require(selected_stop == 99, "failed visual edge navigation leaves its output alone");
}

}

int main() {
    try {
        check_line_boundary_navigation();
        check_visual_line_navigation();
        check_vertical_line_navigation();
        check_visual_edge_navigation();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
