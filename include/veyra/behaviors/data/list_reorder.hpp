#ifndef VEYRA_BEHAVIORS_LIST_REORDER_HPP
#define VEYRA_BEHAVIORS_LIST_REORDER_HPP

#include <cstddef>
#include <cstdint>
#include <span>

namespace veyra {

constexpr std::size_t kInvalidReorderIndex = static_cast<std::size_t>(-1);

constexpr std::size_t adjust_reorder_destination(
    std::size_t from_index,
    std::size_t to_index,
    std::size_t count
) noexcept {
    if (from_index >= count || to_index > count) {
        return kInvalidReorderIndex;
    }

    if (from_index < to_index) {
        return to_index - 1;
    }

    return to_index;
}

inline bool apply_reorder_index(
    std::span<std::uint64_t> order,
    std::size_t from_index,
    std::size_t to_index
) noexcept {
    const auto at = adjust_reorder_destination(from_index, to_index, order.size());

    if (at == kInvalidReorderIndex) {
        return false;
    }

    if (at == from_index) {
        return true;
    }

    const auto moved = order[from_index];

    if (at > from_index) {
        for (auto slot = from_index; slot < at; ++slot) {
            order[slot] = order[slot + 1];
        }
    } else {
        for (auto slot = from_index; slot > at; --slot) {
            order[slot] = order[slot - 1];
        }
    }

    order[at] = moved;

    return true;
}

}

#endif
