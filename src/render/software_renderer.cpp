#include "veyra/render/software_renderer.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace veyra {
namespace {

bool valid_rect(const LayoutRect& rect) noexcept {
    return std::isfinite(rect.x) && std::isfinite(rect.y) && std::isfinite(rect.width) &&
        std::isfinite(rect.height) && rect.width >= 0.0F && rect.height >= 0.0F;
}

bool valid_surface(const SoftwareSurface& surface) noexcept {
    if (surface.stride < surface.width ||
        (surface.height != 0 &&
         surface.stride > std::numeric_limits<std::size_t>::max() / surface.height)) {
        return false;
    }

    return surface.pixels.size() >= surface.height * surface.stride;
}

bool valid_mask(const SoftwareMask& mask) noexcept {
    if (mask.stride < mask.width ||
        (mask.height != 0 && mask.stride > std::numeric_limits<std::size_t>::max() / mask.height)) {
        return false;
    }

    return mask.coverage.size() >= mask.height * mask.stride;
}

bool valid_image(const SoftwareImage& image) noexcept {
    if (image.stride < image.width ||
        (image.height != 0 &&
         image.stride > std::numeric_limits<std::size_t>::max() / image.height)) {
        return false;
    }

    return image.pixels.size() >= image.height * image.stride;
}

SoftwareClipRect intersect(SoftwareClipRect left, SoftwareClipRect right) noexcept {
    return {
        .left = std::max(left.left, right.left),
        .top = std::max(left.top, right.top),
        .right = std::min(left.right, right.right),
        .bottom = std::min(left.bottom, right.bottom),
    };
}

std::size_t floor_coordinate(double value, std::size_t limit) noexcept {
    if (value >= static_cast<double>(limit)) {
        return limit;
    }
    return static_cast<std::size_t>(std::floor(value));
}

std::size_t ceil_coordinate(double value, std::size_t limit) noexcept {
    if (value >= static_cast<double>(limit)) {
        return limit;
    }
    return static_cast<std::size_t>(std::ceil(value));
}

SoftwareClipRect
pixel_bounds(const LayoutRect& rect, std::size_t width, std::size_t height) noexcept {
    if (rect.width == 0.0F || rect.height == 0.0F) {
        return {};
    }

    const auto maximum_x = static_cast<double>(width);
    const auto maximum_y = static_cast<double>(height);
    const auto left = std::clamp(static_cast<double>(rect.x), 0.0, maximum_x);
    const auto top = std::clamp(static_cast<double>(rect.y), 0.0, maximum_y);
    const auto right = std::clamp(static_cast<double>(rect.x) + rect.width, 0.0, maximum_x);
    const auto bottom = std::clamp(static_cast<double>(rect.y) + rect.height, 0.0, maximum_y);

    return {
        .left = floor_coordinate(left, width),
        .top = floor_coordinate(top, height),
        .right = ceil_coordinate(right, width),
        .bottom = ceil_coordinate(bottom, height),
    };
}

SoftwareClipRect pixel_interior_bounds(
    double x,
    double y,
    double rectangle_width,
    double rectangle_height,
    std::size_t width,
    std::size_t height
) noexcept {
    const auto maximum_x = static_cast<double>(width);
    const auto maximum_y = static_cast<double>(height);
    const auto left = std::clamp(x, 0.0, maximum_x);
    const auto top = std::clamp(y, 0.0, maximum_y);
    const auto right = std::clamp(x + rectangle_width, 0.0, maximum_x);
    const auto bottom = std::clamp(y + rectangle_height, 0.0, maximum_y);
    const auto interior_left = ceil_coordinate(left, width);
    const auto interior_top = ceil_coordinate(top, height);
    const auto interior_right = std::max(interior_left, floor_coordinate(right, width));
    const auto interior_bottom = std::max(interior_top, floor_coordinate(bottom, height));

    return {
        .left = interior_left,
        .top = interior_top,
        .right = interior_right,
        .bottom = interior_bottom,
    };
}

bool overlaps(SoftwareClipRect left, SoftwareClipRect right) noexcept {
    return left.left < right.right && right.left < left.right && left.top < right.bottom &&
        right.top < left.bottom;
}

bool valid_commands(std::span<const DisplayCommand> commands, std::size_t& maximum_depth) noexcept {
    std::size_t depth = 0;
    for (const auto& command : commands) {
        if (const auto* fill = std::get_if<FillRect>(&command)) {
            if (!valid_rect(fill->bounds)) {
                return false;
            }
            continue;
        }
        if (const auto* stroke = std::get_if<StrokeRect>(&command)) {
            if (!valid_rect(stroke->bounds) || !std::isfinite(stroke->thickness) ||
                stroke->thickness < 0.0F) {
                return false;
            }
            continue;
        }
        if (const auto* line = std::get_if<StrokeLine>(&command)) {
            if (!std::isfinite(line->start.x) || !std::isfinite(line->start.y) ||
                !std::isfinite(line->end.x) || !std::isfinite(line->end.y) ||
                !std::isfinite(line->thickness) || line->thickness < 0.0F) {
                return false;
            }
            continue;
        }
        if (const auto* mask = std::get_if<DrawMask>(&command)) {
            if (mask->mask == nullptr || !valid_mask(*mask->mask)) {
                return false;
            }
            continue;
        }
        if (const auto* image = std::get_if<DrawImage>(&command)) {
            if (image->image == nullptr || !valid_image(*image->image)) {
                return false;
            }
            continue;
        }
        if (const auto* clip = std::get_if<PushClip>(&command)) {
            if (!valid_rect(clip->bounds)) {
                return false;
            }
            ++depth;
            maximum_depth = std::max(maximum_depth, depth);
            continue;
        }
        if (std::holds_alternative<PopClip>(command)) {
            if (depth == 0) {
                return false;
            }
            --depth;
            continue;
        }
        return false;
    }

    return depth == 0;
}

SoftwarePixel composite(DisplayColor source, SoftwarePixel destination) noexcept {
    if (source.alpha == 255) {
        return {
            .red = source.red,
            .green = source.green,
            .blue = source.blue,
            .alpha = 255,
        };
    }
    if (source.alpha == 0) {
        return destination;
    }
    if (destination.alpha == 0) {
        return {
            .red = source.red,
            .green = source.green,
            .blue = source.blue,
            .alpha = source.alpha,
        };
    }
    if (destination.alpha == 255) {
        const auto inverse_source_alpha = 255 - source.alpha;
        const auto blend_channel =
            [source,
             inverse_source_alpha](std::uint8_t source_channel, std::uint8_t destination_channel) {
                const auto channel_weight =
                    static_cast<std::uint32_t>(source_channel) * source.alpha +
                    static_cast<std::uint32_t>(destination_channel) * inverse_source_alpha;
                return static_cast<std::uint8_t>((channel_weight + 127) / 255);
            };
        return {
            .red = blend_channel(source.red, destination.red),
            .green = blend_channel(source.green, destination.green),
            .blue = blend_channel(source.blue, destination.blue),
            .alpha = 255,
        };
    }

    const auto source_weight = static_cast<std::uint32_t>(source.alpha) * 255;
    const auto destination_weight =
        static_cast<std::uint32_t>(destination.alpha) * (255 - source.alpha);
    const auto alpha_weight = source_weight + destination_weight;
    if (alpha_weight == 0) {
        return {};
    }

    const auto blend_channel =
        [source_weight,
         destination_weight,
         alpha_weight](std::uint8_t source_channel, std::uint8_t destination_channel) {
            const auto channel_weight = static_cast<std::uint32_t>(source_channel) * source_weight +
                static_cast<std::uint32_t>(destination_channel) * destination_weight;
            return static_cast<std::uint8_t>((channel_weight + alpha_weight / 2) / alpha_weight);
        };

    return {
        .red = blend_channel(source.red, destination.red),
        .green = blend_channel(source.green, destination.green),
        .blue = blend_channel(source.blue, destination.blue),
        .alpha = static_cast<std::uint8_t>((alpha_weight + 127) / 255),
    };
}

void fill_pixels(
    SoftwareClipRect bounds,
    DisplayColor color,
    SoftwareSurface surface,
    SoftwareClipRect clip
) noexcept {
    bounds = intersect(bounds, clip);
    if (bounds.left >= bounds.right || bounds.top >= bounds.bottom) {
        return;
    }
    if (color.alpha == 0) {
        return;
    }

    if (color.alpha == 255) {
        const auto pixel = SoftwarePixel{
            .red = color.red,
            .green = color.green,
            .blue = color.blue,
            .alpha = 255,
        };
        for (auto y = bounds.top; y < bounds.bottom; ++y) {
            const auto row_offset = y * surface.stride;
            std::fill_n(
                surface.pixels.data() + row_offset + bounds.left,
                bounds.right - bounds.left,
                pixel
            );
        }
        return;
    }

    for (auto y = bounds.top; y < bounds.bottom; ++y) {
        const auto row_offset = y * surface.stride;
        for (auto x = bounds.left; x < bounds.right; ++x) {
            auto& destination = surface.pixels[row_offset + x];
            destination = composite(color, destination);
        }
    }
}

void fill_rect(const FillRect& command, SoftwareSurface surface, SoftwareClipRect clip) noexcept {
    fill_pixels(
        pixel_bounds(command.bounds, surface.width, surface.height),
        command.color,
        surface,
        clip
    );
}

void stroke_rect(
    const StrokeRect& command,
    SoftwareSurface surface,
    SoftwareClipRect clip
) noexcept {
    const auto outer = pixel_bounds(command.bounds, surface.width, surface.height);
    if (outer.left >= outer.right || outer.top >= outer.bottom || command.thickness == 0.0F) {
        return;
    }

    const auto minimum_dimension = std::min(
        static_cast<double>(command.bounds.width),
        static_cast<double>(command.bounds.height)
    );
    const auto inset = std::min(static_cast<double>(command.thickness), minimum_dimension * 0.5);
    if (inset * 2.0 >= minimum_dimension) {
        fill_pixels(outer, command.color, surface, clip);
        return;
    }

    const auto inner_width = static_cast<double>(command.bounds.width) - inset * 2.0;
    const auto inner_height = static_cast<double>(command.bounds.height) - inset * 2.0;
    const auto inner = intersect(
        outer,
        pixel_interior_bounds(
            static_cast<double>(command.bounds.x) + inset,
            static_cast<double>(command.bounds.y) + inset,
            inner_width,
            inner_height,
            surface.width,
            surface.height
        )
    );
    if (inner.left >= inner.right || inner.top >= inner.bottom) {
        fill_pixels(outer, command.color, surface, clip);
        return;
    }

    fill_pixels(
        {
            .left = outer.left,
            .top = outer.top,
            .right = outer.right,
            .bottom = inner.top,
        },
        command.color,
        surface,
        clip
    );
    fill_pixels(
        {
            .left = outer.left,
            .top = inner.bottom,
            .right = outer.right,
            .bottom = outer.bottom,
        },
        command.color,
        surface,
        clip
    );
    fill_pixels(
        {
            .left = outer.left,
            .top = inner.top,
            .right = inner.left,
            .bottom = inner.bottom,
        },
        command.color,
        surface,
        clip
    );
    fill_pixels(
        {
            .left = inner.right,
            .top = inner.top,
            .right = outer.right,
            .bottom = inner.bottom,
        },
        command.color,
        surface,
        clip
    );
}

void stroke_line(
    const StrokeLine& command,
    SoftwareSurface surface,
    SoftwareClipRect clip
) noexcept {
    if (command.thickness == 0.0F || command.color.alpha == 0 || surface.width == 0 ||
        surface.height == 0) {
        return;
    }

    const auto start_x = static_cast<double>(command.start.x);
    const auto start_y = static_cast<double>(command.start.y);
    const auto end_x = static_cast<double>(command.end.x);
    const auto end_y = static_cast<double>(command.end.y);
    const auto radius = static_cast<double>(command.thickness) * 0.5;
    const auto fringe = radius + 0.5;
    const auto maximum_x = static_cast<double>(surface.width);
    const auto maximum_y = static_cast<double>(surface.height);
    const auto bounds = intersect(
        {
            .left = floor_coordinate(
                std::clamp(std::min(start_x, end_x) - fringe, 0.0, maximum_x),
                surface.width
            ),
            .top = floor_coordinate(
                std::clamp(std::min(start_y, end_y) - fringe, 0.0, maximum_y),
                surface.height
            ),
            .right = ceil_coordinate(
                std::clamp(std::max(start_x, end_x) + fringe, 0.0, maximum_x),
                surface.width
            ),
            .bottom = ceil_coordinate(
                std::clamp(std::max(start_y, end_y) + fringe, 0.0, maximum_y),
                surface.height
            ),
        },
        clip
    );
    if (bounds.left >= bounds.right || bounds.top >= bounds.bottom) {
        return;
    }

    const auto direction_x = end_x - start_x;
    const auto direction_y = end_y - start_y;
    const auto length_squared = direction_x * direction_x + direction_y * direction_y;
    const auto inverse_length_squared = length_squared == 0.0 ? 0.0 : 1.0 / length_squared;
    const auto fringe_squared = fringe * fringe;
    const auto source_alpha = static_cast<std::uint32_t>(command.color.alpha);
    for (auto y = bounds.top; y < bounds.bottom; ++y) {
        const auto pixel_y = static_cast<double>(y) + 0.5;
        const auto row_offset = y * surface.stride;
        for (auto x = bounds.left; x < bounds.right; ++x) {
            const auto pixel_x = static_cast<double>(x) + 0.5;
            const auto projection = length_squared == 0.0
                ? 0.0
                : std::clamp(
                      ((pixel_x - start_x) * direction_x + (pixel_y - start_y) * direction_y) *
                          inverse_length_squared,
                      0.0,
                      1.0
                  );
            const auto nearest_x = start_x + projection * direction_x;
            const auto nearest_y = start_y + projection * direction_y;
            const auto distance_x = pixel_x - nearest_x;
            const auto distance_y = pixel_y - nearest_y;
            const auto distance_squared = distance_x * distance_x + distance_y * distance_y;
            if (distance_squared >= fringe_squared) {
                continue;
            }

            const auto distance = std::sqrt(distance_squared);
            const auto coverage = std::min(1.0, fringe - distance);

            const auto coverage_alpha = static_cast<std::uint32_t>(std::lround(coverage * 255.0));
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

struct BlitBounds {
    std::size_t source_left{};
    std::size_t source_top{};
    SoftwareClipRect destination;
};

BlitBounds blit_bounds(
    std::int32_t x_coordinate,
    std::int32_t y_coordinate,
    std::size_t source_width,
    std::size_t source_height,
    SoftwareSurface surface,
    SoftwareClipRect clip
) noexcept {
    const auto x = static_cast<std::int64_t>(x_coordinate);
    const auto y = static_cast<std::int64_t>(y_coordinate);
    auto source_left = x < 0 ? static_cast<std::size_t>(-x) : 0;
    auto source_top = y < 0 ? static_cast<std::size_t>(-y) : 0;
    auto left = x < 0 ? 0 : static_cast<std::size_t>(x);
    auto top = y < 0 ? 0 : static_cast<std::size_t>(y);
    source_left = std::min(source_left, source_width);
    source_top = std::min(source_top, source_height);
    left = std::min(left, surface.width);
    top = std::min(top, surface.height);
    auto right = left + std::min(source_width - source_left, surface.width - left);
    auto bottom = top + std::min(source_height - source_top, surface.height - top);
    if (left < clip.left) {
        source_left += clip.left - left;
        left = clip.left;
    }
    if (top < clip.top) {
        source_top += clip.top - top;
        top = clip.top;
    }
    right = std::min(right, clip.right);
    bottom = std::min(bottom, clip.bottom);
    return {
        .source_left = source_left,
        .source_top = source_top,
        .destination = {
            .left = left,
            .top = top,
            .right = right,
            .bottom = bottom,
        },
    };
}

void draw_mask(const DrawMask& command, SoftwareSurface surface, SoftwareClipRect clip) noexcept {
    const auto& mask = *command.mask;
    const auto bounds = blit_bounds(command.x, command.y, mask.width, mask.height, surface, clip);
    if (bounds.destination.left >= bounds.destination.right ||
        bounds.destination.top >= bounds.destination.bottom || command.color.alpha == 0) {
        return;
    }

    for (auto destination_y = bounds.destination.top; destination_y < bounds.destination.bottom;
         ++destination_y) {
        const auto mask_y = bounds.source_top + destination_y - bounds.destination.top;
        const auto destination_row = destination_y * surface.stride;
        const auto mask_row = mask_y * mask.stride;
        for (auto destination_x = bounds.destination.left; destination_x < bounds.destination.right;
             ++destination_x) {
            const auto mask_x = bounds.source_left + destination_x - bounds.destination.left;
            const auto coverage = mask.coverage[mask_row + mask_x];
            if (coverage == 0) {
                continue;
            }
            const auto alpha = static_cast<std::uint8_t>(
                (static_cast<std::uint32_t>(command.color.alpha) * coverage + 127) / 255
            );
            auto& destination = surface.pixels[destination_row + destination_x];
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

void draw_image(const DrawImage& command, SoftwareSurface surface, SoftwareClipRect clip) noexcept {
    const auto& image = *command.image;
    const auto bounds = blit_bounds(command.x, command.y, image.width, image.height, surface, clip);
    if (bounds.destination.left >= bounds.destination.right ||
        bounds.destination.top >= bounds.destination.bottom) {
        return;
    }

    for (auto destination_y = bounds.destination.top; destination_y < bounds.destination.bottom;
         ++destination_y) {
        const auto image_y = bounds.source_top + destination_y - bounds.destination.top;
        const auto destination_row = destination_y * surface.stride;
        const auto image_row = image_y * image.stride;
        for (auto destination_x = bounds.destination.left; destination_x < bounds.destination.right;
             ++destination_x) {
            const auto image_x = bounds.source_left + destination_x - bounds.destination.left;
            const auto source = image.pixels[image_row + image_x];
            if (source.alpha == 255) {
                surface.pixels[destination_row + destination_x] = source;
                continue;
            }
            if (source.alpha == 0) {
                continue;
            }
            surface.pixels[destination_row + destination_x] = composite(
                DisplayColor{
                    .red = source.red,
                    .green = source.green,
                    .blue = source.blue,
                    .alpha = source.alpha,
                },
                surface.pixels[destination_row + destination_x]
            );
        }
    }
}

void render_commands(
    std::span<const DisplayCommand> commands,
    SoftwareSurface surface,
    SoftwareRenderWorkspace workspace,
    SoftwareClipRect initial_clip
) noexcept {
    auto current_clip = initial_clip;
    std::size_t depth = 0;
    for (const auto& command : commands) {
        if (const auto* fill = std::get_if<FillRect>(&command)) {
            fill_rect(*fill, surface, current_clip);
            continue;
        }
        if (const auto* stroke = std::get_if<StrokeRect>(&command)) {
            stroke_rect(*stroke, surface, current_clip);
            continue;
        }
        if (const auto* line = std::get_if<StrokeLine>(&command)) {
            stroke_line(*line, surface, current_clip);
            continue;
        }
        if (const auto* mask = std::get_if<DrawMask>(&command)) {
            draw_mask(*mask, surface, current_clip);
            continue;
        }
        if (const auto* image = std::get_if<DrawImage>(&command)) {
            draw_image(*image, surface, current_clip);
            continue;
        }
        if (const auto* clip = std::get_if<PushClip>(&command)) {
            workspace.clips[depth] = current_clip;
            ++depth;
            current_clip =
                intersect(current_clip, pixel_bounds(clip->bounds, surface.width, surface.height));
            continue;
        }
        --depth;
        current_clip = workspace.clips[depth];
    }
}

SoftwareRenderResult render(
    std::span<const DisplayCommand> commands,
    SoftwareSurface surface,
    SoftwareRenderWorkspace workspace,
    std::span<const LayoutRect> damage_regions,
    bool full_surface
) noexcept {
    if (!valid_surface(surface)) {
        return SoftwareRenderResult::invalid_surface;
    }

    std::size_t maximum_depth = 0;
    if (!valid_commands(commands, maximum_depth)) {
        return SoftwareRenderResult::invalid_input;
    }
    if (workspace.clips.size() < maximum_depth) {
        return SoftwareRenderResult::insufficient_workspace;
    }
    for (std::size_t index = 0; index < damage_regions.size(); ++index) {
        if (!valid_rect(damage_regions[index])) {
            return SoftwareRenderResult::invalid_input;
        }
        const auto bounds = pixel_bounds(damage_regions[index], surface.width, surface.height);
        for (std::size_t prior = 0; prior < index; ++prior) {
            const auto prior_bounds =
                pixel_bounds(damage_regions[prior], surface.width, surface.height);
            if (overlaps(bounds, prior_bounds)) {
                return SoftwareRenderResult::invalid_input;
            }
        }
    }

    const auto surface_clip = SoftwareClipRect{
        .right = surface.width,
        .bottom = surface.height,
    };
    if (full_surface) {
        render_commands(commands, surface, workspace, surface_clip);
        return SoftwareRenderResult::complete;
    }

    for (const auto& damage : damage_regions) {
        const auto damage_clip =
            intersect(surface_clip, pixel_bounds(damage, surface.width, surface.height));
        render_commands(commands, surface, workspace, damage_clip);
    }
    return SoftwareRenderResult::complete;
}

}

SoftwareRenderResult render_software(
    std::span<const DisplayCommand> commands,
    SoftwareSurface surface,
    SoftwareRenderWorkspace workspace
) noexcept {
    return render(commands, surface, workspace, {}, true);
}

SoftwareRenderResult render_software(
    std::span<const DisplayCommand> commands,
    SoftwareSurface surface,
    SoftwareRenderWorkspace workspace,
    std::span<const LayoutRect> damage_regions
) noexcept {
    return render(commands, surface, workspace, damage_regions, false);
}

}
