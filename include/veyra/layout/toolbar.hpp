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
    float collapsed_width{};
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
    std::size_t folded_count{};
    bool show_overflow{};
};

inline ToolbarResult resolve_toolbar_overflow(
    const ToolbarRequest& request,
    std::span<std::uint64_t> visible_ids,
    std::span<std::uint64_t> overflow_ids,
    std::span<std::uint64_t> folded_ids,
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

    if (folded_ids.size() < count) {
        return ToolbarResult::invalid_input;
    }

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

        if (!std::isfinite(item.collapsed_width)) {
            return ToolbarResult::invalid_input;
        }

        if (item.collapsed_width < 0.0F) {
            return ToolbarResult::invalid_input;
        }

        if (item.collapsed_width > item.width) {
            return ToolbarResult::invalid_input;
        }
    }

    if (count == 0) {
        output.visible_count = 0;
        output.overflow_count = 0;
        output.folded_count = 0;
        output.show_overflow = false;

        return ToolbarResult::complete;
    }

    const auto is_foldable = [&](std::size_t index) noexcept {
        const float collapsed = request.items[index].collapsed_width;
        return collapsed > 0.0F && collapsed < request.items[index].width;
    };

    const auto effective_width = [&](std::size_t index, std::size_t fold_from) noexcept {
        if (index >= fold_from && is_foldable(index)) {
            return request.items[index].collapsed_width;
        }

        return request.items[index].width;
    };

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
        output.folded_count = 0;
        output.show_overflow = false;

        return ToolbarResult::complete;
    }

    std::size_t fold_from = count;
    float folded_total = total;

    for (std::size_t step = 0; step < count; ++step) {
        if (folded_total <= request.available_width) {
            break;
        }

        const std::size_t index = count - 1 - step;

        if (is_foldable(index)) {
            folded_total -= request.items[index].width - request.items[index].collapsed_width;
            fold_from = index;
        }
    }

    if (folded_total <= request.available_width) {
        std::size_t folded = 0;

        for (std::size_t index = 0; index < count; ++index) {
            visible_ids[index] = request.items[index].id;

            if (index >= fold_from && is_foldable(index)) {
                folded_ids[folded] = request.items[index].id;
                ++folded;
            }
        }

        output.visible_count = count;
        output.overflow_count = 0;
        output.folded_count = folded;
        output.show_overflow = false;

        return ToolbarResult::complete;
    }

    float pinned_sum = 0.0F;
    std::size_t pinned_count = 0;

    for (std::size_t index = 0; index < count; ++index) {
        if (request.items[index].pinned) {
            pinned_sum += effective_width(index, fold_from);
            ++pinned_count;
        }
    }

    float pinned_min = pinned_sum;

    if (pinned_count > 0) {
        pinned_min += request.gap * static_cast<float>(pinned_count - 1);
    }

    if (pinned_min > request.available_width) {
        std::size_t kept = 0;
        std::size_t spilled = 0;
        std::size_t folded = 0;

        for (std::size_t index = 0; index < count; ++index) {
            if (request.items[index].pinned) {
                visible_ids[kept] = request.items[index].id;
                ++kept;

                if (index >= fold_from && is_foldable(index)) {
                    folded_ids[folded] = request.items[index].id;
                    ++folded;
                }
            } else {
                overflow_ids[spilled] = request.items[index].id;
                ++spilled;
            }
        }

        output.visible_count = kept;
        output.overflow_count = spilled;
        output.folded_count = folded;
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
    std::size_t folded = 0;

    for (std::size_t index = 0; index < count; ++index) {
        const ToolbarItem& item = request.items[index];

        if (item.pinned) {
            visible_ids[visible] = item.id;
            ++visible;

            if (index >= fold_from && is_foldable(index)) {
                folded_ids[folded] = item.id;
                ++folded;
            }

            continue;
        }

        float cost = effective_width(index, fold_from);

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

            if (index >= fold_from && is_foldable(index)) {
                folded_ids[folded] = item.id;
                ++folded;
            }
        } else {
            overflow_ids[overflowed] = item.id;
            ++overflowed;
        }
    }

    output.visible_count = visible;
    output.overflow_count = overflowed;
    output.folded_count = folded;
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
