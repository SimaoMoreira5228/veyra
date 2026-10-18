#include "veyra/behaviors/list_reorder.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

bool same_order(std::span<const std::uint64_t> left, std::span<const std::uint64_t> right) {
    if (left.size() != right.size()) {
        return false;
    }

    for (std::size_t slot = 0; slot < left.size(); ++slot) {
        if (left[slot] != right[slot]) {
            return false;
        }
    }

    return true;
}

void check_move_down_shifts_destination_by_one() {
    constexpr std::size_t kCount = 5;

    require(
        veyra::adjust_reorder_destination(2, 5, kCount) == 4,
        "a post-removal drop at the end lands on the last slot"
    );
    require(
        veyra::adjust_reorder_destination(1, 3, kCount) == 2,
        "moving down two slots shifts the destination down by one"
    );
    require(
        veyra::adjust_reorder_destination(1, 2, kCount) == 1,
        "dropping one slot down is a no-op after the removal shift"
    );
}

void check_move_up_keeps_destination() {
    constexpr std::size_t kCount = 5;

    require(
        veyra::adjust_reorder_destination(3, 1, kCount) == 1,
        "moving up keeps the destination slot"
    );
    require(
        veyra::adjust_reorder_destination(4, 0, kCount) == 0,
        "moving the tail to the head keeps slot zero"
    );
    require(
        veyra::adjust_reorder_destination(2, 2, kCount) == 2,
        "dropping on the same slot keeps the slot"
    );
}

void check_invalid_ranges_report_sentinel() {
    constexpr std::size_t kCount = 5;

    require(
        veyra::adjust_reorder_destination(5, 2, kCount) == veyra::kInvalidReorderIndex,
        "a past-end source is invalid"
    );
    require(
        veyra::adjust_reorder_destination(1, 6, kCount) == veyra::kInvalidReorderIndex,
        "a destination past the end is invalid"
    );
    require(
        veyra::adjust_reorder_destination(0, 0, 0) == veyra::kInvalidReorderIndex,
        "an empty list has no valid destination"
    );
}

void check_apply_moves_ids_in_place() {
    std::array<std::uint64_t, 5> order{11, 22, 33, 44, 55};

    require(veyra::apply_reorder_index(order, 2, 5), "moving the middle row to the end applies");
    const std::array<std::uint64_t, 5> down_expected{11, 22, 44, 55, 33};
    require(same_order(order, down_expected), "moving down shifts the middle rows up");

    require(veyra::apply_reorder_index(order, 4, 1), "moving the tail upward applies");
    const std::array<std::uint64_t, 5> up_expected{11, 33, 22, 44, 55};
    require(same_order(order, up_expected), "moving up shifts the middle rows down");

    require(veyra::apply_reorder_index(order, 1, 2), "an adjacent move down applies");
    require(same_order(order, up_expected), "an adjacent move down leaves the order alone");

    require(!veyra::apply_reorder_index(order, 9, 1), "a bad source leaves the order alone");
    require(same_order(order, up_expected), "a rejected source keeps every id");
    require(!veyra::apply_reorder_index(order, 1, 9), "a bad destination leaves the order alone");
    require(same_order(order, up_expected), "a rejected destination keeps every id");
}

void check_apply_matches_remove_then_insert() {
    for (std::size_t from = 0; from < 5; ++from) {
        for (std::size_t to = 0; to <= 5; ++to) {
            std::array<std::uint64_t, 5> order{1, 2, 3, 4, 5};
            require(veyra::apply_reorder_index(order, from, to), "every in-range move applies");

            auto oracle = std::vector<std::uint64_t>{1, 2, 3, 4, 5};
            const auto moved = oracle[from];
            oracle.erase(oracle.begin() + static_cast<std::ptrdiff_t>(from));
            const auto at = from < to ? to - 1 : to;
            oracle.insert(oracle.begin() + static_cast<std::ptrdiff_t>(at), moved);

            require(same_order(order, oracle), "the applied order matches remove-then-insert");
        }
    }
}

}

int main() {
    try {
        check_move_down_shifts_destination_by_one();
        check_move_up_keeps_destination();
        check_invalid_ranges_report_sentinel();
        check_apply_moves_ids_in_place();
        check_apply_matches_remove_then_insert();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
