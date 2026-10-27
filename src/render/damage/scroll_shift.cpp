#include "veyra/render/damage/scroll_shift.hpp"

#include <cstring>
#include <limits>

namespace veyra {

ScrollShiftResult
resolve_scroll_shift(const ScrollShiftRequest& request, ScrollShiftPlan& output) noexcept {
    if (request.width == 0) {
        return ScrollShiftResult::invalid_input;
    }

    if (request.height == 0) {
        return ScrollShiftResult::invalid_input;
    }

    if (!same_scroll_generation(request.current_generation, request.retained_generation)) {
        output = ScrollShiftPlan{};

        return ScrollShiftResult::stale_generation;
    }

    auto origin_x = std::size_t{0};
    auto origin_y = std::size_t{0};
    auto extent_x = request.width;
    auto extent_y = request.height;

    if (request.has_clip) {
        if (request.clip.left >= request.width) {
            output = ScrollShiftPlan{};

            return ScrollShiftResult::no_movement;
        }

        if (request.clip.top >= request.height) {
            output = ScrollShiftPlan{};

            return ScrollShiftResult::no_movement;
        }

        origin_x = request.clip.left;
        origin_y = request.clip.top;
        extent_x = request.clip.width;

        const auto remaining_x = request.width - origin_x;

        if (extent_x > remaining_x) {
            extent_x = remaining_x;
        }

        extent_y = request.clip.height;

        const auto remaining_y = request.height - origin_y;

        if (extent_y > remaining_y) {
            extent_y = remaining_y;
        }

        if (extent_x == 0 || extent_y == 0) {
            output = ScrollShiftPlan{};

            return ScrollShiftResult::no_movement;
        }
    }

    if (request.delta_x == 0 && request.delta_y == 0) {
        output = ScrollShiftPlan{};

        return ScrollShiftResult::no_movement;
    }

    const auto raw_x = static_cast<long long>(request.delta_x);
    const auto raw_y = static_cast<long long>(request.delta_y);

    auto magnitude_x = raw_x;

    if (magnitude_x < 0) {
        magnitude_x = -magnitude_x;
    }

    auto magnitude_y = raw_y;

    if (magnitude_y < 0) {
        magnitude_y = -magnitude_y;
    }

    const auto keep_x =
        static_cast<unsigned long long>(magnitude_x) >= static_cast<unsigned long long>(extent_x)
        ? std::size_t{0}
        : extent_x - static_cast<std::size_t>(magnitude_x);
    const auto keep_y =
        static_cast<unsigned long long>(magnitude_y) >= static_cast<unsigned long long>(extent_y)
        ? std::size_t{0}
        : extent_y - static_cast<std::size_t>(magnitude_y);

    if (keep_x == 0 || keep_y == 0) {
        output.has_blit = false;
        output.source = ScrollShiftRect{};
        output.destination = ScrollShiftRect{};
        output.strip_count = 1;
        output.strips[0] = {
            .left = origin_x,
            .top = origin_y,
            .width = extent_x,
            .height = extent_y,
        };
        output.strips[1] = ScrollShiftRect{};
        output.full_damage_x = request.delta_x != 0 && keep_x == 0;
        output.full_damage_y = request.delta_y != 0 && keep_y == 0;

        return ScrollShiftResult::complete;
    }

    auto source_left = origin_x;

    if (request.delta_x < 0) {
        source_left += static_cast<std::size_t>(magnitude_x);
    }

    auto source_top = origin_y;

    if (request.delta_y < 0) {
        source_top += static_cast<std::size_t>(magnitude_y);
    }

    auto destination_left = origin_x;

    if (request.delta_x > 0) {
        destination_left += static_cast<std::size_t>(magnitude_x);
    }

    auto destination_top = origin_y;

    if (request.delta_y > 0) {
        destination_top += static_cast<std::size_t>(magnitude_y);
    }

    output.has_blit = true;
    output.source = {
        .left = source_left,
        .top = source_top,
        .width = keep_x,
        .height = keep_y,
    };
    output.destination = {
        .left = destination_left,
        .top = destination_top,
        .width = keep_x,
        .height = keep_y,
    };

    auto strip_count = std::size_t{0};

    if (request.delta_y != 0) {
        auto strip_top = origin_y;

        if (request.delta_y < 0) {
            strip_top = origin_y + extent_y - static_cast<std::size_t>(magnitude_y);
        }

        output.strips[strip_count] = {
            .left = origin_x,
            .top = strip_top,
            .width = extent_x,
            .height = static_cast<std::size_t>(magnitude_y),
        };
        ++strip_count;
    }

    if (request.delta_x != 0) {
        auto strip_left = origin_x;

        if (request.delta_x < 0) {
            strip_left = origin_x + extent_x - static_cast<std::size_t>(magnitude_x);
        }

        auto strip_top = origin_y;

        if (request.delta_y > 0) {
            strip_top += static_cast<std::size_t>(magnitude_y);
        }

        output.strips[strip_count] = {
            .left = strip_left,
            .top = strip_top,
            .width = static_cast<std::size_t>(magnitude_x),
            .height = keep_y,
        };
        ++strip_count;
    }

    if (strip_count < 2) {
        output.strips[strip_count] = ScrollShiftRect{};
    }

    output.strip_count = strip_count;
    output.full_damage_x = false;
    output.full_damage_y = false;

    return ScrollShiftResult::complete;
}

bool shift_surface_pixels(
    std::span<SoftwarePixel> pixels,
    std::size_t stride,
    std::size_t surface_width,
    std::size_t surface_height,
    const ScrollShiftRect& source,
    const ScrollShiftRect& destination
) noexcept {
    if (source.width == 0) {
        return false;
    }

    if (source.height == 0) {
        return false;
    }

    if (source.width != destination.width) {
        return false;
    }

    if (source.height != destination.height) {
        return false;
    }

    if (surface_width == 0) {
        return false;
    }

    if (surface_height == 0) {
        return false;
    }

    if (stride < surface_width) {
        return false;
    }

    if (source.width > surface_width) {
        return false;
    }

    if (source.left > surface_width - source.width) {
        return false;
    }

    if (source.height > surface_height) {
        return false;
    }

    if (source.top > surface_height - source.height) {
        return false;
    }

    if (destination.left > surface_width - destination.width) {
        return false;
    }

    if (destination.top > surface_height - destination.height) {
        return false;
    }

    if (pixels.size() < surface_width) {
        return false;
    }

    if (surface_height - 1 > (pixels.size() - surface_width) / stride) {
        return false;
    }

    if (source.width > std::numeric_limits<std::size_t>::max() / sizeof(SoftwarePixel)) {
        return false;
    }

    const auto row_bytes = source.width * sizeof(SoftwarePixel);

    if (destination.top > source.top) {
        auto row = source.height;

        while (row > 0) {
            --row;

            const auto from = (source.top + row) * stride + source.left;
            const auto to = (destination.top + row) * stride + destination.left;
            std::memmove(&pixels[to], &pixels[from], row_bytes);
        }

        return true;
    }

    for (std::size_t row = 0; row < source.height; ++row) {
        const auto from = (source.top + row) * stride + source.left;
        const auto to = (destination.top + row) * stride + destination.left;
        std::memmove(&pixels[to], &pixels[from], row_bytes);
    }

    return true;
}

}
