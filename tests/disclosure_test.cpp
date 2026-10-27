#include "veyra/behaviors/navigation/disclosure.hpp"

#include "veyra/behaviors/selection/selection.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

void check_expand_collapse_exactly_once() {
    auto control = veyra::DisclosureController{};
    require(control.empty(), "a new controller expands nothing");
    require(control.count() == 0, "a new controller holds zero ids");

    require(
        control.expand(7) == veyra::DisclosureResult::changed,
        "expanding an id reports a change"
    );
    require(control.count() == 1 && control.contains(7), "the expanded id is stored");
    require(!control.empty(), "a held id means the controller is not empty");
    require(
        control.expand(7) == veyra::DisclosureResult::handled && control.count() == 1,
        "expanding a held id suppresses the duplicate"
    );

    require(
        control.collapse(4) == veyra::DisclosureResult::handled && control.contains(7),
        "collapsing an absent id reports no change and keeps the set"
    );
    require(
        control.collapse(7) == veyra::DisclosureResult::changed && control.empty(),
        "collapsing the held id drops it"
    );
    require(
        control.collapse(7) == veyra::DisclosureResult::handled,
        "collapsing an already-collapsed id is handled, not changed"
    );

    require(
        control.clear() == veyra::DisclosureResult::handled,
        "clearing an empty controller reports no change"
    );
    control.expand(1);
    control.expand(2);
    require(
        control.clear() == veyra::DisclosureResult::changed && control.empty(),
        "clear collapses every expanded id at once"
    );
}

void check_toggle() {
    auto control = veyra::DisclosureController{};

    require(
        control.toggle_item(3) == veyra::DisclosureResult::changed && control.contains(3),
        "toggling a collapsed id expands it"
    );
    require(
        control.toggle_item(3) == veyra::DisclosureResult::changed && !control.contains(3),
        "toggling an expanded id collapses it"
    );
    require(control.empty(), "two toggles leave the set the way it started");
}

void check_subtree_collapse_purges() {
    auto control = veyra::DisclosureController{};
    control.expand(1);
    control.expand(2);
    control.expand(3);
    control.expand(9);

    std::array<std::uint64_t, 2> descendants{2, 3};
    require(
        control.collapse_subtree(1, descendants) == veyra::DisclosureResult::changed,
        "collapsing a parent with expanded descendants reports a change"
    );
    require(
        !control.contains(1) && !control.contains(2) && !control.contains(3),
        "the parent and its expanded descendants leave the set"
    );
    require(control.contains(9) && control.count() == 1, "ids outside the subtree stay expanded");

    require(
        control.collapse_subtree(1, descendants) == veyra::DisclosureResult::handled,
        "repeating a subtree collapse with nothing left reports no change"
    );

    control.expand(5);
    std::array<std::uint64_t, 2> missing{6, 7};
    require(
        control.collapse_subtree(5, missing) == veyra::DisclosureResult::changed &&
            !control.contains(5),
        "a subtree collapse still drops the parent when no descendant is expanded"
    );
    require(
        control.collapse_subtree(5, missing) == veyra::DisclosureResult::handled,
        "descendants that were never expanded do not fake a change"
    );

    control.expand(10);
    control.expand(11);
    std::array<std::uint64_t, 3> duplicates{11, 11, 12};
    require(
        control.collapse_subtree(10, duplicates) == veyra::DisclosureResult::changed &&
            !control.contains(10) && !control.contains(11),
        "a duplicated descendant span collapses each id once"
    );
}

void check_expand_all_and_overflow() {
    auto control = veyra::DisclosureController{};

    std::array<std::uint64_t, 4> ids{5, 6, 5, 7};
    require(
        control.expand_all(ids) == veyra::DisclosureResult::changed && control.count() == 3,
        "expand-all takes the span with first-occurrence dedup"
    );
    require(
        control.expand_all(ids) == veyra::DisclosureResult::handled,
        "repeating the same expand-all reports no change"
    );

    std::array<std::uint64_t, 4> wider{5, 6, 7, 8};
    require(
        control.expand_all(wider) == veyra::DisclosureResult::changed && control.contains(8),
        "expand-all adds to the set and never drops ids outside the span"
    );

    std::array<std::uint64_t, 0> none{};
    require(
        control.expand_all(none) == veyra::DisclosureResult::handled,
        "an empty expand-all reports no change"
    );

    std::array<std::uint64_t, veyra::DisclosureController::kCapacity + 2> too_many{};
    for (std::size_t i = 0; i < too_many.size(); ++i) {
        too_many[i] = static_cast<std::uint64_t>(i + 1);
    }
    const std::size_t before = control.count();
    require(
        control.expand_all(too_many) == veyra::DisclosureResult::overflow &&
            control.count() == before,
        "an over-capacity expand-all is rejected without touching the set"
    );

    auto full = veyra::DisclosureController{};
    std::array<std::uint64_t, veyra::DisclosureController::kCapacity> exact{};
    for (std::size_t i = 0; i < exact.size(); ++i) {
        exact[i] = static_cast<std::uint64_t>(i + 1);
    }
    require(
        full.expand_all(exact) == veyra::DisclosureResult::changed &&
            full.count() == veyra::DisclosureController::kCapacity,
        "a span exactly at capacity is accepted"
    );
    require(
        full.expand(9999) == veyra::DisclosureResult::overflow && !full.contains(9999),
        "one more id past capacity overflows without mutation"
    );

    std::array<std::uint64_t, 2> spill{9998, 9999};
    require(
        full.expand_all(spill) == veyra::DisclosureResult::overflow &&
            full.count() == veyra::DisclosureController::kCapacity,
        "expand-all past a full set overflows without dropping held ids"
    );

    auto partial = veyra::DisclosureController{};
    std::array<std::uint64_t, veyra::DisclosureController::kCapacity - 1> almost{};
    for (std::size_t i = 0; i < almost.size(); ++i) {
        almost[i] = static_cast<std::uint64_t>(i + 1);
    }
    require(
        partial.expand_all(almost) == veyra::DisclosureResult::changed,
        "filling to one below capacity works"
    );
    std::array<std::uint64_t, 2> pair{
        static_cast<std::uint64_t>(veyra::DisclosureController::kCapacity),
        static_cast<std::uint64_t>(veyra::DisclosureController::kCapacity + 1),
    };
    const std::size_t held = partial.count();
    require(
        partial.expand_all(pair) == veyra::DisclosureResult::overflow && partial.count() == held,
        "an expand-all that only partly fits is rejected whole, not trimmed"
    );
}

void check_disabled_rejects_mutation() {
    auto control = veyra::DisclosureController{};
    control.expand(1);

    std::array<std::uint64_t, 2> span{2, 3};
    require(control.set_disabled(true), "disabling reports a state change");
    require(!control.set_disabled(true), "repeating the same disabled flag reports no change");
    require(
        control.expand(2) == veyra::DisclosureResult::ignored &&
            control.collapse(1) == veyra::DisclosureResult::ignored &&
            control.toggle_item(1) == veyra::DisclosureResult::ignored &&
            control.collapse_subtree(1, span) == veyra::DisclosureResult::ignored &&
            control.expand_all(span) == veyra::DisclosureResult::ignored &&
            control.clear() == veyra::DisclosureResult::ignored,
        "every mutation is ignored while disabled"
    );
    require(
        control.count() == 1 && control.contains(1),
        "rejected mutations leave the expanded set untouched"
    );
    require(control.state().disabled && control.disabled(), "disabled state is observable");
    require(control.contains(1) && !control.empty(), "reads still work while disabled");

    require(control.set_disabled(false), "enabling reports a state change");
    require(
        control.expand(2) == veyra::DisclosureResult::changed,
        "mutation works again after enabling"
    );
}

void check_queries() {
    auto control = veyra::DisclosureController{};
    control.expand(11);
    control.expand(12);

    require(control.state().count == 2, "state exposes the expanded count");

    std::array<std::uint64_t, 4> buffer{};
    require(
        control.copy_expanded(buffer) == 2 && buffer[0] == 11 && buffer[1] == 12,
        "copy_expanded fills a caller buffer in expansion order"
    );

    std::array<std::uint64_t, 1> small{};
    require(
        control.copy_expanded(small) == 1 && small[0] == 11,
        "a short buffer truncates to its own size"
    );
}

void check_disclosure_semantics() {
    auto control = veyra::DisclosureController{};
    control.expand(11);

    auto node = veyra::SemanticNodeData{
        .role = veyra::SemanticRole::text,
        .label = "unchanged",
    };
    require(
        !control.write_disclosure_semantics(11, {}, node),
        "an empty accessible name is rejected"
    );
    require(node.role == veyra::SemanticRole::text, "invalid semantic output stays unchanged");
    require(control.write_disclosure_semantics(11, "Folder", node), "a named row writes semantics");
    require(
        node.role == veyra::SemanticRole::list_item && node.label == "Folder" &&
            veyra::has_state(node.states, veyra::SemanticState::expanded) &&
            veyra::has_action(node.actions, veyra::SemanticAction::collapse) &&
            !veyra::has_action(node.actions, veyra::SemanticAction::expand),
        "an expanded row exposes the expanded state with a collapse action"
    );
    require(
        control.write_disclosure_semantics(99, "Other", node) &&
            !veyra::has_state(node.states, veyra::SemanticState::expanded) &&
            veyra::has_action(node.actions, veyra::SemanticAction::expand) &&
            !veyra::has_action(node.actions, veyra::SemanticAction::collapse),
        "a collapsed row offers expand instead of collapse"
    );

    control.set_disabled(true);
    require(
        control.write_disclosure_semantics(11, "Folder", node) &&
            veyra::has_state(node.states, veyra::SemanticState::disabled) &&
            veyra::has_state(node.states, veyra::SemanticState::expanded) &&
            !veyra::has_action(node.actions, veyra::SemanticAction::collapse) &&
            !veyra::has_action(node.actions, veyra::SemanticAction::expand),
        "a disabled row keeps its expanded state but offers no actions"
    );
}

void check_independent_from_selection() {
    auto expanded = veyra::DisclosureController{};
    auto selected = veyra::SelectionController{};
    selected.configure({.mode = veyra::SelectionMode::multiple});

    expanded.expand(1);
    expanded.expand(2);
    selected.select(1);
    selected.select(2);

    require(
        expanded.collapse(1) == veyra::DisclosureResult::changed && selected.contains(1),
        "collapsing a row leaves its selection alone"
    );
    require(
        selected.deselect(2) == veyra::SelectionResult::changed && expanded.contains(2),
        "deselecting a row leaves its expansion alone"
    );
    require(expanded.count() == 1 && selected.count() == 1, "each controller owns its own id set");
}

}

int main() {
    try {
        check_expand_collapse_exactly_once();
        check_toggle();
        check_subtree_collapse_purges();
        check_expand_all_and_overflow();
        check_disabled_rejects_mutation();
        check_queries();
        check_disclosure_semantics();
        check_independent_from_selection();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
