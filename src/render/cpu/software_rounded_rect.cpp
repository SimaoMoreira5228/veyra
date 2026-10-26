#include "software_renderer_internal.hpp"

#include <algorithm>
#include <cmath>

namespace veyra::render_detail {
namespace {

std::uint8_t coverage_alpha(std::uint8_t alpha, double coverage) noexcept {
    const auto scaled = static_cast<double>(alpha) * coverage;
    return static_cast<std::uint8_t>(std::lround(scaled));
}

void blend_coverage(DisplayColor color, double coverage, SoftwarePixel& destination) noexcept {
    color.alpha = coverage_alpha(color.alpha, coverage);
    if (color.alpha != 0) {
        destination = composite(color, destination);
    }
}

void blend_coverage_span(
    DisplayColor color,
    double coverage,
    std::span<SoftwarePixel> pixels
) noexcept {
    if (coverage == 0.0 || color.alpha == 0 || pixels.empty()) {
        return;
    }
    if (coverage == 1.0 && color.alpha == 255) {
        const auto pixel = SoftwarePixel{
            .red = color.red,
            .green = color.green,
            .blue = color.blue,
            .alpha = 255,
        };
        std::fill(pixels.begin(), pixels.end(), pixel);
        return;
    }
    for (auto& destination : pixels) {
        blend_coverage(color, coverage, destination);
    }
}

}

void draw_rounded_rect(
    const FillRoundedRect& command,
    SoftwareSurface surface,
    SoftwareClipRect clip
) noexcept {
    const auto& bounds = command.bounds;
    if (bounds.width == 0.0F || bounds.height == 0.0F || command.color.alpha == 0) {
        return;
    }

    const auto pixels = intersect(pixel_bounds(bounds, surface.width, surface.height), clip);
    if (pixels.left >= pixels.right || pixels.top >= pixels.bottom) {
        return;
    }

    const auto radius = std::min(
        static_cast<double>(command.radius),
        std::min(static_cast<double>(bounds.width), static_cast<double>(bounds.height)) * 0.5
    );
    if (radius == 0.0) {
        for (auto y = pixels.top; y < pixels.bottom; ++y) {
            const auto row = y * surface.stride;
            for (auto x = pixels.left; x < pixels.right; ++x) {
                surface.pixels[row + x] = composite(command.color, surface.pixels[row + x]);
            }
        }
        return;
    }

    const auto half_width = static_cast<double>(bounds.width) * 0.5;
    const auto half_height = static_cast<double>(bounds.height) * 0.5;
    const auto center_x = static_cast<double>(bounds.x) + half_width;
    const auto center_y = static_cast<double>(bounds.y) + half_height;
    const auto straight_x = half_width - radius;
    const auto straight_y = half_height - radius;
    for (auto y = pixels.top; y < pixels.bottom; ++y) {
        const auto row = y * surface.stride;
        const auto point_y = std::abs(static_cast<double>(y) + 0.5 - center_y) - straight_y;
        const auto fully_inside = point_y <= radius - 0.5;
        const auto center_extent = point_y > 0.0 ? straight_x
            : fully_inside                       ? half_width - 0.5
                                                 : straight_x + point_y;
        auto span_left = pixels.left;
        auto span_right = pixels.left;
        auto has_center_span = false;
        if (center_extent >= 0.0) {
            const auto left = center_x - center_extent;
            const auto right = center_x + center_extent;
            span_left = static_cast<std::size_t>(std::clamp(
                std::ceil(left - 0.5),
                static_cast<double>(pixels.left),
                static_cast<double>(pixels.right)
            ));
            span_right = static_cast<std::size_t>(std::clamp(
                std::floor(right - 0.5) + 1.0,
                static_cast<double>(span_left),
                static_cast<double>(pixels.right)
            ));
            has_center_span = span_left < span_right;
            const auto distance = point_y - radius;
            const auto coverage = fully_inside ? 1.0 : std::clamp(0.5 - distance, 0.0, 1.0);
            blend_coverage_span(
                command.color,
                coverage,
                surface.pixels.subspan(row + span_left, span_right - span_left)
            );
        }

        auto corner_extent = center_extent;
        if (point_y <= 0.0) {
            corner_extent = half_width + 0.5;
        } else if (point_y <= radius + 0.5) {
            const auto corner_reach =
                std::sqrt(std::max(0.0, (radius + 0.5) * (radius + 0.5) - point_y * point_y));
            corner_extent = straight_x + corner_reach;
        }

        const auto paint_corners = [&](std::size_t first, std::size_t last) {
            for (auto x = first; x < last; ++x) {
                const auto point_x = std::abs(static_cast<double>(x) + 0.5 - center_x) - straight_x;
                const auto distance = point_y <= 0.0 ? std::max(point_x, point_y) - radius
                                                     : std::hypot(point_x, point_y) - radius;
                const auto coverage = std::clamp(0.5 - distance, 0.0, 1.0);
                if (coverage == 0.0) {
                    continue;
                }

                blend_coverage(command.color, coverage, surface.pixels[row + x]);
            }
        };
        if (corner_extent > center_extent) {
            const auto left_first = static_cast<std::size_t>(std::clamp(
                std::ceil(center_x - corner_extent - 0.5),
                static_cast<double>(pixels.left),
                static_cast<double>(pixels.right)
            ));
            const auto left_last = static_cast<std::size_t>(std::clamp(
                std::floor(center_x - center_extent - 0.5) + 1.0,
                static_cast<double>(left_first),
                static_cast<double>(pixels.right)
            ));
            const auto right_first = static_cast<std::size_t>(std::clamp(
                std::ceil(center_x + center_extent - 0.5),
                static_cast<double>(pixels.left),
                static_cast<double>(pixels.right)
            ));
            const auto right_last = static_cast<std::size_t>(std::clamp(
                std::floor(center_x + corner_extent - 0.5) + 1.0,
                static_cast<double>(right_first),
                static_cast<double>(pixels.right)
            ));
            if (has_center_span) {
                paint_corners(left_first, std::min(left_last, span_left));
                paint_corners(std::max(right_first, span_right), right_last);
            } else if (left_last > right_first) {
                paint_corners(left_first, std::max(left_last, right_last));
            } else {
                paint_corners(left_first, left_last);
                paint_corners(right_first, right_last);
            }
        }
    }
}

void stroke_rounded_rect(
    const StrokeRoundedRect& command,
    SoftwareSurface surface,
    SoftwareClipRect clip
) noexcept {
    const auto& bounds = command.bounds;
    const auto minimum_dimension =
        std::min(static_cast<double>(bounds.width), static_cast<double>(bounds.height));
    if (minimum_dimension == 0.0 || command.thickness == 0.0 || command.color.alpha == 0) {
        return;
    }

    const auto thickness = std::min(static_cast<double>(command.thickness), minimum_dimension);
    const auto radius = std::min(static_cast<double>(command.radius), minimum_dimension * 0.5);
    if (thickness == minimum_dimension) {
        draw_rounded_rect(
            FillRoundedRect{
                .bounds = bounds,
                .radius = static_cast<float>(radius),
                .color = command.color,
            },
            surface,
            clip
        );
        return;
    }

    const auto pixels = intersect(pixel_bounds(bounds, surface.width, surface.height), clip);
    if (pixels.left >= pixels.right || pixels.top >= pixels.bottom) {
        return;
    }

    const auto half_thickness = thickness * 0.5;
    const auto centerline_radius = std::max(0.0, radius - half_thickness);
    const auto half_width = static_cast<double>(bounds.width) * 0.5 - half_thickness;
    const auto half_height = static_cast<double>(bounds.height) * 0.5 - half_thickness;
    const auto center_x = static_cast<double>(bounds.x) + static_cast<double>(bounds.width) * 0.5;
    const auto center_y = static_cast<double>(bounds.y) + static_cast<double>(bounds.height) * 0.5;
    const auto straight_x = half_width - centerline_radius;
    const auto straight_y = half_height - centerline_radius;
    const auto outer_reach = half_thickness + 0.5;
    for (auto y = pixels.top; y < pixels.bottom; ++y) {
        const auto row = y * surface.stride;
        const auto point_y = std::abs(static_cast<double>(y) + 0.5 - center_y) - straight_y;
        const auto paint_range = [&](std::size_t first, std::size_t last) {
            for (auto x = first; x < last; ++x) {
                const auto point_x = std::abs(static_cast<double>(x) + 0.5 - center_x) - straight_x;
                double distance = 0.0;
                if (point_x <= 0.0 || point_y <= 0.0) {
                    distance = std::max(point_x, point_y) - centerline_radius;
                } else {
                    distance = std::hypot(point_x, point_y) - centerline_radius;
                }

                const auto edge_distance = std::abs(distance);
                if (edge_distance >= outer_reach) {
                    continue;
                }

                const auto coverage = std::clamp(outer_reach - edge_distance, 0.0, 1.0);
                blend_coverage(command.color, coverage, surface.pixels[row + x]);
            }
        };

        const auto central_extent = straight_x + std::min(point_y, 0.0);
        auto central_left = pixels.left;
        auto central_right = pixels.left;
        if (central_extent > 0.0) {
            const auto left = center_x - central_extent;
            const auto right = center_x + central_extent;
            central_left = static_cast<std::size_t>(std::clamp(
                std::ceil(left - 0.5),
                static_cast<double>(pixels.left),
                static_cast<double>(pixels.right)
            ));
            central_right = static_cast<std::size_t>(std::clamp(
                std::floor(right - 0.5) + 1.0,
                static_cast<double>(central_left),
                static_cast<double>(pixels.right)
            ));
            const auto distance = point_y - centerline_radius;
            const auto coverage = std::clamp(outer_reach - std::abs(distance), 0.0, 1.0);
            blend_coverage_span(
                command.color,
                coverage,
                surface.pixels.subspan(row + central_left, central_right - central_left)
            );
        }

        if (point_y > centerline_radius + outer_reach) {
            continue;
        }

        double side_start = 0.0;
        double side_end = 0.0;
        if (point_y > 0.0) {
            const auto inner_radius = std::max(0.0, centerline_radius - outer_reach);
            side_start = std::sqrt(std::max(0.0, inner_radius * inner_radius - point_y * point_y));
            side_end = std::sqrt(
                std::max(
                    0.0,
                    (centerline_radius + outer_reach) * (centerline_radius + outer_reach) -
                        point_y * point_y
                )
            );
        } else {
            side_start = std::max(point_y, centerline_radius - outer_reach);
            side_end = centerline_radius + outer_reach;
        }

        const auto minimum_offset = straight_x + side_start;
        const auto maximum_offset = straight_x + side_end;
        if (minimum_offset <= 0.0) {
            paint_range(pixels.left, central_left);
            paint_range(central_right, pixels.right);
            continue;
        }

        const auto left_first = static_cast<std::size_t>(std::clamp(
            std::ceil(center_x - maximum_offset - 0.5),
            static_cast<double>(pixels.left),
            static_cast<double>(pixels.right)
        ));
        const auto left_last = static_cast<std::size_t>(std::clamp(
            std::floor(center_x - minimum_offset - 0.5) + 1.0,
            static_cast<double>(left_first),
            static_cast<double>(pixels.right)
        ));
        const auto right_first = static_cast<std::size_t>(std::clamp(
            std::ceil(center_x + minimum_offset - 0.5),
            static_cast<double>(pixels.left),
            static_cast<double>(pixels.right)
        ));
        const auto right_last = static_cast<std::size_t>(std::clamp(
            std::floor(center_x + maximum_offset - 0.5) + 1.0,
            static_cast<double>(right_first),
            static_cast<double>(pixels.right)
        ));
        if (left_first < central_right && left_last > central_left) {
            paint_range(left_first, std::min(left_last, central_left));
            paint_range(std::max(left_first, central_right), left_last);
        } else {
            paint_range(left_first, left_last);
        }
        if (right_first < central_right && right_last > central_left) {
            paint_range(right_first, std::min(right_last, central_left));
            paint_range(std::max(right_first, central_right), right_last);
        } else {
            paint_range(right_first, right_last);
        }
    }
}

}
