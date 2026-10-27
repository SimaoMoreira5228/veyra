#ifndef VEYRA_WIDGETS_BASIC_BUTTON_HPP
#define VEYRA_WIDGETS_BASIC_BUTTON_HPP

#include "veyra/behaviors/control/pressable.hpp"
#include "veyra/style/theme/theme.hpp"

namespace veyra {

struct ButtonAppearance {
    Srgba8 background;
    Srgba8 foreground;
    LogicalPx horizontal_padding;
    LogicalPx vertical_padding;
    LogicalPx corner_radius;
};

ButtonAppearance button_appearance(const Theme& theme, PressableState state) noexcept;
ButtonAppearance
button_appearance(const Theme& theme, PressableState state, PressableTint tint) noexcept;

}

#endif
