#ifndef VEYRA_LAYOUT_TOOLBAR_HPP
#define VEYRA_LAYOUT_TOOLBAR_HPP

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>

namespace veyra {

enum class ToolbarResult : std::uint8_t {
    complete,
    overflow,
    invalid_input,
    unfulfillable,
};

struct ToolbarItem {
    std::uint64_t id{};
    float width{};
    bool pinned{};
};

struct ToolbarRequest {
    std::span<const ToolbarItem> items;
    float available_width{};
    float overflow_button_width{};
    float gap{};
};

struct ToolbarLayout {
    std::size_t visible_count{};
    std::size_t overflow_count{};
    bool show_overflow{};
};

inline ToolbarResult resolve_toolbar_overflow(
    const ToolbarRequest& request,
    std::span<std::uint64_t> visible_ids,
    std::span<std::uint64_t> overflow_ids,
    ToolbarLayout& output
) noexcept {
    if (!std::isfinite(request.available_width)) {
        return ToolbarResult::invalid_input;
    }

    if (!std::isfinite(request.overflow_button_width)) {
        return ToolbarResult::invalid_input;
    }

    if (!std::isfinite(request.gap)) {
        return ToolbarResult::invalid_input;
    }

    if (request.available_width < 0.0F) {
        return ToolbarResult::invalid_input;
    }

    if (request.overflow_button_width < 0.0F) {
        return ToolbarResult::invalid_input;
    }

    if (request.gap < 0.0F) {
        return ToolbarResult::invalid_input;
    }

    const std::size_t count = request.items.size();

    if (visible_ids.size() < count) {
        return ToolbarResult::invalid_input;
    }

    if (overflow_ids.size() < count) {
        return ToolbarResult::invalid_input;
    }

    float pinned_sum = 0.0F;
    std::size_t pinned_count = 0;

    for (const ToolbarItem& item : request.items) {
        if (item.id == 0) {
            return ToolbarResult::invalid_input;
        }

        if (!std::isfinite(item.width)) {
            return ToolbarResult::invalid_input;
        }

        if (item.width < 0.0F) {
            return ToolbarResult::invalid_input;
        }

        if (item.pinned) {
            pinned_sum += item.width;
            ++pinned_count;
        }
    }

    if (count == 0) {
        output.visible_count = 0;
        output.overflow_count = 0;
        output.show_overflow = false;

        return ToolbarResult::complete;
    }

    float total = 0.0F;

    for (const ToolbarItem& item : request.items) {
        total += item.width;
    }

    total += request.gap * static_cast<float>(count - 1);

    if (total <= request.available_width) {
        for (std::size_t index = 0; index < count; ++index) {
            visible_ids[index] = request.items[index].id;
        }

        output.visible_count = count;
        output.overflow_count = 0;
        output.show_overflow = false;

        return ToolbarResult::complete;
    }

    float pinned_min = pinned_sum;

    if (pinned_count > 0) {
        pinned_min += request.gap * static_cast<float>(pinned_count - 1);
    }

    if (pinned_min > request.available_width) {
        std::size_t kept = 0;
        std::size_t spilled = 0;

        for (const ToolbarItem& item : request.items) {
            if (item.pinned) {
                visible_ids[kept] = item.id;
                ++kept;
            } else {
                overflow_ids[spilled] = item.id;
                ++spilled;
            }
        }

        output.visible_count = kept;
        output.overflow_count = spilled;
        output.show_overflow = false;

        if (spilled > 0 && request.overflow_button_width > 0.0F) {
            const float button_need = pinned_min + request.gap + request.overflow_button_width;
            output.show_overflow = button_need <= request.available_width;
        }

        return ToolbarResult::unfulfillable;
    }

    // NOTE: pinned widths sit in `used` already, so the ordered pass below
    // appends pinned ids without charging them a second time.
    float used = pinned_min;
    std::size_t accounted = pinned_count;
    std::size_t visible = 0;
    std::size_t overflowed = 0;

    for (const ToolbarItem& item : request.items) {
        if (item.pinned) {
            visible_ids[visible] = item.id;
            ++visible;

            continue;
        }

        float cost = item.width;

        if (accounted > 0) {
            cost += request.gap;
        }

        float need = used + cost;

        if (request.overflow_button_width > 0.0F) {
            need += request.overflow_button_width + request.gap;
        }

        if (need <= request.available_width) {
            visible_ids[visible] = item.id;
            ++visible;
            ++accounted;
            used += cost;
        } else {
            overflow_ids[overflowed] = item.id;
            ++overflowed;
        }
    }

    output.visible_count = visible;
    output.overflow_count = overflowed;
    output.show_overflow = false;

    if (overflowed > 0 && request.overflow_button_width > 0.0F) {
        if (visible == 0) {
            output.show_overflow = request.overflow_button_width <= request.available_width;
        } else {
            const float button_need = used + request.gap + request.overflow_button_width;
            output.show_overflow = button_need <= request.available_width;
        }
    }

    return ToolbarResult::overflow;
}

}

#endif
