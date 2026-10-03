#ifndef VEYRA_TEXT_TEXT_EDITOR_BUFFER_HPP
#define VEYRA_TEXT_TEXT_EDITOR_BUFFER_HPP

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace veyra {

enum class TextEditResult : std::uint8_t {
    complete,
    invalid_utf8,
    invalid_cursor,
    invalid_direction,
    invalid_state,
    insufficient_capacity,
    overlapping_input,
    overlapping_output,
};

enum class TextCursorDirection : std::uint8_t {
    backward,
    forward,
};

struct TextSelection {
    std::size_t start{};
    std::size_t length{};
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
    TextEditorBuffer(std::span<char> storage, std::string_view initial_text) noexcept;
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
    TextEditResult set_cursor(std::size_t byte_offset, bool extend_selection = false) noexcept;
    TextEditResult
    move_cursor(TextCursorDirection direction, bool extend_selection = false) noexcept;
    void select_all() noexcept;
    TextEditResult insert(std::string_view text) noexcept;
    TextEditResult erase_backward() noexcept;
    TextEditResult erase_forward() noexcept;
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
    std::span<char> storage_;
    std::size_t gap_start_{};
    std::size_t gap_end_{};
    std::size_t selection_anchor_{};
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
    TextEditResult replace_selection(std::string_view text) noexcept;
    void replace_range(std::size_t start, std::size_t end, std::string_view text) noexcept;
    void clear_composition() noexcept;
    void erase_range(std::size_t start, std::size_t end) noexcept;
};

}

#endif
