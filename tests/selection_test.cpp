#include "veyra/behaviors/selection.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

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
        control.has_anchor() && control.anchor() == 1 && control.has_active() &&
            control.active() == 1,
        "a plain select moves the anchor and active cursor to the id"
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
        control.active() == 50 && control.anchor() == 30,
        "range extension moves active and keeps the anchor"
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

    std::array<std::uint64_t, veyra::SelectionController::kCapacity + 1> too_many{};
    for (std::size_t i = 0; i < too_many.size(); ++i) {
        too_many[i] = static_cast<std::uint64_t>(i + 1);
    }
    const std::size_t before = control.count();
    require(
        control.select_all(too_many) == veyra::SelectionResult::overflow &&
            control.count() == before,
        "an over-capacity select-all is rejected without touching the set"
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
    require(control.set_disabled(true), "disabling reports a state change");
    require(
        control.select(2) == veyra::SelectionResult::ignored &&
            control.deselect(1) == veyra::SelectionResult::ignored &&
            control.toggle_item(2) == veyra::SelectionResult::ignored &&
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

    auto node = veyra::SemanticNodeData{};
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
}

}

int main() {
    try {
        check_single_select_and_empty_policy();
        check_multi_select_and_toggle();
        check_range_extension();
        check_select_all_and_overflow();
        check_disabled_rejects_mutation();
        check_queries_and_item_semantics();
        check_radio_mutual_exclusion();
        check_radio_movement_and_semantics();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
