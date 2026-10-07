#include "software_renderer_internal.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace veyra::render_detail {

bool valid_image(const SoftwareImage& image) noexcept {
    if (image.stride < image.width ||
        (image.height != 0 &&
         image.stride > std::numeric_limits<std::size_t>::max() / image.height)) {
        return false;
    }

    return image.pixels.size() >= image.height * image.stride;
}

bool valid_image_placement(const DrawImage& command) noexcept {
    if (!valid_rect(command.source) || !valid_rect(command.destination)) {
        return false;
    }

    const auto right = static_cast<double>(command.source.x) + command.source.width;
    const auto bottom = static_cast<double>(command.source.y) + command.source.height;
    return command.source.x >= 0.0F && command.source.y >= 0.0F &&
        right <= static_cast<double>(command.image->width) &&
        bottom <= static_cast<double>(command.image->height);
}

namespace {

void blit_pixel(const SoftwarePixel& source, SoftwarePixel& destination) noexcept {
    if (source.alpha == 255) {
        destination = source;
        return;
    }
    if (source.alpha == 0) {
        return;
    }
    destination = composite(
        DisplayColor{
            .red = source.red,
            .green = source.green,
            .blue = source.blue,
            .alpha = source.alpha,
        },
        destination
    );
}

}

void draw_image(const DrawImage& command, SoftwareSurface surface, SoftwareClipRect clip) noexcept {
    const auto& image = *command.image;
    const auto& source_bounds = command.source;
    const auto& destination_bounds = command.destination;
    if (source_bounds.width == 0.0F || source_bounds.height == 0.0F ||
        destination_bounds.width == 0.0F || destination_bounds.height == 0.0F) {
        return;
    }

    const auto source_right = static_cast<double>(source_bounds.x) + source_bounds.width;
    const auto source_bottom = static_cast<double>(source_bounds.y) + source_bounds.height;
    const auto first_source_x = static_cast<std::size_t>(std::floor(source_bounds.x));
    const auto first_source_y = static_cast<std::size_t>(std::floor(source_bounds.y));
    const auto last_source_x = static_cast<std::size_t>(std::ceil(source_right)) - 1;
    const auto last_source_y = static_cast<std::size_t>(std::ceil(source_bottom)) - 1;
    const auto minimum_source_x = static_cast<double>(source_bounds.x);
    const auto minimum_source_y = static_cast<double>(source_bounds.y);
    const auto maximum_source_x = std::nextafter(source_right, minimum_source_x);
    const auto maximum_source_y = std::nextafter(source_bottom, minimum_source_y);
    const auto destination =
        intersect(pixel_bounds(destination_bounds, surface.width, surface.height), clip);
    if (destination.left >= destination.right || destination.top >= destination.bottom) {
        return;
    }

    if (source_bounds.x == 0.0F && source_bounds.y == 0.0F &&
        static_cast<double>(source_bounds.width) == static_cast<double>(image.width) &&
        static_cast<double>(source_bounds.height) == static_cast<double>(image.height) &&
        destination_bounds.width == source_bounds.width &&
        destination_bounds.height == source_bounds.height &&
        destination_bounds.x == std::floor(destination_bounds.x) &&
        destination_bounds.y == std::floor(destination_bounds.y) &&
        static_cast<double>(destination_bounds.x) >=
            static_cast<double>(std::numeric_limits<std::int32_t>::min()) &&
        static_cast<double>(destination_bounds.x) <=
            static_cast<double>(std::numeric_limits<std::int32_t>::max()) &&
        static_cast<double>(destination_bounds.y) >=
            static_cast<double>(std::numeric_limits<std::int32_t>::min()) &&
        static_cast<double>(destination_bounds.y) <=
            static_cast<double>(std::numeric_limits<std::int32_t>::max())) {
        const auto bounds = blit_bounds(
            static_cast<std::int32_t>(destination_bounds.x),
            static_cast<std::int32_t>(destination_bounds.y),
            image.width,
            image.height,
            surface,
            clip
        );
        for (auto destination_y = bounds.destination.top; destination_y < bounds.destination.bottom;
             ++destination_y) {
            const auto image_y = bounds.source_top + destination_y - bounds.destination.top;
            const auto destination_row = destination_y * surface.stride;
            const auto image_row = image_y * image.stride;
            for (auto destination_x = bounds.destination.left;
                 destination_x < bounds.destination.right;
                 ++destination_x) {
                const auto image_x = bounds.source_left + destination_x - bounds.destination.left;
                blit_pixel(
                    image.pixels[image_row + image_x],
                    surface.pixels[destination_row + destination_x]
                );
            }
        }
        return;
    }

    const auto scale_x = static_cast<double>(source_bounds.width) / destination_bounds.width;
    const auto scale_y = static_cast<double>(source_bounds.height) / destination_bounds.height;
    for (auto destination_y = destination.top; destination_y < destination.bottom;
         ++destination_y) {
        const auto image_y_position = std::clamp(
            static_cast<double>(source_bounds.y) +
                (static_cast<double>(destination_y) + 0.5 - destination_bounds.y) * scale_y,
            minimum_source_y,
            maximum_source_y
        );
        const auto image_y = std::clamp(
            static_cast<std::size_t>(std::floor(image_y_position)),
            first_source_y,
            last_source_y
        );
        const auto destination_row = destination_y * surface.stride;
        const auto image_row = image_y * image.stride;
        for (auto destination_x = destination.left; destination_x < destination.right;
             ++destination_x) {
            const auto image_x_position = std::clamp(
                static_cast<double>(source_bounds.x) +
                    (static_cast<double>(destination_x) + 0.5 - destination_bounds.x) * scale_x,
                minimum_source_x,
                maximum_source_x
            );
            const auto image_x = std::clamp(
                static_cast<std::size_t>(std::floor(image_x_position)),
                first_source_x,
                last_source_x
            );
            blit_pixel(
                image.pixels[image_row + image_x],
                surface.pixels[destination_row + destination_x]
            );
        }
    }
}

}
