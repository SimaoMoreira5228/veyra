#include "software_renderer_internal.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>

namespace veyra::render_detail {
namespace {

struct PathPoint {
    double x{};
    double y{};
};

constexpr auto curve_root_iterations = 24;

PathPoint translated(LayoutPoint point, LayoutPoint offset) noexcept {
    return {
        .x = static_cast<double>(point.x) + offset.x,
        .y = static_cast<double>(point.y) + offset.y,
    };
}

PathPoint interpolate(PathPoint start, PathPoint end, double fraction) noexcept {
    return {
        .x = start.x + (end.x - start.x) * fraction,
        .y = start.y + (end.y - start.y) * fraction,
    };
}

PathPoint
evaluate_curve(std::array<PathPoint, 4> points, std::size_t degree, double fraction) noexcept {
    for (std::size_t level = degree; level > 0; --level) {
        for (std::size_t index = 0; index < level; ++index) {
            points[index] = interpolate(points[index], points[index + 1], fraction);
        }
    }

    return points[0];
}

double
evaluate_curve_y(std::array<double, 4> coordinates, std::size_t degree, double fraction) noexcept {
    for (std::size_t level = degree; level > 0; --level) {
        for (std::size_t index = 0; index < level; ++index) {
            coordinates[index] += (coordinates[index + 1] - coordinates[index]) * fraction;
        }
    }

    return coordinates[0];
}

void add_line_intersection(
    PathPoint start,
    PathPoint end,
    double sample_y,
    std::span<double> intersections,
    std::span<std::int8_t> windings,
    std::size_t& intersection_count
) noexcept {
    if ((start.y <= sample_y && sample_y < end.y) || (end.y <= sample_y && sample_y < start.y)) {
        const auto fraction = (sample_y - start.y) / (end.y - start.y);
        intersections[intersection_count] = start.x + fraction * (end.x - start.x);
        if (!windings.empty()) {
            windings[intersection_count] = static_cast<std::int8_t>(end.y > start.y ? 1 : -1);
        }
        ++intersection_count;
    }
}

void add_derivative_roots(
    double quadratic,
    double linear,
    double constant,
    std::array<double, 4>& cuts,
    std::size_t& cut_count
) noexcept {
    if (quadratic == 0.0) {
        if (linear == 0.0) {
            return;
        }

        const auto root = -constant / linear;
        if (root > 0.0 && root < 1.0) {
            cuts[cut_count++] = root;
        }
        return;
    }

    const auto discriminant = linear * linear - 4.0 * quadratic * constant;
    if (discriminant < 0.0) {
        return;
    }

    const auto square_root = std::sqrt(discriminant);
    const auto quotient = -0.5 * (linear + std::copysign(square_root, linear));
    if (quotient == 0.0) {
        return;
    }

    const auto first_root = quotient / quadratic;
    const auto second_root = constant / quotient;
    if (first_root > 0.0 && first_root < 1.0) {
        cuts[cut_count++] = first_root;
    }
    if (second_root > 0.0 && second_root < 1.0 && second_root != first_root) {
        cuts[cut_count++] = second_root;
    }
}

void append_curve_intersections(
    const std::array<PathPoint, 4>& points,
    std::size_t degree,
    double sample_y,
    std::span<double> intersections,
    std::span<std::int8_t> windings,
    std::size_t& intersection_count
) noexcept {
    std::array<double, 4> cuts{0.0, 1.0};
    auto cut_count = std::size_t{2};
    const auto y_coordinates = std::array{
        points[0].y,
        points[1].y,
        points[2].y,
        points[3].y,
    };
    if (degree == 2) {
        const auto curve_quadratic = points[0].y - 2.0 * points[1].y + points[2].y;
        const auto curve_linear = 2.0 * (points[1].y - points[0].y);
        add_derivative_roots(0.0, 2.0 * curve_quadratic, curve_linear, cuts, cut_count);
    } else {
        const auto curve_cubic = -points[0].y + 3.0 * points[1].y - 3.0 * points[2].y + points[3].y;
        const auto curve_quadratic = 3.0 * points[0].y - 6.0 * points[1].y + 3.0 * points[2].y;
        const auto curve_linear = 3.0 * (points[1].y - points[0].y);
        add_derivative_roots(
            3.0 * curve_cubic,
            2.0 * curve_quadratic,
            curve_linear,
            cuts,
            cut_count
        );
    }

    std::sort(cuts.begin(), cuts.begin() + static_cast<std::ptrdiff_t>(cut_count));
    for (std::size_t interval = 0; interval + 1 < cut_count; ++interval) {
        auto lower = cuts[interval];
        auto upper = cuts[interval + 1];
        const auto lower_y = evaluate_curve_y(y_coordinates, degree, lower);
        const auto upper_y = evaluate_curve_y(y_coordinates, degree, upper);
        const auto rising = lower_y <= sample_y && sample_y < upper_y;
        const auto falling = upper_y <= sample_y && sample_y < lower_y;
        if (!rising && !falling) {
            continue;
        }

        auto fraction = lower + (sample_y - lower_y) * (upper - lower) / (upper_y - lower_y);
        for (auto iteration = 0; iteration < curve_root_iterations; ++iteration) {
            const auto curve_y = evaluate_curve_y(y_coordinates, degree, fraction);
            if (curve_y == sample_y) {
                break;
            }
            if ((rising && curve_y < sample_y) || (falling && curve_y > sample_y)) {
                lower = fraction;
            } else {
                upper = fraction;
            }

            const auto inverse_fraction = 1.0 - fraction;
            const auto derivative = degree == 2 ? 2.0 *
                    (inverse_fraction * (y_coordinates[1] - y_coordinates[0]) +
                     fraction * (y_coordinates[2] - y_coordinates[1]))
                                                : 3.0 *
                    (inverse_fraction * inverse_fraction * (y_coordinates[1] - y_coordinates[0]) +
                     2.0 * inverse_fraction * fraction * (y_coordinates[2] - y_coordinates[1]) +
                     fraction * fraction * (y_coordinates[3] - y_coordinates[2]));
            auto next_fraction = fraction - (curve_y - sample_y) / derivative;
            if (!std::isfinite(next_fraction) || next_fraction <= lower || next_fraction >= upper) {
                next_fraction = (lower + upper) * 0.5;
            }
            if (next_fraction == fraction) {
                break;
            }
            fraction = next_fraction;
        }

        intersections[intersection_count] = evaluate_curve(points, degree, fraction).x;
        if (!windings.empty()) {
            windings[intersection_count] = static_cast<std::int8_t>(rising ? 1 : -1);
        }
        ++intersection_count;
    }
}

void swap_intersections(
    std::span<double> intersections,
    std::span<std::int8_t> windings,
    std::size_t left,
    std::size_t right
) noexcept {
    std::swap(intersections[left], intersections[right]);
    std::swap(windings[left], windings[right]);
}

void sift_intersections(
    std::span<double> intersections,
    std::span<std::int8_t> windings,
    std::size_t root,
    std::size_t end
) noexcept {
    while (root < end / 2) {
        auto child = root * 2 + 1;
        if (child + 1 < end && intersections[child] < intersections[child + 1]) {
            ++child;
        }
        if (intersections[root] >= intersections[child]) {
            return;
        }

        swap_intersections(intersections, windings, root, child);
        root = child;
    }
}

void sort_winding_intersections(
    std::span<double> intersections,
    std::span<std::int8_t> windings,
    std::size_t count
) noexcept {
    for (auto start = count / 2; start > 0; --start) {
        sift_intersections(intersections, windings, start - 1, count);
    }

    for (auto end = count; end > 1; --end) {
        swap_intersections(intersections, windings, 0, end - 1);
        sift_intersections(intersections, windings, 0, end - 1);
    }
}

void fill_span(
    std::size_t y,
    std::size_t left,
    std::size_t right,
    DisplayColor color,
    SoftwareSurface surface
) noexcept {
    if (left >= right || color.alpha == 0) {
        return;
    }

    const auto row_offset = y * surface.stride;
    if (color.alpha == 255) {
        const auto pixel = SoftwarePixel{
            .red = color.red,
            .green = color.green,
            .blue = color.blue,
            .alpha = 255,
        };
        std::fill_n(surface.pixels.data() + row_offset + left, right - left, pixel);
        return;
    }

    for (auto x = left; x < right; ++x) {
        auto& destination = surface.pixels[row_offset + x];
        destination = composite(color, destination);
    }
}

void fill_intersection_span(
    std::size_t y,
    double left_x,
    double right_x,
    SoftwareClipRect clip,
    std::size_t surface_width,
    void* context,
    PathSpanEmit emit
) noexcept {
    const auto width = static_cast<double>(surface_width);
    const auto left_position = left_x - 0.5;
    const auto right_position = right_x - 0.5;
    auto left = ceil_coordinate(std::clamp(left_position, 0.0, width), surface_width);
    auto right = ceil_coordinate(std::clamp(right_position, 0.0, width), surface_width);
    left = std::max(left, clip.left);
    right = std::min(right, clip.right);
    if (left < right) {
        emit(context, PathPixelSpan{.y = y, .left = left, .right = right});
    }
}

}

namespace {

bool valid_point(LayoutPoint point) noexcept {
    return std::isfinite(point.x) && std::isfinite(point.y);
}

bool valid_fill_rule(SoftwarePathFillRule rule) noexcept {
    return rule == SoftwarePathFillRule::even_odd || rule == SoftwarePathFillRule::non_zero;
}

bool translated_point_is_finite(LayoutPoint point, LayoutPoint translation) noexcept {
    return std::isfinite(point.x + translation.x) && std::isfinite(point.y + translation.y);
}

bool add_capacity(std::size_t amount, std::size_t& capacity) noexcept {
    if (amount > std::numeric_limits<std::size_t>::max() - capacity) {
        return false;
    }

    capacity += amount;
    return true;
}

bool valid_segment(const SoftwarePathSegment& segment) noexcept {
    if (const auto* line = std::get_if<SoftwareLineTo>(&segment)) {
        return valid_point(line->end);
    }
    if (const auto* quadratic = std::get_if<SoftwareQuadraticTo>(&segment)) {
        return valid_point(quadratic->control) && valid_point(quadratic->end);
    }
    const auto* cubic = std::get_if<SoftwareCubicTo>(&segment);
    return cubic != nullptr && valid_point(cubic->control_1) && valid_point(cubic->control_2) &&
        valid_point(cubic->end);
}

std::size_t segment_capacity(const SoftwarePathSegment& segment) noexcept {
    if (std::holds_alternative<SoftwareLineTo>(segment)) {
        return 1;
    }
    if (std::holds_alternative<SoftwareQuadraticTo>(segment)) {
        return 2;
    }
    return 3;
}

void include_y(
    LayoutPoint point,
    LayoutPoint translation,
    double& minimum_y,
    double& maximum_y
) noexcept {
    const auto y = static_cast<double>(point.y) + translation.y;
    minimum_y = std::min(minimum_y, y);
    maximum_y = std::max(maximum_y, y);
}

}

bool valid_path(
    const FillPath& command,
    std::size_t& intersection_capacity,
    std::size_t& winding_capacity
) noexcept {
    if (command.contours.empty() || !valid_fill_rule(command.fill_rule)) {
        return false;
    }

    intersection_capacity = 0;
    for (const auto& contour : command.contours) {
        if (!valid_point(contour.start) || contour.segments.empty()) {
            return false;
        }

        auto contour_capacity = std::size_t{1};
        for (const auto& segment : contour.segments) {
            if (!valid_segment(segment) ||
                !add_capacity(segment_capacity(segment), contour_capacity)) {
                return false;
            }
        }
        if (!add_capacity(contour_capacity, intersection_capacity)) {
            return false;
        }
    }

    winding_capacity =
        command.fill_rule == SoftwarePathFillRule::non_zero ? intersection_capacity : 0;
    return true;
}

bool valid_translated_path(const FillPath& command, LayoutPoint translation) noexcept {
    for (const auto& contour : command.contours) {
        if (!translated_point_is_finite(contour.start, translation)) {
            return false;
        }

        for (const auto& segment : contour.segments) {
            if (const auto* line = std::get_if<SoftwareLineTo>(&segment)) {
                if (!translated_point_is_finite(line->end, translation)) {
                    return false;
                }
                continue;
            }
            if (const auto* quadratic = std::get_if<SoftwareQuadraticTo>(&segment)) {
                if (!translated_point_is_finite(quadratic->control, translation) ||
                    !translated_point_is_finite(quadratic->end, translation)) {
                    return false;
                }
                continue;
            }

            const auto* cubic = std::get_if<SoftwareCubicTo>(&segment);
            if (cubic == nullptr || !translated_point_is_finite(cubic->control_1, translation) ||
                !translated_point_is_finite(cubic->control_2, translation) ||
                !translated_point_is_finite(cubic->end, translation)) {
                return false;
            }
        }
    }

    return true;
}

void fill_path_spans(
    const FillPath& command,
    LayoutPoint translation,
    std::size_t surface_width,
    std::size_t surface_height,
    SoftwareClipRect clip,
    std::span<double> intersections,
    std::span<std::int8_t> windings,
    void* context,
    PathSpanEmit emit
) noexcept {
    const auto path_windings =
        command.fill_rule == SoftwarePathFillRule::non_zero ? windings : std::span<std::int8_t>{};

    auto minimum_y = std::numeric_limits<double>::infinity();
    auto maximum_y = -std::numeric_limits<double>::infinity();
    for (const auto& contour : command.contours) {
        include_y(contour.start, translation, minimum_y, maximum_y);
        for (const auto& segment : contour.segments) {
            if (const auto* line = std::get_if<SoftwareLineTo>(&segment)) {
                include_y(line->end, translation, minimum_y, maximum_y);
            } else if (const auto* quadratic = std::get_if<SoftwareQuadraticTo>(&segment)) {
                include_y(quadratic->control, translation, minimum_y, maximum_y);
                include_y(quadratic->end, translation, minimum_y, maximum_y);
            } else {
                const auto* cubic = std::get_if<SoftwareCubicTo>(&segment);
                if (cubic == nullptr) {
                    return;
                }
                include_y(cubic->control_1, translation, minimum_y, maximum_y);
                include_y(cubic->control_2, translation, minimum_y, maximum_y);
                include_y(cubic->end, translation, minimum_y, maximum_y);
            }
        }
    }

    const auto height_limit = static_cast<double>(surface_height);
    auto top = floor_coordinate(std::clamp(minimum_y, 0.0, height_limit), surface_height);
    auto bottom = ceil_coordinate(std::clamp(maximum_y, 0.0, height_limit), surface_height);
    top = std::max(top, clip.top);
    bottom = std::min(bottom, clip.bottom);
    if (top >= bottom) {
        return;
    }

    for (auto y = top; y < bottom; ++y) {
        const auto sample_y = static_cast<double>(y) + 0.5;
        std::size_t intersection_count = 0;
        for (const auto& contour : command.contours) {
            auto point = translated(contour.start, translation);
            for (const auto& segment : contour.segments) {
                if (const auto* line = std::get_if<SoftwareLineTo>(&segment)) {
                    const auto end = translated(line->end, translation);
                    add_line_intersection(
                        point,
                        end,
                        sample_y,
                        intersections,
                        path_windings,
                        intersection_count
                    );
                    point = end;
                    continue;
                }

                if (const auto* quadratic = std::get_if<SoftwareQuadraticTo>(&segment)) {
                    const auto control = translated(quadratic->control, translation);
                    const auto end = translated(quadratic->end, translation);
                    const auto curve = std::array<PathPoint, 4>{point, control, end, {}};
                    append_curve_intersections(
                        curve,
                        2,
                        sample_y,
                        intersections,
                        path_windings,
                        intersection_count
                    );
                    point = end;
                    continue;
                }

                const auto* cubic = std::get_if<SoftwareCubicTo>(&segment);
                if (cubic == nullptr) {
                    return;
                }
                const auto control_1 = translated(cubic->control_1, translation);
                const auto control_2 = translated(cubic->control_2, translation);
                const auto end = translated(cubic->end, translation);
                const auto curve = std::array<PathPoint, 4>{point, control_1, control_2, end};
                append_curve_intersections(
                    curve,
                    3,
                    sample_y,
                    intersections,
                    path_windings,
                    intersection_count
                );
                point = end;
            }
            add_line_intersection(
                point,
                translated(contour.start, translation),
                sample_y,
                intersections,
                path_windings,
                intersection_count
            );
        }

        if (command.fill_rule == SoftwarePathFillRule::even_odd) {
            std::sort(
                intersections.begin(),
                intersections.begin() + static_cast<std::ptrdiff_t>(intersection_count)
            );
            for (std::size_t index = 0; index + 1 < intersection_count; index += 2) {
                fill_intersection_span(
                    y,
                    intersections[index],
                    intersections[index + 1],
                    clip,
                    surface_width,
                    context,
                    emit
                );
            }
            continue;
        }

        sort_winding_intersections(intersections, windings, intersection_count);
        auto winding = 0;
        auto previous_x = 0.0;
        auto intersection_index = std::size_t{0};
        while (intersection_index < intersection_count) {
            const auto current_x = intersections[intersection_index];
            if (winding != 0) {
                fill_intersection_span(
                    y,
                    previous_x,
                    current_x,
                    clip,
                    surface_width,
                    context,
                    emit
                );
            }

            do {
                winding += windings[intersection_index];
                ++intersection_index;
            } while (intersection_index < intersection_count &&
                     intersections[intersection_index] == current_x);
            previous_x = current_x;
        }
    }
}

namespace {

struct FillSpanSink {
    DisplayColor color;
    SoftwareSurface surface;
};

void emit_fill_span(void* context, PathPixelSpan span) noexcept {
    const auto* sink = static_cast<const FillSpanSink*>(context);
    fill_span(span.y, span.left, span.right, sink->color, sink->surface);
}

}

void draw_path(
    const FillPath& command,
    LayoutPoint translation,
    SoftwareSurface surface,
    SoftwareClipRect clip,
    std::span<double> intersections,
    std::span<std::int8_t> windings
) noexcept {
    if (command.color.alpha == 0) {
        return;
    }

    auto sink = FillSpanSink{.color = command.color, .surface = surface};
    fill_path_spans(
        command,
        translation,
        surface.width,
        surface.height,
        clip,
        intersections,
        windings,
        &sink,
        emit_fill_span
    );
}

}
