#ifndef VEYRA_LAYOUT_IMAGE_FIT_HPP
#define VEYRA_LAYOUT_IMAGE_FIT_HPP

#include "veyra/layout/geometry.hpp"

#include <cstdint>

namespace veyra {

enum class ImageFitMode : std::uint8_t {
    fill,
    contain,
    cover,
    none,
    scale_down,
};

struct ImageSize {
    float width{};
    float height{};
};

struct ImagePlacement {
    LayoutRect source;
    LayoutRect destination;
};

enum class ImageFitResult : std::uint8_t {
    complete,
    empty,
    invalid_input,
};

ImageFitResult fit_image(
    LayoutRect bounds,
    ImageSize intrinsic_size,
    ImageFitMode mode,
    ImagePlacement& placement
) noexcept;

}

#endif
