#ifndef VEYRA_TEXT_DIRECTION_HPP
#define VEYRA_TEXT_DIRECTION_HPP

#include <cstdint>

namespace veyra {

enum class TextDirection : std::uint8_t {
    left_to_right,
    right_to_left,
    top_to_bottom,
    bottom_to_top,
};

}

#endif
