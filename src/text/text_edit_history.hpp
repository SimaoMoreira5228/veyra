#ifndef VEYRA_TEXT_TEXT_EDIT_HISTORY_HPP
#define VEYRA_TEXT_TEXT_EDIT_HISTORY_HPP

#include "veyra/text/text_editor_buffer.hpp"

namespace veyra {

class TextEditorHistory {
  public:
    static TextEditResult record(
        TextEditorBuffer& buffer,
        std::size_t start,
        std::size_t end,
        std::string_view inserted_text
    ) noexcept;
    static TextEditResult undo(TextEditorBuffer& buffer) noexcept;
    static TextEditResult redo(TextEditorBuffer& buffer) noexcept;

  private:
    struct Record {
        std::size_t start;
        std::size_t cursor_before;
        std::size_t selection_anchor_before;
        std::size_t removed_length;
        std::size_t inserted_length;
    };

    static std::size_t
    advance(const TextEditorBuffer& buffer, std::size_t offset, std::size_t amount) noexcept;
    static std::size_t
    retreat(const TextEditorBuffer& buffer, std::size_t offset, std::size_t amount) noexcept;
    static void write(
        TextEditorBuffer& buffer,
        std::size_t offset,
        const void* source,
        std::size_t length
    ) noexcept;
    static void read(
        const TextEditorBuffer& buffer,
        std::size_t offset,
        void* output,
        std::size_t length
    ) noexcept;
    static void copy_text_range(
        TextEditorBuffer& buffer,
        std::size_t start,
        std::size_t length,
        std::size_t history_offset
    ) noexcept;
    static void replace_range(
        TextEditorBuffer& buffer,
        std::size_t start,
        std::size_t end,
        std::size_t history_offset,
        std::size_t length
    ) noexcept;
};

}

#endif
