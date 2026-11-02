#include "veyra/layout/flex/flex.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

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

    switch (container.wrap) {
    case FlexWrap::no_wrap:
    case FlexWrap::wrap:
        break;
    default:
        return false;
    }

    switch (container.layout_direction) {
    case FlexLayoutDirection::left_to_right:
    case FlexLayoutDirection::right_to_left:
        break;
    default:
        return false;
    }

    switch (container.align_content) {
    case FlexAlignContent::start:
    case FlexAlignContent::center:
    case FlexAlignContent::end:
    case FlexAlignContent::space_between:
    case FlexAlignContent::stretch:
        break;
    default:
        return false;
    }

    switch (container.align_items) {
    case FlexAlign::start:
    case FlexAlign::center:
    case FlexAlign::end:
    case FlexAlign::stretch:
    case FlexAlign::baseline:
        break;
    default:
        return false;
    }

    if (container.direction == FlexDirection::column &&
        container.align_items == FlexAlign::baseline) {
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

void mirror_layout(const FlexContainer& container, std::span<LayoutRect> output) noexcept {
    if (container.layout_direction != FlexLayoutDirection::right_to_left) {
        return;
    }

    for (auto& rect : output) {
        rect.x = container.width - rect.x - rect.width;
    }
}

bool valid_baseline(const FlexItem& item) noexcept {
    if (!item.baseline) {
        return true;
    }

    const auto cross_size = std::clamp(item.cross_size, item.min_cross, item.max_cross);
    return is_finite_nonnegative(*item.baseline) && *item.baseline <= cross_size;
}

bool valid_item(const FlexItem& item) noexcept {
    return is_finite_nonnegative(item.basis) && is_finite_nonnegative(item.min_main) &&
        is_finite_nonnegative(item.max_main) && item.min_main <= item.max_main &&
        is_finite_nonnegative(item.grow) && is_finite_nonnegative(item.shrink) &&
        is_finite_nonnegative(item.cross_size) && is_finite_nonnegative(item.min_cross) &&
        is_finite_nonnegative(item.max_cross) && item.min_cross <= item.max_cross &&
        valid_baseline(item);
}

bool valid_style(const FlexItemStyle& item) noexcept {
    return (!item.basis || is_finite_nonnegative(*item.basis)) &&
        is_finite_nonnegative(item.min_main) &&
        (!item.max_main ||
         (is_finite_nonnegative(*item.max_main) && *item.max_main >= item.min_main)) &&
        is_finite_nonnegative(item.grow) && is_finite_nonnegative(item.shrink) &&
        is_finite_nonnegative(item.min_cross) &&
        (!item.max_cross ||
         (is_finite_nonnegative(*item.max_cross) && *item.max_cross >= item.min_cross));
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

LayoutConstraints
item_constraints(const FlexContainer& container, const FlexItemStyle& item) noexcept {
    const bool row = container.direction == FlexDirection::row;
    const auto available_main =
        std::max(0.0, main_extent(container, row) - main_padding(container, row));
    const auto available_cross =
        std::max(0.0, cross_extent(container, row) - cross_padding(container, row));
    const auto maximum_main = std::max(
        static_cast<double>(item.min_main),
        std::min(
            available_main,
            static_cast<double>(item.max_main.value_or(std::numeric_limits<float>::max()))
        )
    );
    const auto maximum_cross = std::max(
        static_cast<double>(item.min_cross),
        std::min(
            available_cross,
            static_cast<double>(item.max_cross.value_or(std::numeric_limits<float>::max()))
        )
    );

    if (row) {
        return {
            .width = {.minimum = item.min_main, .maximum = static_cast<float>(maximum_main)},
            .height = {.minimum = item.min_cross, .maximum = static_cast<float>(maximum_cross)},
        };
    }
    return {
        .width = {.minimum = item.min_cross, .maximum = static_cast<float>(maximum_cross)},
        .height = {.minimum = item.min_main, .maximum = static_cast<float>(maximum_main)},
    };
}

struct FlexLineMetrics {
    std::size_t end{};
    double cross_size{};
};

FlexLineMetrics next_flex_line(
    std::span<const FlexItem> items,
    std::size_t begin,
    double available_main,
    double gap,
    bool align_baseline
) noexcept {
    auto end = begin;
    double main_size = 0.0;
    double cross_size = 0.0;
    double baseline_ascent = 0.0;
    double baseline_descent = 0.0;

    while (end < items.size()) {
        const auto& item = items[end];
        const auto item_main_size = std::clamp(
            static_cast<double>(item.basis),
            static_cast<double>(item.min_main),
            static_cast<double>(item.max_main)
        );
        const auto next_main_size = main_size + (end == begin ? 0.0 : gap) + item_main_size;
        if (end > begin && next_main_size > available_main) {
            break;
        }

        main_size = next_main_size;
        const auto item_cross_size = std::clamp(
            static_cast<double>(item.cross_size),
            static_cast<double>(item.min_cross),
            static_cast<double>(item.max_cross)
        );
        cross_size = std::max(cross_size, item_cross_size);
        if (align_baseline) {
            const auto baseline =
                static_cast<double>(item.baseline.value_or(static_cast<float>(item_cross_size)));
            baseline_ascent = std::max(baseline_ascent, baseline);
            baseline_descent = std::max(baseline_descent, item_cross_size - baseline);
        }
        ++end;
    }

    cross_size = std::max(cross_size, baseline_ascent + baseline_descent);
    return {
        .end = end,
        .cross_size = cross_size,
    };
}

}

namespace {

FlexLayoutResult layout_flex_line(
    const FlexContainer& container,
    std::span<const FlexItem> items,
    std::span<LayoutRect> output
) noexcept {
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
    double baseline_ascent = 0.0;

    if (row && container.align_items == FlexAlign::baseline) {
        for (const auto& item : items) {
            const auto cross_size = std::clamp(
                static_cast<double>(item.cross_size),
                static_cast<double>(item.min_cross),
                static_cast<double>(item.max_cross)
            );
            baseline_ascent = std::max(
                baseline_ascent,
                static_cast<double>(item.baseline.value_or(static_cast<float>(cross_size)))
            );
        }
    }

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
    const auto coordinate_limit = static_cast<double>(std::numeric_limits<float>::max());
    bool cross_overflow = false;
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
        } else if (row && container.align_items == FlexAlign::baseline) {
            const auto baseline = item.baseline.value_or(static_cast<float>(cross_size));
            cross_offset = baseline_ascent - baseline;
        }

        const auto cross_position = cross_origin + cross_offset;
        if (!std::isfinite(main_position) || !std::isfinite(cross_position) ||
            main_position < -coordinate_limit || main_position > coordinate_limit ||
            cross_position < -coordinate_limit || cross_position > coordinate_limit) {
            return FlexLayoutResult::invalid_input;
        }

        cross_overflow =
            cross_overflow || cross_offset < 0.0 || cross_offset + cross_size > inner_cross;

        if (row) {
            output[index].x = static_cast<float>(main_position);
            output[index].y = static_cast<float>(cross_position);
            output[index].height = static_cast<float>(cross_size);
        } else {
            output[index].x = static_cast<float>(cross_position);
            output[index].y = static_cast<float>(main_position);
            output[index].width = static_cast<float>(cross_size);
        }

        main_position += main_size(output[index], row) + distributed_gap;
    }

    return free_main < 0.0 || cross_overflow ? FlexLayoutResult::overflow
                                             : FlexLayoutResult::complete;
}

FlexLayoutResult layout_flex_impl(
    const FlexContainer& container,
    std::span<const FlexItem> items,
    std::span<LayoutRect> output,
    bool validate_items
) noexcept {
    if (!valid_container(container)) {
        return FlexLayoutResult::invalid_input;
    }
    if (output.size() < items.size()) {
        return FlexLayoutResult::insufficient_output;
    }
    if (validate_items) {
        for (const auto& item : items) {
            if (!valid_item(item)) {
                return FlexLayoutResult::invalid_input;
            }
        }
    }

    if (container.wrap == FlexWrap::no_wrap || items.empty()) {
        const auto result = layout_flex_line(container, items, output);
        if (result == FlexLayoutResult::complete || result == FlexLayoutResult::overflow) {
            mirror_layout(container, output.first(items.size()));
        }
        return result;
    }

    const bool row = container.direction == FlexDirection::row;
    const auto available_main =
        std::max(0.0, main_extent(container, row) - main_padding(container, row));
    const auto available_cross =
        std::max(0.0, cross_extent(container, row) - cross_padding(container, row));
    const auto cross_origin = cross_leading_padding(container, row);
    const auto maximum_coordinate = static_cast<double>(std::numeric_limits<float>::max());
    double natural_cross_size = 0.0;
    std::size_t line_count = 0;
    for (std::size_t begin = 0; begin < items.size();) {
        const auto line = next_flex_line(
            items,
            begin,
            available_main,
            container.gap,
            container.align_items == FlexAlign::baseline
        );
        natural_cross_size += line.cross_size;
        ++line_count;
        begin = line.end;
    }

    const auto cross_gaps = container.gap * static_cast<double>(line_count - 1);
    const auto free_cross = available_cross - natural_cross_size - cross_gaps;
    const bool cross_overflow = free_cross < 0.0;
    double line_cross_position = 0.0;
    double line_cross_gap = container.gap;
    double line_stretch = 0.0;
    if (free_cross > 0.0) {
        switch (container.align_content) {
        case FlexAlignContent::start:
            break;
        case FlexAlignContent::center:
            line_cross_position = free_cross / 2.0;
            break;
        case FlexAlignContent::end:
            line_cross_position = free_cross;
            break;
        case FlexAlignContent::space_between:
            if (line_count > 1) {
                line_cross_gap += free_cross / static_cast<double>(line_count - 1);
            }
            break;
        case FlexAlignContent::stretch:
            line_stretch = free_cross / static_cast<double>(line_count);
            break;
        }
    }

    bool overflow = cross_overflow;
    for (std::size_t line_begin = 0; line_begin < items.size();) {
        const auto line = next_flex_line(
            items,
            line_begin,
            available_main,
            container.gap,
            container.align_items == FlexAlign::baseline
        );
        const auto line_end = line.end;
        const auto line_cross_size = line.cross_size + line_stretch;
        const auto line_cross_end = line_cross_position + line_cross_size;
        if (cross_origin + line_cross_end > maximum_coordinate ||
            line_cross_size > maximum_coordinate) {
            return FlexLayoutResult::invalid_input;
        }

        auto line_container = container;
        line_container.wrap = FlexWrap::no_wrap;
        if (row) {
            line_container.height = static_cast<float>(line_cross_size);
            line_container.padding.top = 0.0F;
            line_container.padding.bottom = 0.0F;
        } else {
            line_container.width = static_cast<float>(line_cross_size);
            line_container.padding.left = 0.0F;
            line_container.padding.right = 0.0F;
        }

        const auto line_result = layout_flex_line(
            line_container,
            items.subspan(line_begin, line_end - line_begin),
            output.subspan(line_begin, line_end - line_begin)
        );
        if (line_result == FlexLayoutResult::invalid_input) {
            return line_result;
        }
        overflow = overflow || line_result == FlexLayoutResult::overflow ||
            line_cross_end > available_cross;

        for (std::size_t index = line_begin; index < line_end; ++index) {
            if (row) {
                output[index].y =
                    static_cast<float>(cross_origin + line_cross_position + output[index].y);
            } else {
                output[index].x =
                    static_cast<float>(cross_origin + line_cross_position + output[index].x);
            }
        }

        line_cross_position = line_cross_end + line_cross_gap;
        line_begin = line.end;
    }

    mirror_layout(container, output.first(items.size()));

    return overflow ? FlexLayoutResult::overflow : FlexLayoutResult::complete;
}

}

FlexLayoutResult layout_flex(
    const FlexContainer& container,
    std::span<const FlexItem> items,
    std::span<LayoutRect> output
) noexcept {
    return layout_flex_impl(container, items, output, true);
}

bool flex_item_constraints(
    const FlexContainer& container,
    const FlexItemStyle& item,
    LayoutConstraints& constraints
) noexcept {
    if (!valid_container(container) || !valid_style(item)) {
        return false;
    }
    constraints = item_constraints(container, item);
    return true;
}

FlexLayoutResult layout_flex_measured(
    const FlexContainer& container,
    std::span<const FlexItemStyle> styles,
    std::span<const IntrinsicSize> intrinsic_sizes,
    std::span<FlexItem> resolved_items,
    std::span<LayoutRect> output
) noexcept {
    if (!valid_container(container)) {
        return FlexLayoutResult::invalid_input;
    }
    if (output.size() < styles.size()) {
        return FlexLayoutResult::insufficient_output;
    }
    if (intrinsic_sizes.size() < styles.size()) {
        return FlexLayoutResult::insufficient_measurements;
    }
    if (resolved_items.size() < styles.size()) {
        return FlexLayoutResult::insufficient_workspace;
    }

    const bool row = container.direction == FlexDirection::row;
    bool measurement_overflow = false;
    for (std::size_t index = 0; index < styles.size(); ++index) {
        const auto& style = styles[index];
        if (!valid_style(style)) {
            return FlexLayoutResult::invalid_input;
        }

        const auto constraints = item_constraints(container, style);
        MeasuredSize measured;
        const auto measure_result =
            constrain_measurement(constraints, intrinsic_sizes[index], measured);
        if (measure_result == MeasureResult::invalid_constraints ||
            measure_result == MeasureResult::invalid_intrinsic_size) {
            return FlexLayoutResult::invalid_input;
        }
        measurement_overflow = measurement_overflow || measure_result == MeasureResult::overflow;

        resolved_items[index] = {
            .basis =
                style.basis.value_or(row ? measured.width.preferred : measured.height.preferred),
            .min_main = style.min_main,
            .max_main = style.max_main.value_or(std::numeric_limits<float>::max()),
            .grow = style.grow,
            .shrink = style.shrink,
            .cross_size = row ? measured.height.preferred : measured.width.preferred,
            .min_cross = style.min_cross,
            .max_cross = style.max_cross.value_or(std::numeric_limits<float>::max()),
            .baseline = row ? measured.baseline : std::nullopt,
        };
    }

    const auto result = layout_flex_impl(
        container,
        std::span<const FlexItem>(resolved_items.data(), styles.size()),
        output,
        false
    );
    if (result != FlexLayoutResult::complete) {
        return result;
    }
    return measurement_overflow ? FlexLayoutResult::overflow : FlexLayoutResult::complete;
}

}
