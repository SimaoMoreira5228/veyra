#include "veyra/layout/flex.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace veyra {
namespace {

bool is_finite_nonnegative(float value) noexcept {
    return std::isfinite(value) && value >= 0.0F;
}

bool valid_container(const FlexContainer& container) noexcept {
    if (!is_finite_nonnegative(container.width) || !is_finite_nonnegative(container.height) ||
        !is_finite_nonnegative(container.padding.top) ||
        !is_finite_nonnegative(container.padding.right) ||
        !is_finite_nonnegative(container.padding.bottom) ||
        !is_finite_nonnegative(container.padding.left) || !is_finite_nonnegative(container.gap)) {
        return false;
    }

    switch (container.direction) {
    case FlexDirection::row:
    case FlexDirection::column:
        break;
    default:
        return false;
    }

    switch (container.align_items) {
    case FlexAlign::start:
    case FlexAlign::center:
    case FlexAlign::end:
    case FlexAlign::stretch:
        break;
    default:
        return false;
    }

    switch (container.justify_content) {
    case FlexJustify::start:
    case FlexJustify::center:
    case FlexJustify::end:
    case FlexJustify::space_between:
        return true;
    }
    return false;
}

bool valid_item(const FlexItem& item) noexcept {
    return is_finite_nonnegative(item.basis) && is_finite_nonnegative(item.min_main) &&
        is_finite_nonnegative(item.max_main) && item.min_main <= item.max_main &&
        is_finite_nonnegative(item.grow) && is_finite_nonnegative(item.shrink) &&
        is_finite_nonnegative(item.cross_size) && is_finite_nonnegative(item.min_cross) &&
        is_finite_nonnegative(item.max_cross) && item.min_cross <= item.max_cross;
}

double main_size(const LayoutRect& rect, bool row) noexcept {
    return row ? rect.width : rect.height;
}

void set_main_size(LayoutRect& rect, bool row, double size) noexcept {
    if (row) {
        rect.width = static_cast<float>(size);
        return;
    }
    rect.height = static_cast<float>(size);
}

double item_weight(const FlexItem& item, const LayoutRect& rect, bool growing, bool row) noexcept {
    const auto size = main_size(rect, row);
    if (growing) {
        return size < item.max_main ? item.grow : 0.0;
    }
    return size > item.min_main ? static_cast<double>(item.shrink) * item.basis : 0.0;
}

double main_extent(const FlexContainer& container, bool row) noexcept {
    return row ? container.width : container.height;
}

double cross_extent(const FlexContainer& container, bool row) noexcept {
    return row ? container.height : container.width;
}

double main_leading_padding(const FlexContainer& container, bool row) noexcept {
    return row ? container.padding.left : container.padding.top;
}

double cross_leading_padding(const FlexContainer& container, bool row) noexcept {
    return row ? container.padding.top : container.padding.left;
}

double main_padding(const FlexContainer& container, bool row) noexcept {
    return row ? static_cast<double>(container.padding.left) + container.padding.right
               : static_cast<double>(container.padding.top) + container.padding.bottom;
}

double cross_padding(const FlexContainer& container, bool row) noexcept {
    return row ? static_cast<double>(container.padding.top) + container.padding.bottom
               : static_cast<double>(container.padding.left) + container.padding.right;
}

}

FlexLayoutResult layout_flex(
    const FlexContainer& container,
    std::span<const FlexItem> items,
    std::span<LayoutRect> output
) noexcept {
    if (!valid_container(container)) {
        return FlexLayoutResult::invalid_input;
    }
    if (output.size() < items.size()) {
        return FlexLayoutResult::insufficient_output;
    }
    for (const auto& item : items) {
        if (!valid_item(item)) {
            return FlexLayoutResult::invalid_input;
        }
    }

    const bool row = container.direction == FlexDirection::row;
    const auto leading_main = main_leading_padding(container, row);
    const auto gaps = items.empty() ? 0.0 : container.gap * static_cast<double>(items.size() - 1);
    double initial_main = leading_main + gaps;
    double minimum_main = initial_main;
    double shrink_weight = 0.0;
    for (const auto& item : items) {
        const auto basis = std::clamp(
            static_cast<double>(item.basis),
            static_cast<double>(item.min_main),
            static_cast<double>(item.max_main)
        );
        initial_main += basis;
        minimum_main += item.min_main;
        if (basis > item.min_main) {
            shrink_weight += static_cast<double>(item.shrink) * basis;
        }
    }
    const auto max_coordinate = static_cast<double>(std::numeric_limits<float>::max());
    if (minimum_main > max_coordinate || (initial_main > max_coordinate && shrink_weight == 0.0)) {
        return FlexLayoutResult::invalid_input;
    }

    const auto inner_main =
        std::max(0.0, main_extent(container, row) - main_padding(container, row));
    const auto inner_cross =
        std::max(0.0, cross_extent(container, row) - cross_padding(container, row));
    double used_main = gaps;

    for (std::size_t index = 0; index < items.size(); ++index) {
        const auto& item = items[index];
        const auto basis = std::clamp(
            static_cast<double>(item.basis),
            static_cast<double>(item.min_main),
            static_cast<double>(item.max_main)
        );
        set_main_size(output[index], row, basis);
        used_main += basis;
    }

    double free_main = inner_main - used_main;
    for (std::size_t pass = 0; pass < items.size() && free_main != 0.0; ++pass) {
        const bool growing = free_main > 0.0;
        double total_weight = 0.0;
        for (std::size_t index = 0; index < items.size(); ++index) {
            total_weight += item_weight(items[index], output[index], growing, row);
        }
        if (total_weight == 0.0) {
            break;
        }

        bool hit_limit = false;
        for (std::size_t index = 0; index < items.size(); ++index) {
            const auto& item = items[index];
            const auto weight = item_weight(item, output[index], growing, row);
            if (weight == 0.0) {
                continue;
            }

            const auto current = main_size(output[index], row);
            const auto proposed = current + free_main * (weight / total_weight);
            const auto minimum = static_cast<double>(item.min_main);
            const auto maximum = static_cast<double>(item.max_main);
            const auto next = std::clamp(proposed, minimum, maximum);
            set_main_size(output[index], row, next);
            hit_limit = hit_limit || next != proposed;
        }

        used_main = gaps;
        for (std::size_t index = 0; index < items.size(); ++index) {
            used_main += main_size(output[index], row);
        }
        free_main = inner_main - used_main;
        if (!hit_limit) {
            break;
        }
    }

    double offset = 0.0;
    double distributed_gap = container.gap;
    if (free_main > 0.0) {
        switch (container.justify_content) {
        case FlexJustify::start:
            break;
        case FlexJustify::center:
            offset = free_main / 2.0;
            break;
        case FlexJustify::end:
            offset = free_main;
            break;
        case FlexJustify::space_between:
            if (items.size() > 1) {
                distributed_gap += free_main / static_cast<double>(items.size() - 1);
            }
            break;
        }
    }

    auto main_position = main_leading_padding(container, row) + offset;
    const auto cross_origin = cross_leading_padding(container, row);
    for (std::size_t index = 0; index < items.size(); ++index) {
        const auto& item = items[index];
        auto cross_size = std::clamp(
            static_cast<double>(item.cross_size),
            static_cast<double>(item.min_cross),
            static_cast<double>(item.max_cross)
        );
        if (container.align_items == FlexAlign::stretch) {
            cross_size = std::clamp(
                inner_cross,
                static_cast<double>(item.min_cross),
                static_cast<double>(item.max_cross)
            );
        }

        double cross_offset = 0.0;
        if (container.align_items == FlexAlign::center) {
            cross_offset = (inner_cross - cross_size) / 2.0;
        } else if (container.align_items == FlexAlign::end) {
            cross_offset = inner_cross - cross_size;
        }

        if (row) {
            output[index].x = static_cast<float>(main_position);
            output[index].y = static_cast<float>(cross_origin + cross_offset);
            output[index].height = static_cast<float>(cross_size);
        } else {
            output[index].x = static_cast<float>(cross_origin + cross_offset);
            output[index].y = static_cast<float>(main_position);
            output[index].width = static_cast<float>(cross_size);
        }

        main_position += main_size(output[index], row) + distributed_gap;
    }

    return free_main < 0.0 ? FlexLayoutResult::overflow : FlexLayoutResult::complete;
}

}
