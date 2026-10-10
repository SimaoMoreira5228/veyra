#include "veyra/behaviors/tabs.hpp"

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

std::span<const std::uint64_t> no_disabled() {
    return {};
}

void check_select_and_empty_members() {
    auto tabs = veyra::TabsController{};
    require(!tabs.has_selected(), "a new tab strip selects nothing");
    require(!tabs.has_cursor(), "a new tab strip has no cursor");

    std::array<std::uint64_t, 3> members{11, 12, 13};

    require(
        tabs.select(11, members, no_disabled()) == veyra::TabsResult::changed,
        "selecting a member reports a change"
    );
    require(
        tabs.has_selected() && tabs.selected() == 11 && tabs.has_cursor() && tabs.cursor() == 11,
        "a pointer select plants selection and cursor on the same tab"
    );
    require(
        tabs.select(11, members, no_disabled()) == veyra::TabsResult::handled,
        "reselecting the same tab reports no change"
    );
    require(
        tabs.select(99, members, no_disabled()) == veyra::TabsResult::ignored &&
            tabs.selected() == 11,
        "selecting outside the member list is ignored"
    );

    std::span<const std::uint64_t> no_members{};
    require(
        tabs.select(11, no_members, no_disabled()) == veyra::TabsResult::ignored,
        "selecting with zero members is ignored"
    );
    require(
        tabs.move_cursor(veyra::TabsMove::next, no_members, no_disabled()) ==
            veyra::TabsResult::ignored,
        "roving with zero members is ignored"
    );
    require(
        tabs.activate(no_members, no_disabled()) == veyra::TabsResult::ignored &&
            tabs.selected() == 11,
        "activating over an empty span is ignored and keeps the committed selection"
    );

    auto fresh = veyra::TabsController{};
    require(
        fresh.activate(members, no_disabled()) == veyra::TabsResult::ignored,
        "activating with no cursor is ignored"
    );
}

void check_automatic_roving_selects() {
    auto tabs = veyra::TabsController{};
    std::array<std::uint64_t, 3> members{21, 22, 23};
    tabs.select(21, members, no_disabled());

    require(
        tabs.move_cursor(veyra::TabsMove::next, members, no_disabled()) ==
                veyra::TabsResult::changed &&
            tabs.selected() == 22 && tabs.cursor() == 22,
        "automatic roving moves selection with the cursor"
    );
    require(
        tabs.move_cursor(veyra::TabsMove::prev, members, no_disabled()) ==
                veyra::TabsResult::changed &&
            tabs.selected() == 21,
        "roving back restores the previous selection"
    );
    require(
        tabs.move_cursor(veyra::TabsMove::last, members, no_disabled()) ==
                veyra::TabsResult::changed &&
            tabs.selected() == 23,
        "last jumps selection to the end"
    );
    require(
        tabs.move_cursor(veyra::TabsMove::first, members, no_disabled()) ==
                veyra::TabsResult::changed &&
            tabs.selected() == 21,
        "first jumps selection to the front"
    );
}

void check_manual_roving_diverges() {
    auto tabs = veyra::TabsController{};
    std::array<std::uint64_t, 3> members{31, 32, 33};
    tabs.select(31, members, no_disabled());

    require(tabs.configure(veyra::TabsActivation::manual), "switching to manual reports a change");
    require(
        !tabs.configure(veyra::TabsActivation::manual),
        "repeating the activation mode reports no change"
    );
    require(
        tabs.selected() == 31 && tabs.cursor() == 31,
        "a mode switch keeps the selection and re-syncs the cursor onto it"
    );

    require(
        tabs.move_cursor(veyra::TabsMove::next, members, no_disabled()) ==
                veyra::TabsResult::changed &&
            tabs.cursor() == 32 && tabs.selected() == 31,
        "manual roving moves the cursor and leaves the selection behind"
    );
    require(
        tabs.move_cursor(veyra::TabsMove::next, members, no_disabled()) ==
                veyra::TabsResult::changed &&
            tabs.cursor() == 33 && tabs.selected() == 31,
        "a second manual move keeps diverging from the selection"
    );
    require(
        tabs.activate(members, no_disabled()) == veyra::TabsResult::changed &&
            tabs.selected() == 33,
        "explicit activate commits the cursor to the selection"
    );
    require(
        tabs.activate(members, no_disabled()) == veyra::TabsResult::handled,
        "activating an already committed cursor reports no change"
    );

    require(tabs.configure(veyra::TabsActivation::automatic), "switching back to automatic works");
    require(
        tabs.selected() == 33 && tabs.cursor() == 33,
        "returning to automatic reunites cursor and selection"
    );
    require(
        tabs.move_cursor(veyra::TabsMove::prev, members, no_disabled()) ==
                veyra::TabsResult::changed &&
            tabs.selected() == 32 && tabs.cursor() == 32,
        "automatic roving selects again after the switch back"
    );
}

void check_manual_mode_switch_abandons_cursor() {
    auto tabs = veyra::TabsController{};
    std::array<std::uint64_t, 3> members{41, 42, 43};
    tabs.configure(veyra::TabsActivation::manual);
    tabs.select(41, members, no_disabled());
    tabs.move_cursor(veyra::TabsMove::next, members, no_disabled());

    require(
        tabs.cursor() == 42 && tabs.selected() == 41,
        "manual cursor diverges before the switch"
    );

    require(
        tabs.configure(veyra::TabsActivation::automatic),
        "switching to automatic reports a change"
    );
    require(
        tabs.selected() == 41 && tabs.cursor() == 41,
        "the pending cursor is abandoned and the committed selection wins"
    );
}

void check_wrap_policy() {
    auto tabs = veyra::TabsController{};
    std::array<std::uint64_t, 3> members{51, 52, 53};

    require(
        tabs.move_cursor(veyra::TabsMove::prev, members, no_disabled()) ==
                veyra::TabsResult::changed &&
            tabs.cursor() == 53,
        "prev without a cursor starts at the last tab"
    );

    tabs.select(53, members, no_disabled());

    require(
        tabs.move_cursor(veyra::TabsMove::next, members, no_disabled()) ==
                veyra::TabsResult::changed &&
            tabs.cursor() == 51,
        "next wraps from the last tab to the first"
    );
    require(
        tabs.move_cursor(veyra::TabsMove::prev, members, no_disabled()) ==
                veyra::TabsResult::changed &&
            tabs.cursor() == 53,
        "prev wraps from the first tab to the last"
    );

    auto solo = veyra::TabsController{};
    std::array<std::uint64_t, 1> one{7};
    require(
        solo.select(7, one, no_disabled()) == veyra::TabsResult::changed,
        "a single tab selects"
    );
    require(
        solo.move_cursor(veyra::TabsMove::next, one, no_disabled()) == veyra::TabsResult::handled &&
            solo.move_cursor(veyra::TabsMove::prev, one, no_disabled()) ==
                veyra::TabsResult::handled &&
            solo.selected() == 7,
        "roving on a single tab stays put"
    );
}

void check_disabled_skipping() {
    auto tabs = veyra::TabsController{};
    std::array<std::uint64_t, 4> members{61, 62, 63, 64};
    std::array<std::uint64_t, 1> blocked{62};
    tabs.select(61, members, no_disabled());

    require(
        tabs.select(62, members, blocked) == veyra::TabsResult::ignored && tabs.selected() == 61,
        "selecting a disabled tab is ignored"
    );
    require(
        tabs.move_cursor(veyra::TabsMove::next, members, blocked) == veyra::TabsResult::changed &&
            tabs.cursor() == 63,
        "roving skips disabled tabs"
    );
    require(
        tabs.move_cursor(veyra::TabsMove::prev, members, blocked) == veyra::TabsResult::changed &&
            tabs.cursor() == 61,
        "roving backward skips disabled tabs too"
    );
    require(
        tabs.move_cursor(veyra::TabsMove::last, members, blocked) == veyra::TabsResult::changed &&
            tabs.cursor() == 64,
        "last lands on the final enabled tab"
    );

    std::array<std::uint64_t, 4> all_blocked{61, 62, 63, 64};
    require(
        tabs.move_cursor(veyra::TabsMove::next, members, all_blocked) ==
                veyra::TabsResult::ignored &&
            tabs.cursor() == 64,
        "roving with every tab disabled is ignored"
    );
    require(
        tabs.move_cursor(veyra::TabsMove::first, members, all_blocked) ==
                veyra::TabsResult::ignored &&
            tabs.move_cursor(veyra::TabsMove::last, members, all_blocked) ==
                veyra::TabsResult::ignored,
        "jumps with every tab disabled are ignored"
    );
    require(
        tabs.activate(members, all_blocked) == veyra::TabsResult::ignored,
        "committing onto a disabled cursor is ignored"
    );
}

void check_close_fallback() {
    auto tabs = veyra::TabsController{};
    std::array<std::uint64_t, 3> members{71, 72, 73};
    tabs.select(72, members, no_disabled());

    veyra::TabsClose middle = tabs.close_request(72, members, no_disabled());
    require(
        middle.result == veyra::TabsResult::changed && middle.closing == 72 &&
            middle.has_fallback && middle.fallback == 73,
        "closing a middle tab falls forward to the next tab"
    );
    require(
        tabs.selected() == 73 && tabs.cursor() == 73,
        "selection and cursor retarget to the fallback at once"
    );

    tabs.select(73, members, no_disabled());
    veyra::TabsClose last = tabs.close_request(73, members, no_disabled());
    require(
        last.result == veyra::TabsResult::changed && last.has_fallback && last.fallback == 72,
        "closing the last tab falls back to the previous tab"
    );

    tabs.select(71, members, no_disabled());
    veyra::TabsClose idle = tabs.close_request(73, members, no_disabled());
    require(
        idle.result == veyra::TabsResult::handled && idle.has_fallback && idle.fallback == 72 &&
            tabs.selected() == 71,
        "closing an unselected tab leaves selection alone but still names the neighbor"
    );

    std::array<std::uint64_t, 1> lone{80};
    tabs.select(80, lone, no_disabled());
    veyra::TabsClose only = tabs.close_request(80, lone, no_disabled());
    require(
        only.result == veyra::TabsResult::changed && !only.has_fallback && !tabs.has_selected() &&
            !tabs.has_cursor(),
        "closing the only tab clears selection with no fallback"
    );

    veyra::TabsClose missing = tabs.close_request(99, members, no_disabled());
    require(
        missing.result == veyra::TabsResult::ignored && missing.closing == 99 &&
            !missing.has_fallback,
        "closing an id outside the members is ignored"
    );

    auto dupes = veyra::TabsController{};
    std::array<std::uint64_t, 3> doubled{71, 71, 72};
    dupes.select(71, doubled, no_disabled());
    veyra::TabsClose deduped = dupes.close_request(71, doubled, no_disabled());
    require(
        deduped.result == veyra::TabsResult::changed && deduped.has_fallback &&
            deduped.fallback == 72 && dupes.selected() == 72,
        "a duplicated member id never becomes the fallback"
    );

    auto twins = veyra::TabsController{};
    std::array<std::uint64_t, 2> paired{72, 71};
    std::array<std::uint64_t, 1> retired{72};
    twins.select(71, paired, no_disabled());
    veyra::TabsClose self_only = twins.close_request(71, paired, retired);
    require(
        self_only.result == veyra::TabsResult::changed && !self_only.has_fallback &&
            !twins.has_selected() && !twins.has_cursor(),
        "closing with only a disabled neighbor clears the selection"
    );
}

void check_close_skips_disabled_fallback() {
    auto tabs = veyra::TabsController{};
    std::array<std::uint64_t, 3> members{91, 92, 93};
    std::array<std::uint64_t, 1> blocked{93};
    tabs.select(92, members, no_disabled());

    veyra::TabsClose outcome = tabs.close_request(92, members, blocked);
    require(
        outcome.result == veyra::TabsResult::changed && outcome.has_fallback &&
            outcome.fallback == 91 && tabs.selected() == 91,
        "a disabled next tab is skipped and the fallback walks backward"
    );
}

void check_stale_cursor_resettles() {
    auto tabs = veyra::TabsController{};
    std::array<std::uint64_t, 3> before{101, 102, 103};
    std::array<std::uint64_t, 2> after{101, 103};
    tabs.select(102, before, no_disabled());

    require(
        tabs.move_cursor(veyra::TabsMove::next, after, no_disabled()) ==
                veyra::TabsResult::changed &&
            tabs.cursor() == 101,
        "a cursor id gone from the span re-anchors from the selection"
    );
}

void check_semantics() {
    auto tabs = veyra::TabsController{};
    std::array<std::uint64_t, 2> members{111, 112};
    tabs.select(111, members, no_disabled());

    auto tab = veyra::SemanticNodeData{
        .role = veyra::SemanticRole::text,
        .label = "unchanged",
    };
    require(!tabs.write_tab_semantics(111, {}, false, tab), "an empty tab name is rejected");
    require(tab.role == veyra::SemanticRole::text, "rejected tab output stays unchanged");
    require(tabs.write_tab_semantics(111, "First", false, tab), "a named tab writes semantics");
    require(
        tab.role == veyra::SemanticRole::tab && tab.label == "First" &&
            veyra::has_state(tab.states, veyra::SemanticState::selected) &&
            veyra::has_action(tab.actions, veyra::SemanticAction::select),
        "the selected tab exposes the selected state and a select action"
    );
    require(
        tabs.write_tab_semantics(112, "Second", false, tab) &&
            !veyra::has_state(tab.states, veyra::SemanticState::selected),
        "an unselected tab writes no selected state"
    );
    require(
        tabs.write_tab_semantics(112, "Second", true, tab) &&
            veyra::has_state(tab.states, veyra::SemanticState::disabled) &&
            !veyra::has_action(tab.actions, veyra::SemanticAction::select),
        "a disabled tab exposes the disabled state and no actions"
    );

    auto list = veyra::SemanticNodeData{};
    require(!tabs.write_list_semantics({}, list), "an empty tab list name is rejected");
    require(tabs.write_list_semantics("Panels", list), "a named tab list writes semantics");
    require(
        list.role == veyra::SemanticRole::tab_list && list.label == "Panels",
        "the container exposes the tab list role"
    );
}

}

int main() {
    try {
        check_select_and_empty_members();
        check_automatic_roving_selects();
        check_manual_roving_diverges();
        check_manual_mode_switch_abandons_cursor();
        check_wrap_policy();
        check_disabled_skipping();
        check_close_fallback();
        check_close_skips_disabled_fallback();
        check_stale_cursor_resettles();
        check_semantics();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
