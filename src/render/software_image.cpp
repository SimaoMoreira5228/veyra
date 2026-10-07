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

struct ImageSourceRegion {
    std::size_t first_x{};
    std::size_t last_x{};
    std::size_t first_y{};
    std::size_t last_y{};
    double minimum_x{};
    double maximum_x{};
    double minimum_y{};
    double maximum_y{};
};

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

void blit_pixel(
    const SoftwarePixel& source,
    SoftwarePixel& destination,
    std::uint8_t opacity
) noexcept {
    if (source.alpha == 0) {
        return;
    }

    const auto alpha =
        static_cast<std::uint8_t>((static_cast<std::uint32_t>(source.alpha) * opacity + 127) / 255);
    if (alpha == 0) {
        return;
    }
    destination = composite(
        DisplayColor{
            .red = source.red,
            .green = source.green,
            .blue = source.blue,
            .alpha = alpha,
        },
        destination
    );
}

template <bool apply_opacity>
void blit_image_pixel(
    const SoftwarePixel& source,
    SoftwarePixel& destination,
    std::uint8_t opacity
) noexcept {
    if constexpr (apply_opacity) {
        blit_pixel(source, destination, opacity);
    } else {
        blit_pixel(source, destination);
    }
}

double interpolate(double start, double end, double fraction) noexcept {
    return start + (end - start) * fraction;
}

std::uint8_t rounded_channel(double value) noexcept {
    return static_cast<std::uint8_t>(std::clamp(std::lround(value), 0L, 255L));
}

SoftwarePixel bilinear_pixel(
    const SoftwarePixel& top_left,
    const SoftwarePixel& top_right,
    const SoftwarePixel& bottom_left,
    const SoftwarePixel& bottom_right,
    double fraction_x,
    double fraction_y
) noexcept {
    const auto interpolate_plane = [fraction_x, fraction_y](
                                       double top_left_value,
                                       double top_right_value,
                                       double bottom_left_value,
                                       double bottom_right_value
                                   ) {
        const auto top = interpolate(top_left_value, top_right_value, fraction_x);
        const auto bottom = interpolate(bottom_left_value, bottom_right_value, fraction_x);
        return interpolate(top, bottom, fraction_y);
    };
    const auto alpha =
        interpolate_plane(top_left.alpha, top_right.alpha, bottom_left.alpha, bottom_right.alpha);
    if (alpha <= 0.0) {
        return {.alpha = 0};
    }
    const auto interpolate_channel = [&](std::uint8_t SoftwarePixel::* channel) {
        const auto premultiplied = interpolate_plane(
            static_cast<double>(top_left.*channel) * top_left.alpha,
            static_cast<double>(top_right.*channel) * top_right.alpha,
            static_cast<double>(bottom_left.*channel) * bottom_left.alpha,
            static_cast<double>(bottom_right.*channel) * bottom_right.alpha
        );
        return rounded_channel(premultiplied / alpha);
    };

    return {
        .red = interpolate_channel(&SoftwarePixel::red),
        .green = interpolate_channel(&SoftwarePixel::green),
        .blue = interpolate_channel(&SoftwarePixel::blue),
        .alpha = rounded_channel(alpha),
    };
}

template <bool apply_opacity>
void draw_nearest_image(
    const DrawImage& command,
    SoftwareSurface surface,
    SoftwareClipRect destination,
    ImageSourceRegion source_region,
    std::uint8_t opacity
) noexcept {
    const auto& image = *command.image;
    const auto& source = command.source;
    const auto& target = command.destination;
    const auto scale_x = static_cast<double>(source.width) / target.width;
    const auto scale_y = static_cast<double>(source.height) / target.height;
    for (auto y = destination.top; y < destination.bottom; ++y) {
        const auto source_y = std::clamp(
            static_cast<double>(source.y) + (static_cast<double>(y) + 0.5 - target.y) * scale_y,
            source_region.minimum_y,
            source_region.maximum_y
        );
        const auto image_y = std::clamp(
            static_cast<std::size_t>(std::floor(source_y)),
            source_region.first_y,
            source_region.last_y
        );
        const auto destination_row = y * surface.stride;
        const auto image_row = image_y * image.stride;
        for (auto x = destination.left; x < destination.right; ++x) {
            const auto source_x = std::clamp(
                static_cast<double>(source.x) + (static_cast<double>(x) + 0.5 - target.x) * scale_x,
                source_region.minimum_x,
                source_region.maximum_x
            );
            const auto image_x = std::clamp(
                static_cast<std::size_t>(std::floor(source_x)),
                source_region.first_x,
                source_region.last_x
            );
            blit_image_pixel<apply_opacity>(
                image.pixels[image_row + image_x],
                surface.pixels[destination_row + x],
                opacity
            );
        }
    }
}

template <bool apply_opacity>
void draw_linear_image(
    const DrawImage& command,
    SoftwareSurface surface,
    SoftwareClipRect destination,
    ImageSourceRegion source_region,
    std::uint8_t opacity
) noexcept {
    const auto& image = *command.image;
    const auto& source = command.source;
    const auto& target = command.destination;
    const auto scale_x = static_cast<double>(source.width) / target.width;
    const auto scale_y = static_cast<double>(source.height) / target.height;
    for (auto y = destination.top; y < destination.bottom; ++y) {
        const auto source_y = std::clamp(
            static_cast<double>(source.y) + (static_cast<double>(y) + 0.5 - target.y) * scale_y,
            source_region.minimum_y,
            source_region.maximum_y
        );
        const auto sample_y = std::clamp(
            source_y - 0.5,
            static_cast<double>(source_region.first_y),
            static_cast<double>(source_region.last_y)
        );
        const auto lower_image_y = std::floor(sample_y);
        const auto image_y = static_cast<std::size_t>(lower_image_y);
        const auto next_image_y = std::min(image_y + 1, source_region.last_y);
        const auto fraction_y = sample_y - lower_image_y;
        const auto destination_row = y * surface.stride;
        const auto image_row = image_y * image.stride;
        const auto next_image_row = next_image_y * image.stride;
        for (auto x = destination.left; x < destination.right; ++x) {
            const auto source_x = std::clamp(
                static_cast<double>(source.x) + (static_cast<double>(x) + 0.5 - target.x) * scale_x,
                source_region.minimum_x,
                source_region.maximum_x
            );
            const auto sample_x = std::clamp(
                source_x - 0.5,
                static_cast<double>(source_region.first_x),
                static_cast<double>(source_region.last_x)
            );
            const auto lower_image_x = std::floor(sample_x);
            const auto image_x = static_cast<std::size_t>(lower_image_x);
            const auto next_image_x = std::min(image_x + 1, source_region.last_x);
            const auto pixel = bilinear_pixel(
                image.pixels[image_row + image_x],
                image.pixels[image_row + next_image_x],
                image.pixels[next_image_row + image_x],
                image.pixels[next_image_row + next_image_x],
                sample_x - lower_image_x,
                fraction_y
            );
            blit_image_pixel<apply_opacity>(pixel, surface.pixels[destination_row + x], opacity);
        }
    }
}

template <bool apply_opacity>
void draw_native_image(
    const DrawImage& command,
    SoftwareSurface surface,
    SoftwareClipRect clip,
    std::uint8_t opacity
) noexcept {
    const auto& image = *command.image;
    const auto bounds = blit_bounds(
        static_cast<std::int32_t>(command.destination.x),
        static_cast<std::int32_t>(command.destination.y),
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
        for (auto destination_x = bounds.destination.left; destination_x < bounds.destination.right;
             ++destination_x) {
            const auto image_x = bounds.source_left + destination_x - bounds.destination.left;
            blit_image_pixel<apply_opacity>(
                image.pixels[image_row + image_x],
                surface.pixels[destination_row + destination_x],
                opacity
            );
        }
    }
}

}

void draw_image(const DrawImage& command, SoftwareSurface surface, SoftwareClipRect clip) noexcept {
    const auto& image = *command.image;
    const auto& source_bounds = command.source;
    const auto& destination_bounds = command.destination;
    if (source_bounds.width == 0.0F || source_bounds.height == 0.0F ||
        destination_bounds.width == 0.0F || destination_bounds.height == 0.0F ||
        command.opacity == 0) {
        return;
    }

    const auto source_right = static_cast<double>(source_bounds.x) + source_bounds.width;
    const auto source_bottom = static_cast<double>(source_bounds.y) + source_bounds.height;
    const auto minimum_source_x = static_cast<double>(source_bounds.x);
    const auto minimum_source_y = static_cast<double>(source_bounds.y);
    const auto source_region = ImageSourceRegion{
        .first_x = static_cast<std::size_t>(std::floor(source_bounds.x)),
        .last_x = static_cast<std::size_t>(std::ceil(source_right)) - 1,
        .first_y = static_cast<std::size_t>(std::floor(source_bounds.y)),
        .last_y = static_cast<std::size_t>(std::ceil(source_bottom)) - 1,
        .minimum_x = minimum_source_x,
        .maximum_x = std::nextafter(source_right, minimum_source_x),
        .minimum_y = minimum_source_y,
        .maximum_y = std::nextafter(source_bottom, minimum_source_y),
    };
    const auto destination =
        intersect(pixel_bounds(destination_bounds, surface.width, surface.height), clip);
    if (destination.left >= destination.right || destination.top >= destination.bottom) {
        return;
    }

    if (source_bounds.x == 0.0F && source_bounds.y == 0.0F &&
        command.sampling == SoftwareImageSampling::nearest &&
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
        if (command.opacity == 255) {
            draw_native_image<false>(command, surface, clip, command.opacity);
        } else {
            draw_native_image<true>(command, surface, clip, command.opacity);
        }
        return;
    }

    if (command.sampling == SoftwareImageSampling::linear) {
        if (command.opacity == 255) {
            draw_linear_image<false>(command, surface, destination, source_region, command.opacity);
        } else {
            draw_linear_image<true>(command, surface, destination, source_region, command.opacity);
        }
        return;
    }

    if (command.opacity == 255) {
        draw_nearest_image<false>(command, surface, destination, source_region, command.opacity);
        return;
    }
    draw_nearest_image<true>(command, surface, destination, source_region, command.opacity);
}

}
