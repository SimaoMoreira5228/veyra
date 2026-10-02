#include "veyra/layout/stack.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace veyra {
namespace {

bool is_finite_nonnegative(float value) noexcept {
    return std::isfinite(value) && value >= 0.0F;
}

bool valid_alignment(StackAlignment alignment) noexcept {
    switch (alignment) {
    case StackAlignment::start:
    case StackAlignment::center:
    case StackAlignment::end:
    case StackAlignment::stretch:
        return true;
    }
    return false;
}

bool valid_container(const StackContainer& container) noexcept {
    return is_finite_nonnegative(container.width) && is_finite_nonnegative(container.height) &&
        is_finite_nonnegative(container.padding.top) &&
        is_finite_nonnegative(container.padding.right) &&
        is_finite_nonnegative(container.padding.bottom) &&
        is_finite_nonnegative(container.padding.left) && valid_alignment(container.align_x) &&
        valid_alignment(container.align_y);
}

bool valid_item(const StackItem& item) noexcept {
    return is_finite_nonnegative(item.width) && is_finite_nonnegative(item.height) &&
        is_finite_nonnegative(item.min_width) && is_finite_nonnegative(item.max_width) &&
        item.min_width <= item.max_width && is_finite_nonnegative(item.min_height) &&
        is_finite_nonnegative(item.max_height) && item.min_height <= item.max_height;
}

double aligned_position(double origin, double available, double size, StackAlignment alignment) {
    switch (alignment) {
    case StackAlignment::start:
    case StackAlignment::stretch:
        return origin;
    case StackAlignment::center:
        return origin + (available - size) / 2.0;
    case StackAlignment::end:
        return origin + available - size;
    }
    return origin;
}

double aligned_size(
    double preferred,
    double minimum,
    double maximum,
    double available,
    StackAlignment alignment
) {
    const auto requested = alignment == StackAlignment::stretch ? available : preferred;
    return std::clamp(requested, minimum, maximum);
}

}

StackLayoutResult layout_stack(
    const StackContainer& container,
    std::span<const StackItem> items,
    std::span<LayoutRect> output
) noexcept {
    if (!valid_container(container)) {
        return StackLayoutResult::invalid_input;
    }
    if (output.size() < items.size()) {
        return StackLayoutResult::insufficient_output;
    }
    for (const auto& item : items) {
        if (!valid_item(item)) {
            return StackLayoutResult::invalid_input;
        }
    }

    const auto available_width = std::max(
        0.0,
        static_cast<double>(container.width) - container.padding.left - container.padding.right
    );
    const auto available_height = std::max(
        0.0,
        static_cast<double>(container.height) - container.padding.top - container.padding.bottom
    );
    bool overflow = false;

    for (std::size_t index = 0; index < items.size(); ++index) {
        const auto& item = items[index];
        const auto width = aligned_size(
            item.width,
            item.min_width,
            item.max_width,
            available_width,
            container.align_x
        );
        const auto height = aligned_size(
            item.height,
            item.min_height,
            item.max_height,
            available_height,
            container.align_y
        );
        const auto x =
            aligned_position(container.padding.left, available_width, width, container.align_x);
        const auto y =
            aligned_position(container.padding.top, available_height, height, container.align_y);

        output[index] = {
            .x = static_cast<float>(x),
            .y = static_cast<float>(y),
            .width = static_cast<float>(width),
            .height = static_cast<float>(height),
        };
        overflow = overflow || width > available_width || height > available_height;
    }

    return overflow ? StackLayoutResult::overflow : StackLayoutResult::complete;
}

}
