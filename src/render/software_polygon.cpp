#include "software_renderer_internal.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

namespace veyra::render_detail {
namespace {

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

}

bool valid_polygon(const FillPolygon& command) noexcept {
    if (command.points.size() < 3) {
        return false;
    }

    for (const auto point : command.points) {
        if (!std::isfinite(point.x) || !std::isfinite(point.y)) {
            return false;
        }
    }

    return true;
}

void draw_polygon(
    const FillPolygon& command,
    LayoutPoint translation,
    SoftwareSurface surface,
    SoftwareClipRect clip,
    std::span<double> intersections
) noexcept {
    if (command.color.alpha == 0) {
        return;
    }

    auto minimum_y = std::numeric_limits<double>::infinity();
    auto maximum_y = -std::numeric_limits<double>::infinity();
    for (const auto point : command.points) {
        const auto y = static_cast<double>(point.y + translation.y);
        minimum_y = std::min(minimum_y, y);
        maximum_y = std::max(maximum_y, y);
    }

    const auto surface_height = static_cast<double>(surface.height);
    auto top = floor_coordinate(std::clamp(minimum_y, 0.0, surface_height), surface.height);
    auto bottom = ceil_coordinate(std::clamp(maximum_y, 0.0, surface_height), surface.height);
    top = std::max(top, clip.top);
    bottom = std::min(bottom, clip.bottom);
    if (top >= bottom) {
        return;
    }

    for (auto y = top; y < bottom; ++y) {
        const auto sample_y = static_cast<double>(y) + 0.5;
        std::size_t intersection_count = 0;
        auto previous = command.points.back();
        auto previous_x = static_cast<double>(previous.x + translation.x);
        auto previous_y = static_cast<double>(previous.y + translation.y);
        for (const auto point : command.points) {
            const auto current_x = static_cast<double>(point.x + translation.x);
            const auto current_y = static_cast<double>(point.y + translation.y);
            if ((previous_y <= sample_y && sample_y < current_y) ||
                (current_y <= sample_y && sample_y < previous_y)) {
                const auto fraction = (sample_y - previous_y) / (current_y - previous_y);
                intersections[intersection_count++] =
                    previous_x + fraction * (current_x - previous_x);
            }
            previous_x = current_x;
            previous_y = current_y;
        }

        std::sort(
            intersections.begin(),
            intersections.begin() + static_cast<std::ptrdiff_t>(intersection_count)
        );
        const auto surface_width = static_cast<double>(surface.width);
        for (std::size_t index = 0; index + 1 < intersection_count; index += 2) {
            const auto left_position = intersections[index] - 0.5;
            const auto right_position = intersections[index + 1] - 0.5;
            auto left =
                ceil_coordinate(std::clamp(left_position, 0.0, surface_width), surface.width);
            auto right =
                ceil_coordinate(std::clamp(right_position, 0.0, surface_width), surface.width);
            left = std::max(left, clip.left);
            right = std::min(right, clip.right);
            fill_span(y, left, right, command.color, surface);
        }
    }
}

}
