#ifndef VEYRA_TESTS_SCROLL_SURFACE_HELPERS_HPP
#define VEYRA_TESTS_SCROLL_SURFACE_HELPERS_HPP

#include "veyra/render/software_renderer.hpp"

#include <cstddef>
#include <span>

inline bool pixel_equal(const veyra::SoftwarePixel& left, const veyra::SoftwarePixel& right) {
    return left.red == right.red && left.green == right.green && left.blue == right.blue &&
        left.alpha == right.alpha;
}

inline bool buffers_equal(
    std::span<const veyra::SoftwarePixel> left,
    std::span<const veyra::SoftwarePixel> right
) {
    if (left.size() != right.size()) {
        return false;
    }

    for (std::size_t index = 0; index < left.size(); ++index) {
        if (!pixel_equal(left[index], right[index])) {
            return false;
        }
    }

    return true;
}

#endif
