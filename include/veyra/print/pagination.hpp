#ifndef VEYRA_PRINT_PAGINATION_HPP
#define VEYRA_PRINT_PAGINATION_HPP

#include "veyra/layout/geometry.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>

namespace veyra {

enum class PaginationResult : std::uint8_t {
    complete,
    invalid_input,
    insufficient_output,
};

inline PaginationResult paginate_print_blocks(
    std::span<const float> block_heights,
    LayoutRect content_rect,
    std::span<LayoutRect> page_storage,
    std::size_t& page_count
) noexcept {
    if (!std::isfinite(content_rect.x) || !std::isfinite(content_rect.y)) {
        return PaginationResult::invalid_input;
    }

    if (!std::isfinite(content_rect.width) || !std::isfinite(content_rect.height)) {
        return PaginationResult::invalid_input;
    }

    if (content_rect.width <= 0.0F || content_rect.height <= 0.0F) {
        return PaginationResult::invalid_input;
    }

    double total_height = 0.0;

    for (const float block_height : block_heights) {
        if (!std::isfinite(block_height) || block_height < 0.0F) {
            return PaginationResult::invalid_input;
        }

        total_height += static_cast<double>(block_height);
    }

    if (!std::isfinite(total_height)) {
        return PaginationResult::invalid_input;
    }

    const double page_height = static_cast<double>(content_rect.height);

    std::size_t needed = 0;

    if (total_height > 0.0) {
        needed = static_cast<std::size_t>(std::ceil(total_height / page_height));
    }

    if (needed == 0) {
        page_count = 0;

        return PaginationResult::complete;
    }

    if (page_storage.size() < needed) {
        page_count = needed;

        return PaginationResult::insufficient_output;
    }

    double remaining = total_height;

    for (std::size_t index = 0; index < needed; ++index) {
        double slice = page_height;

        if (slice > remaining) {
            slice = remaining;
        }

        const float page_y =
            content_rect.y + static_cast<float>(static_cast<double>(index) * page_height);

        page_storage[index].x = content_rect.x;
        page_storage[index].y = page_y;
        page_storage[index].width = content_rect.width;
        page_storage[index].height = static_cast<float>(slice);

        remaining -= slice;
    }

    page_count = needed;

    return PaginationResult::complete;
}

}

#endif
