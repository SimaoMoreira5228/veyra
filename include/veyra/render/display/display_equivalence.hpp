#ifndef VEYRA_RENDER_DISPLAY_EQUIVALENCE_HPP
#define VEYRA_RENDER_DISPLAY_EQUIVALENCE_HPP

#include "veyra/render/cpu/software_renderer.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace veyra {

bool same_float_bits(float left, float right) noexcept;
bool same_point(LayoutPoint left, LayoutPoint right) noexcept;
bool same_rect(LayoutRect left, LayoutRect right) noexcept;
bool same_points(std::span<const LayoutPoint> left, std::span<const LayoutPoint> right) noexcept;
bool same_segment(const SoftwarePathSegment& left, const SoftwarePathSegment& right) noexcept;
bool same_segments(
    std::span<const SoftwarePathSegment> left,
    std::span<const SoftwarePathSegment> right
) noexcept;
bool same_contour(const SoftwarePathContour& left, const SoftwarePathContour& right) noexcept;
bool same_contours(
    std::span<const SoftwarePathContour> left,
    std::span<const SoftwarePathContour> right
) noexcept;
bool same_command(const DisplayCommand& left, const DisplayCommand& right) noexcept;

bool equivalent_display_lists(
    std::span<const DisplayCommand> left,
    std::span<const DisplayCommand> right,
    std::size_t& first_different
) noexcept;

bool equivalent_display_lists(
    std::span<const DisplayCommand> left,
    std::span<const DisplayCommand> right
) noexcept;

}

namespace veyra::render {
using ::veyra::equivalent_display_lists;
using ::veyra::same_command;
using ::veyra::same_contour;
using ::veyra::same_contours;
using ::veyra::same_float_bits;
using ::veyra::same_point;
using ::veyra::same_points;
using ::veyra::same_rect;
using ::veyra::same_segment;
using ::veyra::same_segments;
}

namespace veyra::render::display {
using ::veyra::equivalent_display_lists;
using ::veyra::same_command;
using ::veyra::same_contour;
using ::veyra::same_contours;
using ::veyra::same_float_bits;
using ::veyra::same_point;
using ::veyra::same_points;
using ::veyra::same_rect;
using ::veyra::same_segment;
using ::veyra::same_segments;
}

#endif
