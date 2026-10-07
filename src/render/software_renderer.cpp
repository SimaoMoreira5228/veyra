#include "veyra/render/software_renderer.hpp"

#include "software_renderer_internal.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace veyra {
namespace {

using render_detail::blit_bounds;
using render_detail::ceil_coordinate;
using render_detail::composite;
using render_detail::floor_coordinate;
using render_detail::intersect;
using render_detail::pixel_bounds;
using render_detail::valid_rect;

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
    const auto interior_left = render_detail::ceil_coordinate(left, width);
    const auto interior_top = render_detail::ceil_coordinate(top, height);
    const auto interior_right =
        std::max(interior_left, render_detail::floor_coordinate(right, width));
    const auto interior_bottom =
        std::max(interior_top, render_detail::floor_coordinate(bottom, height));

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
            if (image->image == nullptr || !render_detail::valid_image(*image->image) ||
                !render_detail::valid_image_placement(*image)) {
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
            render_detail::draw_image(*image, surface, current_clip);
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
