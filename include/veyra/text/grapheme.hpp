#ifndef VEYRA_TEXT_GRAPHEME_NAVIGATION_HPP
#define VEYRA_TEXT_GRAPHEME_NAVIGATION_HPP

#include "veyra/text/text_editor_buffer.hpp"

namespace veyra {

TextEditResult
move_cursor_by_grapheme(TextEditorBuffer& buffer, TextCursorDirection direction) noexcept;

TextEditResult erase_grapheme(TextEditorBuffer& buffer, TextCursorDirection direction) noexcept;

}

#endif
