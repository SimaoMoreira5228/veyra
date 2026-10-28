#ifndef VEYRA_FOUNDATION_GLYPH_HPP
#define VEYRA_FOUNDATION_GLYPH_HPP

#include "veyra/foundation/image.hpp"

#include <cstdint>

namespace veyra {

enum class GlyphRasterMode : std::uint8_t {
    grayscale,
    monochrome,
};

struct GlyphMaskKey {
    std::uint64_t face_id{};
    std::uint64_t variation_id{};
    std::uint32_t glyph_id{};
    std::uint32_t pixel_size{};
    std::uint32_t scale_x_16_16{65'536};
    std::uint32_t scale_y_16_16{65'536};
    GlyphRasterMode mode{GlyphRasterMode::grayscale};

    friend constexpr bool operator==(const GlyphMaskKey&, const GlyphMaskKey&) = default;
};

struct GlyphMask {
    SoftwareMask mask;
    std::int32_t bearing_x{};
    std::int32_t bearing_y{};
};

}

namespace veyra::foundation {
using ::veyra::GlyphMask;
using ::veyra::GlyphMaskKey;
using ::veyra::GlyphRasterMode;
}

#endif
