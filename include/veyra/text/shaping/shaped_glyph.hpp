#ifndef VEYRA_TEXT_SHAPED_GLYPH_HPP
#define VEYRA_TEXT_SHAPED_GLYPH_HPP

#include <cstdint>

namespace veyra {

struct ShapedGlyph {
    std::uint32_t id{};
    std::uint32_t cluster{};
    std::int32_t x_advance{};
    std::int32_t y_advance{};
    std::int32_t x_offset{};
    std::int32_t y_offset{};
};

}

namespace veyra::text {
using ::veyra::ShapedGlyph;
}

namespace veyra::text::shaping {
using ::veyra::ShapedGlyph;
}

#endif
