#include "veyra/behaviors/tree_cursor.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using veyra::TreeCursorMove;
using veyra::TreeCursorResult;
using veyra::TreeCursorRow;

static_assert(noexcept(veyra::resolve_tree_cursor(
    std::span<const TreeCursorRow>{},
    std::uint64_t{},
    TreeCursorMove::up
)));

constexpr std::array<veyra::TreeCursorRow, 6> kRows{{
    {.id = 1, .depth = 0, .has_children = true, .expanded = true},
    {.id = 2, .depth = 1, .has_children = true, .expanded = true},
    {.id = 3, .depth = 2, .has_children = false, .expanded = false},
    {.id = 4, .depth = 1, .has_children = true, .expanded = false},
    {.id = 5, .depth = 0, .has_children = true, .expanded = false},
    {.id = 6, .depth = 0, .has_children = false, .expanded = false},
}};

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

void require_outcome(
    std::span<const TreeCursorRow> rows,
    std::uint64_t focused,
    TreeCursorMove move,
    TreeCursorResult result,
    std::uint64_t id
) {
    const veyra::TreeCursorOutcome outcome = veyra::resolve_tree_cursor(rows, focused, move);

    require(
        outcome.result == result && outcome.id == id,
        "cursor resolution reports the expected result and id"
    );
}

void check_vertical_and_jump_moves() {
    require_outcome(kRows, 1, TreeCursorMove::down, TreeCursorResult::moved, 2);
    require_outcome(kRows, 3, TreeCursorMove::down, TreeCursorResult::moved, 4);
    require_outcome(kRows, 6, TreeCursorMove::down, TreeCursorResult::handled, 6);

    require_outcome(kRows, 6, TreeCursorMove::up, TreeCursorResult::moved, 5);
    require_outcome(kRows, 2, TreeCursorMove::up, TreeCursorResult::moved, 1);
    require_outcome(kRows, 1, TreeCursorMove::up, TreeCursorResult::handled, 1);

    require_outcome(kRows, 4, TreeCursorMove::home, TreeCursorResult::moved, 1);
    require_outcome(kRows, 1, TreeCursorMove::home, TreeCursorResult::handled, 1);

    require_outcome(kRows, 2, TreeCursorMove::end, TreeCursorResult::moved, 6);
    require_outcome(kRows, 6, TreeCursorMove::end, TreeCursorResult::handled, 6);
}

void check_right_reports_expand_or_first_child() {
    require_outcome(kRows, 5, TreeCursorMove::right, TreeCursorResult::expand, 5);
    require_outcome(kRows, 4, TreeCursorMove::right, TreeCursorResult::expand, 4);

    require_outcome(kRows, 1, TreeCursorMove::right, TreeCursorResult::moved, 2);
    require_outcome(kRows, 2, TreeCursorMove::right, TreeCursorResult::moved, 3);

    require_outcome(kRows, 3, TreeCursorMove::right, TreeCursorResult::handled, 3);
    require_outcome(kRows, 6, TreeCursorMove::right, TreeCursorResult::handled, 6);
}

void check_right_without_visible_child_is_handled() {
    constexpr std::array<TreeCursorRow, 1> lone{{
        {.id = 7, .depth = 0, .has_children = true, .expanded = true},
    }};
    require_outcome(lone, 7, TreeCursorMove::right, TreeCursorResult::handled, 7);

    constexpr std::array<TreeCursorRow, 2> sibling{{
        {.id = 7, .depth = 0, .has_children = true, .expanded = true},
        {.id = 8, .depth = 0, .has_children = false, .expanded = false},
    }};
    require_outcome(sibling, 7, TreeCursorMove::right, TreeCursorResult::handled, 7);
}

void check_left_reports_collapse_or_parent() {
    require_outcome(kRows, 1, TreeCursorMove::left, TreeCursorResult::collapse, 1);
    require_outcome(kRows, 2, TreeCursorMove::left, TreeCursorResult::collapse, 2);

    require_outcome(kRows, 4, TreeCursorMove::left, TreeCursorResult::moved, 1);
    require_outcome(kRows, 3, TreeCursorMove::left, TreeCursorResult::moved, 2);

    require_outcome(kRows, 5, TreeCursorMove::left, TreeCursorResult::handled, 5);
    require_outcome(kRows, 6, TreeCursorMove::left, TreeCursorResult::handled, 6);
}

void check_rejected_calls_are_ignored() {
    constexpr std::array<TreeCursorRow, 0> none{};
    require_outcome(none, 1, TreeCursorMove::down, TreeCursorResult::ignored, 0);

    require_outcome(kRows, 99, TreeCursorMove::down, TreeCursorResult::ignored, 0);
    require_outcome(kRows, 99, TreeCursorMove::right, TreeCursorResult::ignored, 0);
    require_outcome(kRows, 0, TreeCursorMove::left, TreeCursorResult::ignored, 0);
}

void check_malformed_depth_rejects_the_call() {
    constexpr std::array<TreeCursorRow, 3> jumped{{
        {.id = 1, .depth = 0, .has_children = true, .expanded = true},
        {.id = 9, .depth = 2, .has_children = false, .expanded = false},
        {.id = 6, .depth = 0, .has_children = false, .expanded = false},
    }};

    require_outcome(jumped, 1, TreeCursorMove::down, TreeCursorResult::ignored, 0);
    require_outcome(jumped, 9, TreeCursorMove::up, TreeCursorResult::ignored, 0);
    require_outcome(jumped, 6, TreeCursorMove::left, TreeCursorResult::ignored, 0);
    require_outcome(jumped, 1, TreeCursorMove::right, TreeCursorResult::ignored, 0);
}

}

int main() {
    try {
        check_vertical_and_jump_moves();
        check_right_reports_expand_or_first_child();
        check_right_without_visible_child_is_handled();
        check_left_reports_collapse_or_parent();
        check_rejected_calls_are_ignored();
        check_malformed_depth_rejects_the_call();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
