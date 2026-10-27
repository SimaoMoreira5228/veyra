#ifndef VEYRA_TEXT_GRAPHEME_NAVIGATION_HPP
#define VEYRA_TEXT_GRAPHEME_NAVIGATION_HPP

#include "veyra/text/editing/text_editor_buffer.hpp"

namespace veyra {

TextEditResult move_cursor_by_grapheme(
    TextEditorBuffer& buffer,
    TextCursorDirection direction,
    bool extend_selection = false
) noexcept;

TextEditResult erase_grapheme(TextEditorBuffer& buffer, TextCursorDirection direction) noexcept;

}

#endif
