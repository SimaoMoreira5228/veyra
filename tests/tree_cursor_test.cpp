#include "veyra/behaviors/selection/tree_cursor.hpp"

#include "veyra/behaviors/selection/selection.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using veyra::SelectionMode;
using veyra::SelectionResult;
using veyra::TreeCursorMove;
using veyra::TreeCursorResult;
using veyra::TreeCursorRow;
using veyra::TreeExpansion;

static_assert(noexcept(veyra::resolve_tree_cursor(
    std::span<const TreeCursorRow>{},
    std::uint64_t{},
    TreeCursorMove::up
)));
static_assert(noexcept(veyra::tree_path_length(std::uint64_t{}, std::span<const TreeCursorRow>{})));
static_assert(noexcept(veyra::copy_tree_path(
    std::uint64_t{},
    std::span<const TreeCursorRow>{},
    std::span<std::uint64_t>{}
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

constexpr std::array<TreeCursorRow, 7> kLinked{{
    {.id = 1, .parent = 0, .depth = 0, .has_children = true, .expanded = true},
    {.id = 2, .parent = 1, .depth = 1, .has_children = true, .expanded = true},
    {.id = 3, .parent = 2, .depth = 2, .has_children = false, .expanded = false},
    {.id = 4,
     .parent = 1,
     .depth = 1,
     .has_children = true,
     .expanded = false,
     .children_known = false},
    {.id = 5, .parent = 4, .depth = 2, .has_children = false, .expanded = false},
    {.id = 6, .parent = 0, .depth = 0, .has_children = false, .expanded = false},
    {.id = 7, .parent = 0, .depth = 0, .has_children = true, .expanded = false},
}};

void check_expansion_set_basics() {
    auto expansion = TreeExpansion{};
    require(expansion.empty(), "a new expansion set holds nothing");
    require(expansion.count() == 0, "a new expansion set counts zero");

    require(
        expansion.expand(1) == TreeCursorResult::expand && expansion.is_expanded(1),
        "expanding an id reports expand and stores it"
    );
    require(
        expansion.expand(1) == TreeCursorResult::handled && expansion.count() == 1,
        "expanding a held id suppresses the duplicate"
    );
    require(
        expansion.collapse(9) == TreeCursorResult::handled && expansion.is_expanded(1),
        "collapsing an absent id keeps the set"
    );
    require(
        expansion.collapse(1) == TreeCursorResult::collapse && expansion.empty(),
        "collapsing the held id drops it"
    );
    require(
        expansion.collapse(1) == TreeCursorResult::handled,
        "collapsing an already-collapsed id is handled"
    );

    require(expansion.expand(0) == TreeCursorResult::ignored, "the null id never expands");
    require(expansion.toggle(0) == TreeCursorResult::ignored, "the null id never toggles");

    require(
        expansion.toggle(2) == TreeCursorResult::expand && expansion.is_expanded(2),
        "toggling a collapsed id expands it"
    );
    require(
        expansion.toggle(2) == TreeCursorResult::collapse && !expansion.is_expanded(2),
        "toggling an expanded id collapses it"
    );

    require(
        expansion.collapse_all() == TreeCursorResult::handled,
        "collapsing an empty set reports no change"
    );
    require(expansion.expand(3) == TreeCursorResult::expand, "expanding a third id works");

    std::array<std::uint64_t, TreeExpansion::kCapacity> seen{};
    const std::size_t copied = expansion.copy_expanded(seen);
    require(copied == 1 && seen[0] == 3, "copy_expanded reports the held ids");

    require(
        expansion.collapse_all() == TreeCursorResult::collapse && expansion.empty(),
        "collapse_all drops the set at once"
    );
}

void check_expansion_bulk_restore_is_atomic() {
    auto expansion = TreeExpansion{};

    require(
        expansion.expand_all(std::span<const std::uint64_t>{}) == TreeCursorResult::handled,
        "restoring nothing reports no change"
    );
    require(
        expansion.expand_all(std::array<std::uint64_t, 3>{1, 2, 2}) == TreeCursorResult::expand &&
            expansion.count() == 2,
        "restore dedupes the input"
    );
    require(
        expansion.expand_all(std::array<std::uint64_t, 2>{2, 1}) == TreeCursorResult::handled,
        "restoring held ids reports no change"
    );

    std::array<std::uint64_t, TreeExpansion::kCapacity + 1> overflow{};
    for (std::size_t i = 0; i < overflow.size(); ++i) {
        overflow[i] = static_cast<std::uint64_t>(i + 1);
    }

    const std::size_t before = expansion.count();
    require(
        expansion.expand_all(overflow) == TreeCursorResult::ignored && expansion.count() == before,
        "an oversized restore is rejected without partial state"
    );

    auto full = TreeExpansion{};
    std::array<std::uint64_t, TreeExpansion::kCapacity> ids{};
    for (std::size_t i = 0; i < ids.size(); ++i) {
        ids[i] = static_cast<std::uint64_t>(i + 1);
    }

    require(
        full.expand_all(ids) == TreeCursorResult::expand &&
            full.count() == TreeExpansion::kCapacity,
        "a full restore fills the set"
    );
    require(
        full.expand(TreeExpansion::kCapacity + 1) == TreeCursorResult::ignored &&
            full.count() == TreeExpansion::kCapacity,
        "expanding past capacity is rejected"
    );
}

void check_lazy_children_gate_expansion() {
    auto expansion = TreeExpansion{};

    require(
        expansion.expand(4, kLinked) == TreeCursorResult::needs_children &&
            !expansion.is_expanded(4),
        "expanding an unloaded node reports needs_children instead"
    );
    require(
        expansion.expand(4, kLinked) == TreeCursorResult::needs_children,
        "repeating the request while loading still needs children"
    );
    require(
        expansion.confirm_children(4) == TreeCursorResult::expand && expansion.is_expanded(4),
        "confirming the load honors the earlier request"
    );
    require(
        expansion.confirm_children(4) == TreeCursorResult::handled,
        "confirming twice reports no change"
    );

    require(
        expansion.expand(2, kLinked) == TreeCursorResult::expand,
        "expanding a loaded node expands"
    );
    require(
        expansion.expand(3, kLinked) == TreeCursorResult::handled && !expansion.is_expanded(3),
        "expanding a leaf never enters the set"
    );
    require(
        expansion.expand(99, kLinked) == TreeCursorResult::ignored,
        "expanding an unknown id is ignored"
    );
    require(
        expansion.confirm_children(7) == TreeCursorResult::handled,
        "confirming without a request is a no-op"
    );
}

void check_collapse_cancels_pending_loads() {
    auto expansion = TreeExpansion{};
    require(expansion.expand(4, kLinked) == TreeCursorResult::needs_children, "the load starts");
    require(!expansion.empty(), "a pending load means the set is not empty");

    require(
        expansion.collapse(4) == TreeCursorResult::collapse && expansion.empty(),
        "collapsing a pending node cancels the request"
    );
    require(
        expansion.confirm_children(4) == TreeCursorResult::handled && !expansion.is_expanded(4),
        "a late load for a cancelled request expands nothing"
    );
}

void check_path_identity_runs_root_first() {
    require(veyra::tree_path_length(1, kLinked) == 1, "a root path holds one id");
    require(veyra::tree_path_length(3, kLinked) == 3, "a nested path counts every ancestor");
    require(veyra::tree_path_length(5, kLinked) == 3, "hidden descendants still resolve");
    require(veyra::tree_path_length(99, kLinked) == 0, "an unknown id has no path");
    require(veyra::tree_path_length(0, kLinked) == 0, "the null id has no path");

    std::array<std::uint64_t, 8> path{};
    require(
        veyra::copy_tree_path(3, kLinked, path) == 3 && path[0] == 1 && path[1] == 2 &&
            path[2] == 3,
        "the copied path runs from the root to the id"
    );

    std::array<std::uint64_t, 1> root{};
    require(
        veyra::copy_tree_path(6, kLinked, root) == 1 && root[0] == 6,
        "a root copies to a single id"
    );

    std::array<std::uint64_t, 2> short_path{};
    require(
        veyra::copy_tree_path(3, kLinked, short_path) == 0,
        "a short buffer copies nothing instead of truncating"
    );
    require(veyra::copy_tree_path(99, kLinked, path) == 0, "an unknown id copies nothing");
}

void check_path_identity_rejects_broken_links() {
    constexpr std::array<TreeCursorRow, 2> cycled{{
        {.id = 1, .parent = 2, .depth = 0, .has_children = true, .expanded = true},
        {.id = 2, .parent = 1, .depth = 1, .has_children = true, .expanded = true},
    }};

    std::array<std::uint64_t, 4> path{};
    require(veyra::tree_path_length(1, cycled) == 0, "a parent cycle has no path");
    require(veyra::copy_tree_path(1, cycled, path) == 0, "a parent cycle copies nothing");

    constexpr std::array<TreeCursorRow, 1> dangling{{
        {.id = 1, .parent = 99, .depth = 1, .has_children = false, .expanded = false},
    }};

    require(veyra::tree_path_length(1, dangling) == 0, "a missing ancestor has no path");
    require(veyra::copy_tree_path(1, dangling, path) == 0, "a missing ancestor copies nothing");
}

void check_subtree_selection_covers_descendants() {
    auto selection = veyra::SelectionController{};
    require(
        selection.configure({.mode = SelectionMode::multiple, .allow_empty = true}),
        "the selection switches to multiple"
    );

    require(
        veyra::select_tree_subtree(1, kLinked, selection) == SelectionResult::changed,
        "selecting a subtree reports a change"
    );

    for (std::uint64_t id : {1, 2, 3, 4, 5}) {
        require(selection.contains(id), "every descendant is selected");
    }

    require(!selection.contains(6) && !selection.contains(7), "other roots stay clear");
    require(
        veyra::select_tree_subtree(1, kLinked, selection) == SelectionResult::handled,
        "selecting the same subtree reports no change"
    );
}

void check_subtree_selection_honors_mode_and_unknowns() {
    auto selection = veyra::SelectionController{};
    require(
        selection.configure({.mode = SelectionMode::multiple, .allow_empty = true}),
        "the selection switches to multiple"
    );

    require(
        veyra::select_tree_subtree(99, kLinked, selection) == SelectionResult::ignored &&
            selection.empty(),
        "an unknown root leaves the selection alone"
    );
    require(
        veyra::select_tree_subtree(0, kLinked, selection) == SelectionResult::ignored,
        "the null id selects nothing"
    );

    auto single = veyra::SelectionController{};
    require(
        veyra::select_tree_subtree(1, kLinked, single) == SelectionResult::changed &&
            single.count() == 1 && single.contains(1),
        "single mode selects the root only"
    );
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
        check_expansion_set_basics();
        check_expansion_bulk_restore_is_atomic();
        check_lazy_children_gate_expansion();
        check_collapse_cancels_pending_loads();
        check_path_identity_runs_root_first();
        check_path_identity_rejects_broken_links();
        check_subtree_selection_covers_descendants();
        check_subtree_selection_honors_mode_and_unknowns();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
