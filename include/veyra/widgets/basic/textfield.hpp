#ifndef VEYRA_WIDGETS_BASIC_TEXTFIELD_HPP
#define VEYRA_WIDGETS_BASIC_TEXTFIELD_HPP

#include "veyra/behaviors/editable_text.hpp"
#include "veyra/style/theme.hpp"

namespace veyra {

struct TextFieldAppearance {
    Srgba8 box_fill;
    Srgba8 border;
    Srgba8 caret;
    Srgba8 placeholder;
    Srgba8 text;
    LogicalPx corner_radius;
    LogicalPx horizontal_padding;
};

TextFieldAppearance textfield_appearance(const Theme& theme, EditableTextState state) noexcept;

}

#endif
