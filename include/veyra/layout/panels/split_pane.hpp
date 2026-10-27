#ifndef VEYRA_LAYOUT_SPLIT_PANE_HPP
#define VEYRA_LAYOUT_SPLIT_PANE_HPP

#include "veyra/layout/flex/flex.hpp"
#include "veyra/layout/core/geometry.hpp"

#include <cmath>
#include <cstdint>

namespace veyra {

enum class SplitPaneResult : std::uint8_t {
    complete,
    clamped,
    invalid_input,
    unfulfillable,
};

struct SplitPaneRequest {
    LayoutRect total;
    float fraction{};
    float gap{};
    float min_first{};
    float min_second{};
    FlexDirection direction{FlexDirection::row};
};

struct SplitPaneLayout {
    LayoutRect first;
    LayoutRect second;
    float fraction{};
};

inline SplitPaneResult
resolve_split_pane(const SplitPaneRequest& request, SplitPaneLayout& output) noexcept {
    if (!std::isfinite(request.total.x) || !std::isfinite(request.total.y)) {
        return SplitPaneResult::invalid_input;
    }

    if (!std::isfinite(request.total.width) || !std::isfinite(request.total.height)) {
        return SplitPaneResult::invalid_input;
    }

    if (!std::isfinite(request.fraction) || !std::isfinite(request.gap)) {
        return SplitPaneResult::invalid_input;
    }

    if (!std::isfinite(request.min_first) || !std::isfinite(request.min_second)) {
        return SplitPaneResult::invalid_input;
    }

    if (request.total.width < 0.0F || request.total.height < 0.0F) {
        return SplitPaneResult::invalid_input;
    }

    if (request.gap < 0.0F || request.min_first < 0.0F || request.min_second < 0.0F) {
        return SplitPaneResult::invalid_input;
    }

    const bool is_row = request.direction == FlexDirection::row;
    const float extent = is_row ? request.total.width : request.total.height;
    const float available = extent - request.gap;

    if (available < 0.0F) {
        output.first = {request.total.x, request.total.y, 0.0F, 0.0F};
        output.second = {request.total.x, request.total.y, 0.0F, 0.0F};
        output.fraction = 0.0F;

        return SplitPaneResult::unfulfillable;
    }

    float clamped_fraction = request.fraction;

    if (clamped_fraction < 0.0F) {
        clamped_fraction = 0.0F;
    }

    if (clamped_fraction > 1.0F) {
        clamped_fraction = 1.0F;
    }

    const float mins = request.min_first + request.min_second;

    float first_size = 0.0F;
    float effective;
    auto result = SplitPaneResult::complete;

    if (clamped_fraction != request.fraction) {
        result = SplitPaneResult::clamped;
    }

    if (mins > available) {
        if (mins > 0.0F && available > 0.0F) {
            first_size = available * (request.min_first / mins);
            effective = first_size / available;
        } else {
            first_size = 0.0F;
            effective = 0.0F;
        }

        result = SplitPaneResult::unfulfillable;
    } else {
        const float desired = clamped_fraction * available;
        const float lower = request.min_first;
        const float upper = available - request.min_second;

        first_size = desired;

        if (first_size < lower) {
            first_size = lower;
        }

        if (first_size > upper) {
            first_size = upper;
        }

        if (available > 0.0F) {
            effective = first_size / available;
        } else {
            effective = 0.0F;
        }

        if (first_size != desired) {
            result = SplitPaneResult::clamped;
        }
    }

    const float second_size = available - first_size;

    if (is_row) {
        output.first = {request.total.x, request.total.y, first_size, request.total.height};
        output.second = {
            request.total.x + first_size + request.gap,
            request.total.y,
            second_size,
            request.total.height,
        };
    } else {
        output.first = {request.total.x, request.total.y, request.total.width, first_size};
        output.second = {
            request.total.x,
            request.total.y + first_size + request.gap,
            request.total.width,
            second_size,
        };
    }

    output.fraction = effective;

    return result;
}

}

#endif
