#ifndef VEYRA_WIDGETS_BASIC_RADIO_HPP
#define VEYRA_WIDGETS_BASIC_RADIO_HPP

#include "veyra/behaviors/input/radiogroup.hpp"
#include "veyra/style/theme.hpp"

namespace veyra {

struct RadioAppearance {
    Srgba8 ring;
    Srgba8 dot;
    LogicalPx ring_diameter;
    LogicalPx dot_diameter;
};

RadioAppearance radio_appearance(const Theme& theme, RadioGroupState state, bool selected) noexcept;

}

#endif
