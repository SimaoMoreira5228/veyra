#ifndef VEYRA_WIDGETS_BASIC_TOGGLE_HPP
#define VEYRA_WIDGETS_BASIC_TOGGLE_HPP

#include "veyra/behaviors/control/toggle.hpp"
#include "veyra/style/theme/theme.hpp"

namespace veyra {

struct CheckboxAppearance {
    Srgba8 box;
    Srgba8 mark;
    LogicalPx box_size;
    LogicalPx corner_radius;
};

struct SwitchAppearance {
    Srgba8 track;
    Srgba8 thumb;
    LogicalPx track_width;
    LogicalPx track_height;
    LogicalPx corner_radius;
};

CheckboxAppearance checkbox_appearance(const Theme& theme, ToggleState state) noexcept;
SwitchAppearance switch_appearance(const Theme& theme, ToggleState state) noexcept;

}
#endif
