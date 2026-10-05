#ifndef VEYRA_TEXT_TEXT_EDITOR_BUFFER_HPP
#define VEYRA_TEXT_TEXT_EDITOR_BUFFER_HPP

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace veyra {

class TextEditorHistory;

enum class TextEditResult : std::uint8_t {
    complete,
    invalid_utf8,
    invalid_cursor,
    invalid_direction,
    invalid_state,
    insufficient_capacity,
    insufficient_history_capacity,
    overlapping_input,
    overlapping_output,
};

enum class TextCursorDirection : std::uint8_t {
    backward,
    forward,
};

enum class TextVerticalDirection : std::uint8_t {
    up,
    down,
};

enum class TextVisualEdge : std::uint8_t {
    left,
    right,
};

struct TextSelection {
    std::size_t start{};
    std::size_t length{};
};

struct TextCaretStop {
    std::size_t byte_offset{};
    float x{};
};

struct TextVisualLine {
    std::span<const TextCaretStop> caret_stops;
};

struct TextVisualPosition {
    std::size_t line_index{};
    std::size_t stop_index{};
};

struct TextComposition {
    std::string_view text;
    TextSelection replacement;
    std::size_t cursor{};
    TextSelection selection;
};

TextEditResult utf8_byte_offset(
    std::string_view text,
    std::size_t character_offset,
    std::size_t& byte_offset
) noexcept;

class TextEditorBuffer {
  public:
    TextEditorBuffer(
        std::span<char> storage,
        std::string_view initial_text,
        std::span<char> history_storage = {}
    ) noexcept;
    TextEditorBuffer(const TextEditorBuffer&) = delete;
    TextEditorBuffer& operator=(const TextEditorBuffer&) = delete;
    TextEditorBuffer(TextEditorBuffer&&) = delete;
    TextEditorBuffer& operator=(TextEditorBuffer&&) = delete;

    TextEditResult initialization_result() const noexcept;
    std::string_view text_before_cursor() const noexcept;
    std::string_view text_after_cursor() const noexcept;
    std::size_t size() const noexcept;
    std::size_t cursor() const noexcept;
    TextSelection selection() const noexcept;

    TextEditResult copy_text(std::span<char> output, std::size_t& length) const noexcept;
    TextEditResult copy_selection(std::span<char> output, std::size_t& length) const noexcept;
    TextEditResult set_cursor(std::size_t byte_offset, bool extend_selection = false) noexcept;
    TextEditResult
    move_cursor(TextCursorDirection direction, bool extend_selection = false) noexcept;
    TextEditResult move_cursor_to_line_start(bool extend_selection = false) noexcept;
    TextEditResult move_cursor_to_line_end(bool extend_selection = false) noexcept;
    TextEditResult move_cursor_to_visual_line(
        std::span<const TextCaretStop> caret_stops,
        float preferred_x,
        std::size_t& selected_stop,
        bool extend_selection = false
    ) noexcept;
    TextEditResult move_cursor_to_visual_edge(
        std::span<const TextCaretStop> caret_stops,
        TextVisualEdge edge,
        std::size_t& selected_stop,
        bool extend_selection = false
    ) noexcept;
    TextEditResult move_cursor_vertically(
        std::span<const TextVisualLine> lines,
        TextVisualPosition current_position,
        TextVerticalDirection direction,
        float preferred_x,
        TextVisualPosition& next_position,
        bool extend_selection = false
    ) noexcept;
    void select_all() noexcept;
    TextEditResult insert(std::string_view text) noexcept;
    TextEditResult erase_backward() noexcept;
    TextEditResult erase_forward() noexcept;
    bool can_undo() const noexcept;
    bool can_redo() const noexcept;
    TextEditResult undo() noexcept;
    TextEditResult redo() noexcept;
    TextEditResult update_composition(
        std::span<char> storage,
        std::string_view text,
        std::size_t cursor,
        TextSelection selection
    ) noexcept;
    TextComposition composition() const noexcept;
    TextEditResult commit_composition() noexcept;
    void cancel_composition() noexcept;

  private:
    friend class TextEditorHistory;

    std::span<char> storage_;
    std::span<char> history_storage_;
    std::size_t gap_start_{};
    std::size_t gap_end_{};
    std::size_t selection_anchor_{};
    std::size_t history_start_{};
    std::size_t history_cursor_{};
    std::size_t history_end_{};
    std::size_t history_used_{};
    std::size_t history_record_count_{};
    std::size_t history_undo_count_{};
    std::span<char> composition_storage_;
    std::size_t composition_length_{};
    std::size_t composition_cursor_{};
    std::size_t composition_selection_start_{};
    std::size_t composition_selection_length_{};
    TextSelection composition_replacement_;
    std::size_t original_cursor_{};
    std::size_t original_selection_anchor_{};
    bool composition_active_{};
    TextEditResult initialization_result_{TextEditResult::complete};

    char byte_at(std::size_t byte_offset) const noexcept;
    bool is_boundary(std::size_t byte_offset) const noexcept;
    void move_gap(std::size_t byte_offset) noexcept;
    std::size_t line_break_length_at(std::size_t byte_offset) const noexcept;
    std::size_t line_start_at_cursor() const noexcept;
    std::size_t line_end_at_cursor() const noexcept;
    TextEditResult replace_selection(std::string_view text) noexcept;
    void replace_range(std::size_t start, std::size_t end, std::string_view text) noexcept;
    void clear_composition() noexcept;
    TextEditResult erase_range(std::size_t start, std::size_t end) noexcept;
};

}

#endif
