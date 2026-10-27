#include "veyra/render/display/display_equivalence.hpp"

#include "veyra/render/cpu/software_renderer.hpp"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <variant>

namespace veyra {

bool same_float_bits(float left, float right) noexcept {
    return std::bit_cast<std::uint32_t>(left) == std::bit_cast<std::uint32_t>(right);
}

bool same_point(LayoutPoint left, LayoutPoint right) noexcept {
    return same_float_bits(left.x, right.x) && same_float_bits(left.y, right.y);
}

bool same_rect(LayoutRect left, LayoutRect right) noexcept {
    return same_float_bits(left.x, right.x) && same_float_bits(left.y, right.y) &&
        same_float_bits(left.width, right.width) && same_float_bits(left.height, right.height);
}

bool same_points(std::span<const LayoutPoint> left, std::span<const LayoutPoint> right) noexcept {
    if (left.size() != right.size()) {
        return false;
    }

    for (std::size_t index = 0; index < left.size(); ++index) {
        if (!same_point(left[index], right[index])) {
            return false;
        }
    }

    return true;
}

bool same_segment(const SoftwarePathSegment& left, const SoftwarePathSegment& right) noexcept {
    if (left.index() != right.index()) {
        return false;
    }

    if (const auto* value = std::get_if<SoftwareLineTo>(&left)) {
        return same_point(value->end, std::get<SoftwareLineTo>(right).end);
    }

    if (const auto* value = std::get_if<SoftwareQuadraticTo>(&left)) {
        const auto& other = std::get<SoftwareQuadraticTo>(right);

        return same_point(value->control, other.control) && same_point(value->end, other.end);
    }

    if (const auto* value = std::get_if<SoftwareCubicTo>(&left)) {
        const auto& other = std::get<SoftwareCubicTo>(right);

        return same_point(value->control_1, other.control_1) &&
            same_point(value->control_2, other.control_2) && same_point(value->end, other.end);
    }

    return false;
}

bool same_segments(
    std::span<const SoftwarePathSegment> left,
    std::span<const SoftwarePathSegment> right
) noexcept {
    if (left.size() != right.size()) {
        return false;
    }

    for (std::size_t index = 0; index < left.size(); ++index) {
        if (!same_segment(left[index], right[index])) {
            return false;
        }
    }

    return true;
}

bool same_contour(const SoftwarePathContour& left, const SoftwarePathContour& right) noexcept {
    return same_point(left.start, right.start) && same_segments(left.segments, right.segments);
}

bool same_contours(
    std::span<const SoftwarePathContour> left,
    std::span<const SoftwarePathContour> right
) noexcept {
    if (left.size() != right.size()) {
        return false;
    }

    for (std::size_t index = 0; index < left.size(); ++index) {
        if (!same_contour(left[index], right[index])) {
            return false;
        }
    }

    return true;
}

bool same_command(const DisplayCommand& left, const DisplayCommand& right) noexcept {
    if (left.index() != right.index()) {
        return false;
    }

    if (const auto* value = std::get_if<FillRect>(&left)) {
        const auto& other = std::get<FillRect>(right);

        return same_rect(value->bounds, other.bounds) && value->color == other.color;
    }

    if (const auto* value = std::get_if<FillRoundedRect>(&left)) {
        const auto& other = std::get<FillRoundedRect>(right);

        return same_rect(value->bounds, other.bounds) &&
            same_float_bits(value->radius, other.radius) && value->color == other.color;
    }

    if (const auto* value = std::get_if<StrokeRect>(&left)) {
        const auto& other = std::get<StrokeRect>(right);

        return same_rect(value->bounds, other.bounds) &&
            same_float_bits(value->thickness, other.thickness) && value->color == other.color;
    }

    if (const auto* value = std::get_if<StrokeRoundedRect>(&left)) {
        const auto& other = std::get<StrokeRoundedRect>(right);

        return same_rect(value->bounds, other.bounds) &&
            same_float_bits(value->radius, other.radius) &&
            same_float_bits(value->thickness, other.thickness) && value->color == other.color;
    }

    if (const auto* value = std::get_if<StrokeLine>(&left)) {
        const auto& other = std::get<StrokeLine>(right);

        return same_point(value->start, other.start) && same_point(value->end, other.end) &&
            same_float_bits(value->thickness, other.thickness) && value->color == other.color;
    }

    if (const auto* value = std::get_if<StrokePolyline>(&left)) {
        const auto& other = std::get<StrokePolyline>(right);

        return same_points(value->points, other.points) &&
            same_float_bits(value->thickness, other.thickness) && value->color == other.color &&
            value->closed == other.closed;
    }

    if (const auto* value = std::get_if<DrawMask>(&left)) {
        const auto& other = std::get<DrawMask>(right);

        return same_float_bits(value->x, other.x) && same_float_bits(value->y, other.y) &&
            value->mask == other.mask && value->color == other.color;
    }

    if (const auto* value = std::get_if<DrawGlyph>(&left)) {
        const auto& other = std::get<DrawGlyph>(right);

        return value->atlas == other.atlas && value->u == other.u && value->v == other.v &&
            value->width == other.width && value->height == other.height &&
            same_float_bits(value->x, other.x) && same_float_bits(value->y, other.y) &&
            value->color == other.color;
    }

    if (const auto* value = std::get_if<FillPath>(&left)) {
        const auto& other = std::get<FillPath>(right);

        return same_contours(value->contours, other.contours) && value->color == other.color &&
            value->fill_rule == other.fill_rule;
    }

    if (const auto* value = std::get_if<DrawImage>(&left)) {
        const auto& other = std::get<DrawImage>(right);

        return same_rect(value->source, other.source) &&
            same_rect(value->destination, other.destination) && value->image == other.image &&
            value->sampling == other.sampling && value->opacity == other.opacity;
    }

    if (const auto* value = std::get_if<PushTranslation>(&left)) {
        return same_point(value->offset, std::get<PushTranslation>(right).offset);
    }

    if (std::get_if<PopTranslation>(&left) != nullptr) {
        return true;
    }

    if (const auto* value = std::get_if<PushClip>(&left)) {
        const auto& other = std::get<PushClip>(right);

        return same_rect(value->bounds, other.bounds) && value->clip_id == other.clip_id;
    }

    return std::get_if<PopClip>(&left) != nullptr;
}

bool equivalent_display_lists(
    std::span<const DisplayCommand> left,
    std::span<const DisplayCommand> right,
    std::size_t& first_different
) noexcept {
    const auto shared = left.size() < right.size() ? left.size() : right.size();

    for (std::size_t index = 0; index < shared; ++index) {
        if (!same_command(left[index], right[index])) {
            first_different = index;

            return false;
        }
    }

    if (left.size() != right.size()) {
        first_different = shared;

        return false;
    }

    first_different = left.size();

    return true;
}

bool equivalent_display_lists(
    std::span<const DisplayCommand> left,
    std::span<const DisplayCommand> right
) noexcept {
    auto first_different = std::size_t{0};

    return equivalent_display_lists(left, right, first_different);
}

}
