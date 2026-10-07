#include "software_renderer_internal.hpp"

namespace veyra::render_detail {

void stroke_polyline(
    const StrokePolyline& command,
    LayoutPoint translation,
    SoftwareSurface surface,
    SoftwareClipRect clip,
    std::span<std::uint8_t> coverage_row
) noexcept {
    if (command.points.size() < 2 || command.thickness == 0.0F || command.color.alpha == 0 ||
        surface.width == 0 || surface.height == 0) {
        return;
    }

    const auto radius = static_cast<double>(command.thickness) * 0.5;
    const auto fringe = radius + 0.5;
    auto minimum_x = std::numeric_limits<double>::infinity();
    auto minimum_y = std::numeric_limits<double>::infinity();
    auto maximum_x = -std::numeric_limits<double>::infinity();
    auto maximum_y = -std::numeric_limits<double>::infinity();
    for (const auto point : command.points) {
        const auto x = static_cast<double>(point.x) + translation.x;
        const auto y = static_cast<double>(point.y) + translation.y;
        minimum_x = std::min(minimum_x, x);
        minimum_y = std::min(minimum_y, y);
        maximum_x = std::max(maximum_x, x);
        maximum_y = std::max(maximum_y, y);
    }

    const auto bounds = intersect(
        {
            .left = floor_coordinate(
                std::clamp(minimum_x - fringe, 0.0, static_cast<double>(surface.width)),
                surface.width
            ),
            .top = floor_coordinate(
                std::clamp(minimum_y - fringe, 0.0, static_cast<double>(surface.height)),
                surface.height
            ),
            .right = ceil_coordinate(
                std::clamp(maximum_x + fringe, 0.0, static_cast<double>(surface.width)),
                surface.width
            ),
            .bottom = ceil_coordinate(
                std::clamp(maximum_y + fringe, 0.0, static_cast<double>(surface.height)),
                surface.height
            ),
        },
        clip
    );
    if (bounds.left >= bounds.right || bounds.top >= bounds.bottom) {
        return;
    }

    const auto segment_count = command.closed ? command.points.size() : command.points.size() - 1;
    const auto fringe_squared = fringe * fringe;
    const auto source_alpha = static_cast<std::uint32_t>(command.color.alpha);
    for (auto y = bounds.top; y < bounds.bottom; ++y) {
        const auto pixel_y = static_cast<double>(y) + 0.5;
        const auto row_offset = y * surface.stride;
        std::fill_n(coverage_row.data() + bounds.left, bounds.right - bounds.left, 0);
        for (std::size_t index = 0; index < segment_count; ++index) {
            const auto start = command.points[index];
            const auto end = command.points[(index + 1) % command.points.size()];
            const auto start_x = static_cast<double>(start.x) + translation.x;
            const auto start_y = static_cast<double>(start.y) + translation.y;
            const auto end_x = static_cast<double>(end.x) + translation.x;
            const auto end_y = static_cast<double>(end.y) + translation.y;
            if (pixel_y < std::min(start_y, end_y) - fringe ||
                pixel_y > std::max(start_y, end_y) + fringe) {
                continue;
            }

            const auto left = std::max(
                bounds.left,
                floor_coordinate(
                    std::clamp(
                        std::min(start_x, end_x) - fringe,
                        0.0,
                        static_cast<double>(surface.width)
                    ),
                    surface.width
                )
            );
            const auto right = std::min(
                bounds.right,
                ceil_coordinate(
                    std::clamp(
                        std::max(start_x, end_x) + fringe,
                        0.0,
                        static_cast<double>(surface.width)
                    ),
                    surface.width
                )
            );
            const auto direction_x = end_x - start_x;
            const auto direction_y = end_y - start_y;
            const auto length_squared = direction_x * direction_x + direction_y * direction_y;
            for (auto x = left; x < right; ++x) {
                const auto pixel_x = static_cast<double>(x) + 0.5;
                const auto projection = length_squared == 0.0
                    ? 0.0
                    : std::clamp(
                          ((pixel_x - start_x) * direction_x + (pixel_y - start_y) * direction_y) /
                              length_squared,
                          0.0,
                          1.0
                      );
                const auto distance_x = pixel_x - (start_x + projection * direction_x);
                const auto distance_y = pixel_y - (start_y + projection * direction_y);
                const auto distance_squared = distance_x * distance_x + distance_y * distance_y;
                if (distance_squared >= fringe_squared) {
                    continue;
                }

                const auto coverage = std::min(1.0, fringe - std::sqrt(distance_squared));
                const auto coverage_alpha =
                    static_cast<std::uint8_t>(std::lround(coverage * 255.0));
                coverage_row[x] = std::max(coverage_row[x], coverage_alpha);
            }
        }

        for (auto x = bounds.left; x < bounds.right; ++x) {
            const auto coverage_alpha = coverage_row[x];
            if (coverage_alpha == 0) {
                continue;
            }
            const auto alpha =
                static_cast<std::uint8_t>((source_alpha * coverage_alpha + 127) / 255);
            auto& destination = surface.pixels[row_offset + x];
            destination = composite(
                DisplayColor{
                    .red = command.color.red,
                    .green = command.color.green,
                    .blue = command.color.blue,
                    .alpha = alpha,
                },
                destination
            );
        }
    }
}

}
