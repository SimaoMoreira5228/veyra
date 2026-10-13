#ifndef VEYRA_SRC_RENDER_SOFTWARE_RENDERER_INTERNAL_HPP
#define VEYRA_SRC_RENDER_SOFTWARE_RENDERER_INTERNAL_HPP

#include "veyra/render/software_renderer.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace veyra::render_detail {

inline bool valid_rect(const LayoutRect& rect) noexcept {
    return std::isfinite(rect.x) && std::isfinite(rect.y) && std::isfinite(rect.width) &&
        std::isfinite(rect.height) && rect.width >= 0.0F && rect.height >= 0.0F;
}

bool valid_commands(
    std::span<const DisplayCommand> commands,
    std::size_t& maximum_clip_depth,
    std::size_t& maximum_translation_depth,
    std::size_t& maximum_path_intersections,
    std::size_t& maximum_path_windings
) noexcept;

bool valid_translated_commands(
    std::span<const DisplayCommand> commands,
    std::span<LayoutPoint> translations
) noexcept;

void draw_rounded_rect(
    const FillRoundedRect& command,
    SoftwareSurface surface,
    SoftwareClipRect clip
) noexcept;

void stroke_rounded_rect(
    const StrokeRoundedRect& command,
    SoftwareSurface surface,
    SoftwareClipRect clip
) noexcept;

inline SoftwareClipRect intersect(SoftwareClipRect left, SoftwareClipRect right) noexcept {
    return {
        .left = std::max(left.left, right.left),
        .top = std::max(left.top, right.top),
        .right = std::min(left.right, right.right),
        .bottom = std::min(left.bottom, right.bottom),
    };
}

inline std::size_t floor_coordinate(double value, std::size_t limit) noexcept {
    if (value >= static_cast<double>(limit)) {
        return limit;
    }
    return static_cast<std::size_t>(std::floor(value));
}

inline std::size_t ceil_coordinate(double value, std::size_t limit) noexcept {
    if (value >= static_cast<double>(limit)) {
        return limit;
    }
    return static_cast<std::size_t>(std::ceil(value));
}

inline SoftwareClipRect
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

inline SoftwareClipRect pixel_interior_bounds(
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

inline SoftwarePixel composite(DisplayColor source, SoftwarePixel destination) noexcept {
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

struct BlitBounds {
    std::size_t source_left{};
    std::size_t source_top{};
    SoftwareClipRect destination;
};

inline BlitBounds blit_bounds(
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

bool valid_image(const SoftwareImage& image) noexcept;
bool valid_image_placement(const DrawImage& command) noexcept;
void draw_image(const DrawImage& command, SoftwareSurface surface, SoftwareClipRect clip) noexcept;
bool valid_path(
    const FillPath& command,
    std::size_t& intersection_capacity,
    std::size_t& winding_capacity
) noexcept;
bool valid_translated_path(const FillPath& command, LayoutPoint translation) noexcept;
void draw_path(
    const FillPath& command,
    LayoutPoint translation,
    SoftwareSurface surface,
    SoftwareClipRect clip,
    std::span<double> intersections,
    std::span<std::int8_t> windings
) noexcept;

struct PathPixelSpan {
    std::size_t y{};
    std::size_t left{};
    std::size_t right{};
};

using PathSpanEmit = void (*)(void* context, PathPixelSpan span) noexcept;

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
) noexcept;

}

#endif
