#include "veyra/behaviors/selection/selection.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

static_assert(
    veyra::selection_tri_state(1, 3) == veyra::SelectionTriState::partial,
    "tri-state is a compile-time pure function"
);

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

void check_single_select_and_empty_policy() {
    auto control = veyra::SelectionController{};
    require(control.empty(), "a new controller selects nothing");
    require(control.allows_empty(), "single-select permits an empty selection by default");

    require(
        control.select(7) == veyra::SelectionResult::changed,
        "selecting an id reports a change"
    );
    require(control.count() == 1 && control.contains(7), "the selected id is stored");
    require(
        control.select(7) == veyra::SelectionResult::handled,
        "reselecting the same id reports no change"
    );
    require(control.count() == 1, "duplicate selection does not grow the set");

    require(
        control.select(9) == veyra::SelectionResult::changed && control.contains(9) &&
            !control.contains(7),
        "selecting another id replaces the single selection"
    );

    require(
        control.deselect(4) == veyra::SelectionResult::handled,
        "deselecting an absent id reports no change"
    );
    require(
        control.deselect(9) == veyra::SelectionResult::changed && control.empty(),
        "deselecting the id clears an allow-empty single selection"
    );
    require(
        control.clear() == veyra::SelectionResult::handled,
        "clearing an empty selection reports no change"
    );

    require(
        control.configure({.mode = veyra::SelectionMode::single, .allow_empty = false}),
        "a single-select group can require a value"
    );
    require(
        control.select(3) == veyra::SelectionResult::changed,
        "selection works with empty forbidden"
    );
    require(
        control.deselect(3) == veyra::SelectionResult::ignored && control.contains(3),
        "the last value cannot be deselected when empty is forbidden"
    );
    require(
        control.clear() == veyra::SelectionResult::ignored && control.contains(3),
        "clear is rejected when empty is forbidden"
    );
    require(
        control.toggle_item(3) == veyra::SelectionResult::ignored && control.contains(3),
        "toggling off the only value is rejected when empty is forbidden"
    );
}

void check_multi_select_and_toggle() {
    auto control = veyra::SelectionController{};
    require(
        control.configure({.mode = veyra::SelectionMode::multiple}),
        "a controller can switch to multi-select"
    );
    require(
        !control.configure({.mode = veyra::SelectionMode::multiple}),
        "repeating the same configuration reports no change"
    );

    require(control.select(1) == veyra::SelectionResult::changed, "first select changes");
    require(control.select(2) == veyra::SelectionResult::changed, "second select changes");
    require(
        control.select(1) == veyra::SelectionResult::handled && control.count() == 2,
        "reselecting a held id suppresses the duplicate"
    );
    require(
        control.has_anchor() && control.anchor() == 1 && !control.has_current(),
        "a plain select plants the range anchor and leaves the keyboard cursor alone"
    );

    require(
        control.toggle_item(3) == veyra::SelectionResult::changed && control.contains(3),
        "toggling an absent id selects it"
    );
    require(
        control.toggle_item(3) == veyra::SelectionResult::changed && !control.contains(3),
        "toggling a held id drops it"
    );

    require(
        control.deselect(1) == veyra::SelectionResult::changed && !control.contains(1) &&
            control.count() == 1,
        "deselect removes one id and keeps the rest"
    );
    require(
        control.clear() == veyra::SelectionResult::changed && control.empty(),
        "clear drops every id"
    );
}

void check_range_extension() {
    auto control = veyra::SelectionController{};
    control.configure({.mode = veyra::SelectionMode::multiple});

    std::array<std::uint64_t, 5> order{10, 20, 30, 40, 50};
    require(
        control.extend_range(order, 60) == veyra::SelectionResult::ignored && control.empty(),
        "extending to an id outside the span is ignored"
    );
    require(
        control.extend_range({}, 10) == veyra::SelectionResult::ignored,
        "extending over an empty span is ignored"
    );

    require(
        control.extend_range(order, 30) == veyra::SelectionResult::changed &&
            control.count() == 1 && control.contains(30),
        "extending without an anchor selects the target id"
    );

    require(
        control.extend_range(order, 50) == veyra::SelectionResult::changed &&
            control.count() == 3 && control.contains(30) && control.contains(40) &&
            control.contains(50),
        "extending forward replaces the set with the anchor-to-target slice"
    );
    require(
        control.current() == 50 && control.anchor() == 30,
        "range extension moves current and keeps the anchor"
    );

    require(
        control.extend_range(order, 50) == veyra::SelectionResult::handled,
        "extending to the same range reports no change"
    );

    require(
        control.extend_range(order, 10) == veyra::SelectionResult::changed &&
            control.count() == 3 && control.contains(10) && control.contains(20) &&
            control.contains(30) && !control.contains(50),
        "extending backward replaces the set with the new slice"
    );

    std::array<std::uint64_t, 3> shuffled{30, 10, 20};
    auto unordered = veyra::SelectionController{};
    unordered.configure({.mode = veyra::SelectionMode::multiple});
    unordered.select(30);
    require(
        unordered.extend_range(shuffled, 20) == veyra::SelectionResult::changed &&
            unordered.count() == 3,
        "span order defines the range, so shuffled spans select their own slice"
    );

    auto single = veyra::SelectionController{};
    single.select(10);
    require(
        single.extend_range(order, 40) == veyra::SelectionResult::changed && single.count() == 1 &&
            single.contains(40),
        "single-select range extension collapses to the target id"
    );

    auto repeated = veyra::SelectionController{};
    repeated.configure({.mode = veyra::SelectionMode::multiple});
    std::array<std::uint64_t, 4> doubled{10, 20, 20, 30};
    repeated.select(10);
    require(
        repeated.extend_range(doubled, 30) == veyra::SelectionResult::changed &&
            repeated.count() == 3 && repeated.contains(10) && repeated.contains(20) &&
            repeated.contains(30),
        "a span repeating an id selects it once"
    );

    auto crowded = veyra::SelectionController{};
    crowded.configure({.mode = veyra::SelectionMode::multiple});
    std::array<std::uint64_t, veyra::SelectionController::kCapacity + 2> flood{};
    for (std::size_t i = 0; i < flood.size(); ++i) {
        flood[i] = static_cast<std::uint64_t>(i + 1);
    }
    crowded.select(1);
    const std::size_t held = crowded.count();
    require(
        crowded.extend_range(flood, flood.back()) == veyra::SelectionResult::overflow &&
            crowded.count() == held && crowded.contains(1),
        "a range wider than capacity overflows without touching the set"
    );
}

void check_select_all_and_overflow() {
    auto control = veyra::SelectionController{};
    control.configure({.mode = veyra::SelectionMode::multiple});

    std::array<std::uint64_t, 4> ids{5, 6, 5, 7};
    require(
        control.select_all(ids) == veyra::SelectionResult::changed && control.count() == 3,
        "select-all takes the span with first-occurrence dedup"
    );
    require(
        control.select_all(ids) == veyra::SelectionResult::handled,
        "repeating the same select-all reports no change"
    );

    auto single = veyra::SelectionController{};
    require(
        single.select_all(ids) == veyra::SelectionResult::ignored && single.empty(),
        "select-all has no meaning in single-select mode"
    );

    std::array<std::uint64_t, veyra::SelectionController::kCapacity + 2> too_many{};
    for (std::size_t i = 0; i < too_many.size(); ++i) {
        too_many[i] = static_cast<std::uint64_t>(i + 1);
    }
    const std::size_t before = control.count();
    require(
        control.select_all(too_many) == veyra::SelectionResult::overflow &&
            control.count() == before,
        "an over-capacity select-all is rejected without touching the set"
    );
    require(
        control.select_all({}) == veyra::SelectionResult::handled && control.count() == before,
        "an empty select-all reports no change and keeps the set"
    );

    auto full = veyra::SelectionController{};
    full.configure({.mode = veyra::SelectionMode::multiple});
    std::array<std::uint64_t, veyra::SelectionController::kCapacity> exact{};
    for (std::size_t i = 0; i < exact.size(); ++i) {
        exact[i] = static_cast<std::uint64_t>(i + 1);
    }
    require(
        full.select_all(exact) == veyra::SelectionResult::changed &&
            full.count() == veyra::SelectionController::kCapacity,
        "a span exactly at capacity is accepted"
    );
    require(
        full.select(9999) == veyra::SelectionResult::overflow && !full.contains(9999),
        "one more id past capacity overflows without mutation"
    );
}

void check_disabled_rejects_mutation() {
    auto control = veyra::SelectionController{};
    control.configure({.mode = veyra::SelectionMode::multiple});
    control.select(1);

    std::array<std::uint64_t, 2> order{1, 2};
    control.set_current(2);
    require(control.set_disabled(true), "disabling reports a state change");
    require(
        control.select(2) == veyra::SelectionResult::ignored &&
            control.deselect(1) == veyra::SelectionResult::ignored &&
            control.toggle_item(2) == veyra::SelectionResult::ignored &&
            control.select_current() == veyra::SelectionResult::ignored &&
            control.clear() == veyra::SelectionResult::ignored &&
            control.extend_range(order, 2) == veyra::SelectionResult::ignored &&
            control.select_all(order) == veyra::SelectionResult::ignored,
        "every mutation is ignored while disabled"
    );
    require(
        control.count() == 1 && control.contains(1),
        "rejected mutations leave the selection untouched"
    );
    require(control.state().disabled, "disabled state is observable");

    auto disabled_node = veyra::SemanticNodeData{};
    require(control.write_item_semantics(1, "Row", disabled_node), "disabled rows still write");
    require(
        veyra::has_state(disabled_node.states, veyra::SemanticState::disabled) &&
            disabled_node.actions == 0,
        "a disabled row keeps its record but offers no actions"
    );

    require(control.set_disabled(false), "enabling reports a state change");
    require(
        control.select(2) == veyra::SelectionResult::changed,
        "mutation works again after enabling"
    );
}

void check_queries_and_item_semantics() {
    auto control = veyra::SelectionController{};
    control.configure({.mode = veyra::SelectionMode::multiple});
    control.select(11);
    control.select(12);

    std::array<std::uint64_t, 4> buffer{};
    require(
        control.copy_selected(buffer) == 2 && buffer[0] == 11 && buffer[1] == 12,
        "copy_selected fills a caller buffer in insertion order"
    );

    std::array<std::uint64_t, 1> small{};
    require(
        control.copy_selected(small) == 1 && small[0] == 11,
        "a short buffer truncates to its own size"
    );

    auto node = veyra::SemanticNodeData{
        .role = veyra::SemanticRole::text,
        .label = "unchanged",
    };
    require(!control.write_item_semantics(11, {}, node), "an empty accessible name is rejected");
    require(node.role == veyra::SemanticRole::text, "invalid semantic output stays unchanged");
    require(control.write_item_semantics(11, "Row", node), "a named item writes semantics");
    require(
        node.role == veyra::SemanticRole::list_item && node.label == "Row" &&
            veyra::has_state(node.states, veyra::SemanticState::selected) &&
            veyra::has_action(node.actions, veyra::SemanticAction::select),
        "a selected item exposes the selected state and a select action"
    );
    require(
        control.write_item_semantics(99, "Other", node) &&
            !veyra::has_state(node.states, veyra::SemanticState::selected),
        "an unselected id writes no selected state"
    );
}

void check_current_cursor_independence() {
    auto control = veyra::SelectionController{};
    control.configure({.mode = veyra::SelectionMode::multiple});
    require(!control.has_current(), "a new controller parks the cursor nowhere");
    require(
        control.select_current() == veyra::SelectionResult::ignored,
        "select-current with no cursor is ignored"
    );

    require(control.set_current(11), "parking the cursor reports a move");
    require(
        control.has_current() && control.current() == 11 && control.empty(),
        "parking the cursor selects nothing"
    );
    require(!control.set_current(11), "repeating the cursor reports no move");

    require(
        control.select(12) == veyra::SelectionResult::changed && control.current() == 11,
        "a plain select leaves the keyboard cursor where it was"
    );
    require(control.anchor() == 12, "the select still plants the range anchor");

    require(
        control.select_current() == veyra::SelectionResult::changed && control.contains(11),
        "select-current toggles the cursor id on in multi-select"
    );
    require(
        control.select_current() == veyra::SelectionResult::changed && !control.contains(11),
        "select-current toggles the cursor id back off"
    );

    control.select(30);
    control.deselect(12);
    require(
        control.has_current() && control.current() == 11,
        "deselect keeps the cursor off the set"
    );
    control.clear();
    require(
        control.has_current() && control.current() == 11 && control.empty(),
        "clear drops the set and keeps the cursor"
    );

    auto single = veyra::SelectionController{};
    single.set_current(21);
    single.set_current(22);
    require(
        single.select_current() == veyra::SelectionResult::changed && single.contains(22) &&
            !single.contains(21),
        "select-current replaces the set in single-select"
    );

    require(control.clear_current(), "clearing the cursor reports a move");
    require(!control.has_current(), "the cursor is gone after a clear");
    require(
        control.select_current() == veyra::SelectionResult::ignored,
        "select-current with a cleared cursor is ignored"
    );
    require(!control.clear_current(), "repeating the clear reports no move");
}

void check_exclude_all_mode() {
    auto control = veyra::SelectionController{};
    control.configure({.mode = veyra::SelectionMode::multiple});
    control.select(1);
    control.select(2);

    require(control.set_exclude_all(true), "entering exclude-all reports a change");
    require(!control.set_exclude_all(true), "repeating exclude-all reports no change");
    require(control.exclude_all(), "exclude-all mode is observable");

    require(!control.empty(), "exclude-all is never an empty selection");
    require(control.contains(999), "an unlisted id reads selected under exclude-all");

    require(
        control.deselect(999) == veyra::SelectionResult::changed && !control.contains(999),
        "deselecting under exclude-all files an exception"
    );
    require(
        control.deselect(999) == veyra::SelectionResult::handled,
        "repeating the exception reports no change"
    );
    require(
        control.select(999) == veyra::SelectionResult::changed && control.contains(999),
        "selecting under exclude-all drops the exception"
    );
    require(
        control.select(1000) == veyra::SelectionResult::handled,
        "selecting an unlisted id under exclude-all reports no change"
    );
    require(
        control.toggle_item(1001) == veyra::SelectionResult::changed && !control.contains(1001),
        "toggling under exclude-all files an exception"
    );

    std::array<std::uint64_t, 2> buffer{};
    require(
        control.copy_selected(buffer) == 0 && !control.empty(),
        "exclude-all copy_selected refuses to materialize the virtual set"
    );

    std::array<std::uint64_t, 3> order{1, 2, 3};
    require(
        control.extend_range(order, 3) == veyra::SelectionResult::overflow,
        "exclude-all range extension overflows instead of guessing the universe"
    );

    require(
        control.clear() == veyra::SelectionResult::changed && control.exclude_all() &&
            control.contains(7),
        "clear under exclude-all drops the exceptions and keeps the mode"
    );
    require(
        control.clear() == veyra::SelectionResult::handled,
        "clearing an exception-free exclude-all reports no change"
    );

    std::array<std::uint64_t, 2> few{41, 42};
    require(
        control.select_all(few) == veyra::SelectionResult::changed && !control.exclude_all() &&
            control.count() == 2 && control.contains(41),
        "select-all materializes a bounded span and leaves exclude-all behind"
    );

    require(control.set_exclude_all(true), "exclude-all can be re-entered");
    require(control.set_exclude_all(false), "leaving exclude-all reports a change");
    require(
        control.empty() && !control.contains(41),
        "leaving exclude-all lands on an empty inclusive set"
    );

    auto full = veyra::SelectionController{};
    full.configure({.mode = veyra::SelectionMode::multiple});
    full.set_exclude_all(true);
    for (std::uint64_t id = 1; id <= veyra::SelectionController::kCapacity; ++id) {
        require(
            full.deselect(id) == veyra::SelectionResult::changed,
            "exceptions fill up to capacity"
        );
    }
    require(
        full.deselect(veyra::SelectionController::kCapacity + 1) ==
                veyra::SelectionResult::overflow &&
            full.contains(veyra::SelectionController::kCapacity + 1),
        "one exception past capacity overflows and stays selected"
    );
}

void check_selection_tri_state() {
    require(
        veyra::selection_tri_state(0, 0) == veyra::SelectionTriState::none,
        "nothing of nothing is none"
    );
    require(
        veyra::selection_tri_state(0, 9) == veyra::SelectionTriState::none,
        "an empty count is none"
    );
    require(
        veyra::selection_tri_state(4, 9) == veyra::SelectionTriState::partial,
        "a strict subset is partial"
    );
    require(
        veyra::selection_tri_state(9, 9) == veyra::SelectionTriState::all,
        "a full count is all"
    );
    require(
        veyra::selection_tri_state(1, 1) == veyra::SelectionTriState::all,
        "a lone held item is all"
    );
}

void check_radio_mutual_exclusion() {
    auto group = veyra::RadioGroup{};
    require(!group.allows_empty(), "radio groups require a value by default");
    require(!group.has_selection(), "a new group selects nothing yet");

    std::array<std::uint64_t, 3> members{101, 102, 103};
    require(
        group.select(999, members) == veyra::SelectionResult::ignored && !group.has_selection(),
        "selecting outside the member list is ignored"
    );

    require(
        group.select(101, members) == veyra::SelectionResult::changed && group.has_selection() &&
            group.selected() == 101,
        "selecting a member checks it"
    );
    require(
        group.select(101, members) == veyra::SelectionResult::handled,
        "reselecting the checked radio reports no change"
    );

    require(
        group.select(102, members) == veyra::SelectionResult::changed && group.selected() == 102 &&
            !group.contains(101),
        "activating a second radio deselects the first in the same change"
    );

    require(
        group.clear() == veyra::SelectionResult::ignored && group.selected() == 102,
        "clear is rejected while the group requires a value"
    );
    require(group.configure(true), "a group can opt into an empty selection");
    require(
        group.select(103, members) == veyra::SelectionResult::changed,
        "selection works after reconfiguring the group"
    );
    require(
        group.clear() == veyra::SelectionResult::changed && !group.has_selection(),
        "clear works once empty is allowed"
    );

    auto empty_group = veyra::RadioGroup{};
    std::span<const std::uint64_t> no_members{};
    require(
        empty_group.select(101, no_members) == veyra::SelectionResult::ignored &&
            empty_group.move(veyra::RadioMove::next, no_members) == veyra::SelectionResult::ignored,
        "a group with zero members ignores selection and movement"
    );

    auto fresh_next = veyra::RadioGroup{};
    require(
        fresh_next.move(veyra::RadioMove::next, members) == veyra::SelectionResult::changed &&
            fresh_next.selected() == 101,
        "movement without a check starts at the first member"
    );
    auto fresh_prev = veyra::RadioGroup{};
    require(
        fresh_prev.move(veyra::RadioMove::prev, members) == veyra::SelectionResult::changed &&
            fresh_prev.selected() == 103,
        "backward movement without a check starts at the last member"
    );

    auto solo = veyra::RadioGroup{};
    std::array<std::uint64_t, 1> one{7};
    require(
        solo.select(7, one) == veyra::SelectionResult::changed &&
            solo.move(veyra::RadioMove::next, one) == veyra::SelectionResult::handled &&
            solo.selected() == 7,
        "a one-member group stays on its only radio"
    );
}

void check_radio_movement_and_semantics() {
    auto group = veyra::RadioGroup{};
    std::array<std::uint64_t, 3> members{21, 22, 23};
    group.select(21, members);

    require(
        group.move(veyra::RadioMove::next, members) == veyra::SelectionResult::changed &&
            group.selected() == 22,
        "next moves the check forward"
    );
    require(
        group.move(veyra::RadioMove::next, members) == veyra::SelectionResult::changed &&
            group.move(veyra::RadioMove::next, members) == veyra::SelectionResult::changed &&
            group.selected() == 21,
        "next wraps from the last member to the first"
    );
    require(
        group.move(veyra::RadioMove::prev, members) == veyra::SelectionResult::changed &&
            group.selected() == 23,
        "prev wraps from the first member to the last"
    );
    require(
        group.move(veyra::RadioMove::last, members) == veyra::SelectionResult::handled &&
            group.move(veyra::RadioMove::first, members) == veyra::SelectionResult::changed &&
            group.selected() == 21,
        "first and last jump to the ends of the member order"
    );
    require(
        group.move(static_cast<veyra::RadioMove>(255), members) ==
                veyra::SelectionResult::ignored &&
            group.selected() == 21,
        "an invalid movement is ignored without changing the selected radio"
    );

    auto node = veyra::SemanticNodeData{};
    require(!group.write_semantics(21, {}, node), "an empty radio name is rejected");
    require(group.write_semantics(21, "Size", node), "a named radio writes semantics");
    require(
        node.role == veyra::SemanticRole::radio && node.label == "Size" &&
            veyra::has_state(node.states, veyra::SemanticState::checked) &&
            veyra::has_action(node.actions, veyra::SemanticAction::activate),
        "a checked radio exposes the checked state and activation"
    );
    require(
        group.write_semantics(22, "Other", node) &&
            !veyra::has_state(node.states, veyra::SemanticState::checked),
        "an unchecked radio writes no checked state"
    );

    require(group.set_disabled(true), "disabling a group reports a change");
    require(
        group.select(22, members) == veyra::SelectionResult::ignored &&
            group.move(veyra::RadioMove::next, members) == veyra::SelectionResult::ignored &&
            group.selected() == 21,
        "a disabled group rejects selection and movement"
    );

    auto disabled_radio = veyra::SemanticNodeData{};
    require(group.write_semantics(21, "Size", disabled_radio), "disabled radios still write");
    require(
        veyra::has_state(disabled_radio.states, veyra::SemanticState::disabled) &&
            disabled_radio.actions == 0,
        "a disabled radio keeps its record but offers no actions"
    );
}

}

int main() {
    try {
        check_single_select_and_empty_policy();
        check_multi_select_and_toggle();
        check_range_extension();
        check_select_all_and_overflow();
        check_disabled_rejects_mutation();
        check_current_cursor_independence();
        check_exclude_all_mode();
        check_selection_tri_state();
        check_queries_and_item_semantics();
        check_radio_mutual_exclusion();
        check_radio_movement_and_semantics();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
