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

bool overlaps(SoftwareClipRect left, SoftwareClipRect right) noexcept {
    return left.left < right.right && right.left < left.right && left.top < right.bottom &&
        right.top < left.bottom;
}

bool valid_commands(std::span<const DisplayCommand> commands, std::size_t& maximum_depth) noexcept {
    std::size_t depth = 0;
    for (const auto& command : commands) {
        switch (command.type) {
        case DisplayCommandType::fill_rect:
        case DisplayCommandType::push_clip:
            if (!valid_rect(command.bounds)) {
                return false;
            }
            if (command.type == DisplayCommandType::push_clip) {
                ++depth;
                maximum_depth = std::max(maximum_depth, depth);
            }
            break;
        case DisplayCommandType::pop_clip:
            if (depth == 0) {
                return false;
            }
            --depth;
            break;
        default:
            return false;
        }
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

void fill_rect(
    const DisplayCommand& command,
    SoftwareSurface surface,
    SoftwareClipRect clip
) noexcept {
    const auto bounds =
        intersect(pixel_bounds(command.bounds, surface.width, surface.height), clip);
    if (bounds.left >= bounds.right || bounds.top >= bounds.bottom) {
        return;
    }
    if (command.color.alpha == 0) {
        return;
    }

    if (command.color.alpha == 255) {
        const auto pixel = SoftwarePixel{
            .red = command.color.red,
            .green = command.color.green,
            .blue = command.color.blue,
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
            destination = composite(command.color, destination);
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
        switch (command.type) {
        case DisplayCommandType::fill_rect:
            fill_rect(command, surface, current_clip);
            break;
        case DisplayCommandType::push_clip:
            workspace.clips[depth] = current_clip;
            ++depth;
            current_clip = intersect(
                current_clip,
                pixel_bounds(command.bounds, surface.width, surface.height)
            );
            break;
        case DisplayCommandType::pop_clip:
            --depth;
            current_clip = workspace.clips[depth];
            break;
        }
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
