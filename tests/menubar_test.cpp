#include "veyra/behaviors/menubar.hpp"

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

using Titles = std::array<veyra::MenubarTitle, 4>;

Titles bar_titles() {
    return {{
        {.id = 1},
        {.id = 2},
        {.id = 3},
        {.id = 4},
    }};
}

void check_enter_and_exit() {
    auto bar = veyra::MenubarController{};
    const auto titles = bar_titles();
    std::span<const veyra::MenubarTitle> none{};

    require(!bar.in_strip(), "a new strip is out of the menubar");
    require(!bar.has_focus(), "a new strip holds no focus");
    require(!bar.has_open(), "a new strip opens nothing");

    require(
        bar.move(veyra::MenubarMove::next, titles).result == veyra::MenubarResult::ignored,
        "roving outside the strip is ignored"
    );
    require(
        bar.open_current(titles).result == veyra::MenubarResult::ignored,
        "opening outside the strip is ignored"
    );
    require(bar.drill().result == veyra::MenubarResult::ignored, "drilling outside is ignored");
    require(bar.escape().result == veyra::MenubarResult::ignored, "escaping outside is ignored");
    require(bar.exit_strip().result == veyra::MenubarResult::ignored, "exiting outside is ignored");
    require(
        bar.dismiss_menu().result == veyra::MenubarResult::ignored,
        "dismissing with no menu is ignored"
    );
    require(
        bar.enter_strip(none).result == veyra::MenubarResult::ignored,
        "entering an empty strip is ignored"
    );
    require(!bar.in_strip(), "a rejected enter leaves the strip cold");

    const auto entered = bar.enter_strip(titles);
    require(
        entered.result == veyra::MenubarResult::changed && entered.id == 1,
        "enter arms the first title"
    );
    require(bar.in_strip() && bar.focus() == 1, "enter plants strip focus");

    const auto repeated = bar.enter_strip(titles);
    require(
        repeated.result == veyra::MenubarResult::handled && repeated.id == 1,
        "re-entering reports no change"
    );

    const auto left = bar.exit_strip();
    require(
        left.result == veyra::MenubarResult::exited && left.closed == 0,
        "exiting a quiet strip names no menu"
    );
    require(!bar.in_strip() && !bar.has_open(), "exit clears the strip");
    require(bar.has_focus() && bar.focus() == 1, "exit keeps the strip position");

    const auto returned = bar.enter_strip(titles);
    require(
        returned.result == veyra::MenubarResult::changed && returned.id == 1,
        "re-enter restores the kept position"
    );
}

void check_wrap_roving() {
    auto bar = veyra::MenubarController{};
    const auto titles = bar_titles();
    bar.enter_strip(titles);

    auto moved = bar.move(veyra::MenubarMove::next, titles);
    require(
        moved.result == veyra::MenubarResult::changed && moved.opened == 2,
        "next names the new focus"
    );
    require(bar.focus() == 2 && !bar.has_open(), "quiet roving opens nothing");

    require(bar.move(veyra::MenubarMove::prev, titles).opened == 1, "prev steps back");
    require(bar.move(veyra::MenubarMove::prev, titles).opened == 4, "prev wraps to the end");
    require(bar.move(veyra::MenubarMove::next, titles).opened == 1, "next wraps to the front");
    require(bar.move(veyra::MenubarMove::last, titles).opened == 4, "last jumps to the end");
    require(bar.move(veyra::MenubarMove::first, titles).opened == 1, "first jumps to the front");
    require(
        bar.move(veyra::MenubarMove::first, titles).result == veyra::MenubarResult::handled,
        "repeating a jump reports no change"
    );

    std::array<veyra::MenubarTitle, 1> solo{{{.id = 5}}};
    auto lone = veyra::MenubarController{};
    lone.enter_strip(solo);
    require(
        lone.move(veyra::MenubarMove::next, solo).result == veyra::MenubarResult::handled &&
            lone.move(veyra::MenubarMove::prev, solo).result == veyra::MenubarResult::handled,
        "roving a single title stays put"
    );

    std::array<veyra::MenubarTitle, 2> swapped{{{.id = 2}, {.id = 3}}};
    auto stale = veyra::MenubarController{};
    stale.enter_strip(titles);
    const auto reanchored = stale.move(veyra::MenubarMove::next, swapped);
    require(
        reanchored.result == veyra::MenubarResult::changed && reanchored.opened == 2,
        "a focus id gone from the span re-anchors from the front"
    );
}

void check_open_and_switch() {
    auto bar = veyra::MenubarController{};
    const auto titles = bar_titles();
    bar.enter_strip(titles);

    const auto opened = bar.open_current(titles);
    require(
        opened.result == veyra::MenubarResult::opened && opened.closed == 0 && opened.opened == 1,
        "open names the focused title with no predecessor"
    );
    require(bar.has_open() && bar.open() == 1, "open records the menu id");

    const auto repeated = bar.open_current(titles);
    require(repeated.result == veyra::MenubarResult::handled, "re-opening reports no change");
    require(bar.open() == 1, "a duplicate open keeps the menu");

    const auto switched = bar.move(veyra::MenubarMove::next, titles);
    require(
        switched.result == veyra::MenubarResult::switched && switched.closed == 1 &&
            switched.opened == 2,
        "roving with a menu open reports close plus open in one result"
    );
    require(bar.focus() == 2 && bar.open() == 2, "a switch moves focus with the menu");

    const auto back = bar.move(veyra::MenubarMove::prev, titles);
    require(
        back.result == veyra::MenubarResult::switched && back.closed == 2 && back.opened == 1,
        "roving back switches the menu back"
    );

    std::array<veyra::MenubarTitle, 1> solo{{{.id = 5}}};
    auto lone = veyra::MenubarController{};
    lone.enter_strip(solo);
    lone.open_current(solo);
    require(
        lone.move(veyra::MenubarMove::next, solo).result == veyra::MenubarResult::handled &&
            lone.open() == 5,
        "switching a single-title strip stays on its menu"
    );
}

void check_stale_open_switch() {
    auto bar = veyra::MenubarController{};
    const auto titles = bar_titles();
    bar.enter_strip(titles);
    bar.open_current(titles);

    std::array<veyra::MenubarTitle, 2> swapped{{{.id = 2}, {.id = 3}}};
    require(
        bar.open_current(swapped).result == veyra::MenubarResult::ignored,
        "opening against a span without the focus is ignored"
    );
    require(bar.open() == 1, "a rejected open keeps the stale menu id");

    const auto switched = bar.move(veyra::MenubarMove::next, swapped);
    require(
        switched.result == veyra::MenubarResult::switched && switched.closed == 1 &&
            switched.opened == 2,
        "a switch still names the stale menu as the one closing"
    );
}

void check_drill_intent() {
    auto bar = veyra::MenubarController{};
    const auto titles = bar_titles();

    require(bar.drill().result == veyra::MenubarResult::ignored, "drilling outside is ignored");

    bar.enter_strip(titles);
    require(
        bar.drill().result == veyra::MenubarResult::ignored,
        "drilling with no menu open is ignored"
    );

    bar.open_current(titles);
    const auto down = bar.drill();
    require(
        down.result == veyra::MenubarResult::drill && down.id == 1,
        "drill names the open menu for the app to press into"
    );
    require(bar.open() == 1, "a drill intent leaves the menu open");
    require(bar.drill().result == veyra::MenubarResult::drill, "drill intents repeat cleanly");
}

void check_two_stage_escape() {
    auto bar = veyra::MenubarController{};
    const auto titles = bar_titles();
    bar.enter_strip(titles);
    bar.move(veyra::MenubarMove::next, titles);
    bar.open_current(titles);

    const auto first = bar.escape();
    require(
        first.result == veyra::MenubarResult::back_to_strip && first.id == 2,
        "first escape names the closing menu"
    );
    require(bar.in_strip() && !bar.has_open(), "first escape lands back in the strip");
    require(bar.focus() == 2, "first escape keeps the strip position");
    require(bar.drill().result == veyra::MenubarResult::ignored, "drill dies with the menu");

    const auto second = bar.escape();
    require(second.result == veyra::MenubarResult::exited, "second escape leaves the strip");
    require(!bar.in_strip() && bar.focus() == 2, "exit keeps the position for re-enter");
    require(bar.escape().result == veyra::MenubarResult::ignored, "escaping outside is ignored");

    auto fleeing = veyra::MenubarController{};
    fleeing.enter_strip(titles);
    fleeing.open_current(titles);
    const auto bail = fleeing.exit_strip();
    require(
        bail.result == veyra::MenubarResult::exited && bail.closed == 1,
        "exiting with a menu open names it so the app can close it"
    );
    require(!fleeing.in_strip() && !fleeing.has_open(), "exit clears menu and strip together");
}

void check_click_toggle() {
    auto bar = veyra::MenubarController{};
    const auto titles = bar_titles();

    const auto cold = bar.click(3, titles);
    require(
        cold.result == veyra::MenubarResult::opened && cold.closed == 0 && cold.opened == 3,
        "clicking into a cold strip enters and opens at once"
    );
    require(bar.in_strip() && bar.focus() == 3, "a cold click plants strip focus");

    const auto swap = bar.click(2, titles);
    require(
        swap.result == veyra::MenubarResult::switched && swap.closed == 3 && swap.opened == 2,
        "clicking another title switches like an arrow key"
    );
    require(bar.focus() == 2 && bar.open() == 2, "a click switch moves focus with the menu");

    const auto toggle = bar.click(2, titles);
    require(
        toggle.result == veyra::MenubarResult::back_to_strip && toggle.closed == 2,
        "clicking the open title closes back to the strip"
    );
    require(
        bar.in_strip() && !bar.has_open() && bar.focus() == 2,
        "a toggle keeps strip focus with no menu"
    );

    const auto reopen = bar.click(2, titles);
    require(
        reopen.result == veyra::MenubarResult::opened && reopen.opened == 2,
        "clicking a quiet title opens fresh"
    );

    require(
        bar.click(99, titles).result == veyra::MenubarResult::ignored,
        "clicking an unknown id is ignored"
    );
    require(
        bar.click(0, titles).result == veyra::MenubarResult::ignored,
        "clicking a null id is ignored"
    );
    require(bar.open() == 2, "rejected clicks keep the open menu");
}

void check_dismiss_menu() {
    auto bar = veyra::MenubarController{};
    const auto titles = bar_titles();
    bar.enter_strip(titles);
    bar.open_current(titles);

    const auto dismissed = bar.dismiss_menu();
    require(
        dismissed.result == veyra::MenubarResult::back_to_strip && dismissed.id == 1,
        "dismiss names the menu the app just closed"
    );
    require(
        bar.in_strip() && !bar.has_open() && bar.focus() == 1,
        "dismiss keeps strip focus with no menu"
    );
    require(
        bar.dismiss_menu().result == veyra::MenubarResult::ignored,
        "dismissing twice reports nothing the second time"
    );
}

void check_disabled_titles() {
    auto bar = veyra::MenubarController{};
    std::array<veyra::MenubarTitle, 4> titles{{
        {.id = 1},
        {.id = 2, .disabled = true},
        {.id = 3},
        {.id = 4, .disabled = true},
    }};

    const auto entered = bar.enter_strip(titles);
    require(
        entered.result == veyra::MenubarResult::changed && entered.id == 1,
        "enter lands on the first enabled title"
    );

    require(bar.move(veyra::MenubarMove::next, titles).opened == 3, "next skips disabled titles");
    require(bar.move(veyra::MenubarMove::next, titles).opened == 1, "wrap skips disabled titles");
    require(bar.move(veyra::MenubarMove::prev, titles).opened == 3, "prev skips disabled too");
    require(
        bar.click(2, titles).result == veyra::MenubarResult::ignored,
        "clicking a disabled title is ignored"
    );
    require(!bar.has_open(), "a rejected click opens nothing");

    std::array<veyra::MenubarTitle, 2> retired{{{.id = 1, .disabled = true}, {.id = 2}}};
    auto grayed = veyra::MenubarController{};
    grayed.enter_strip(bar_titles());
    require(
        grayed.open_current(retired).result == veyra::MenubarResult::ignored,
        "opening a freshly disabled focus is ignored"
    );
    require(!grayed.has_open(), "a disabled open keeps the strip quiet");

    std::array<veyra::MenubarTitle, 2> blocked{
        {{.id = 6, .disabled = true}, {.id = 7, .disabled = true}}
    };
    auto dark = veyra::MenubarController{};
    require(
        dark.enter_strip(blocked).result == veyra::MenubarResult::ignored,
        "entering an all-disabled strip is ignored"
    );
    require(!dark.in_strip(), "a rejected enter leaves the strip cold");
}

void check_empty_strip() {
    auto bar = veyra::MenubarController{};
    std::span<const veyra::MenubarTitle> none{};

    require(
        bar.enter_strip(none).result == veyra::MenubarResult::ignored,
        "entering nothing is ignored"
    );
    require(
        bar.move(veyra::MenubarMove::first, none).result == veyra::MenubarResult::ignored,
        "roving nothing is ignored"
    );
    require(
        bar.open_current(none).result == veyra::MenubarResult::ignored,
        "opening nothing is ignored"
    );
    require(
        bar.click(1, none).result == veyra::MenubarResult::ignored,
        "clicking nothing is ignored"
    );
    require(bar.drill().result == veyra::MenubarResult::ignored, "drilling nothing is ignored");
}

void check_semantics() {
    auto bar = veyra::MenubarController{};
    const auto titles = bar_titles();

    auto record = veyra::SemanticNodeData{
        .role = veyra::SemanticRole::text,
        .label = "unchanged",
    };

    require(!bar.write_bar_semantics({}, record), "an empty bar name is rejected");
    require(record.role == veyra::SemanticRole::text, "a rejected bar write stays unchanged");
    require(bar.write_bar_semantics("Menu bar", record), "a named bar writes semantics");
    require(
        record.role == veyra::SemanticRole::menubar && record.label == "Menu bar",
        "the container exposes the menubar role"
    );

    require(!bar.write_title_semantics(titles[0], {}, record), "an empty title name is rejected");
    const veyra::MenubarTitle ghost{};
    require(!bar.write_title_semantics(ghost, "Ghost", record), "a null title id is rejected");

    bar.enter_strip(titles);
    require(bar.write_title_semantics(titles[0], "File", record), "a title writes semantics");
    require(
        record.role == veyra::SemanticRole::menu_item &&
            veyra::has_state(record.states, veyra::SemanticState::focused) &&
            veyra::has_action(record.actions, veyra::SemanticAction::expand),
        "the focused title exposes focused state and an expand action"
    );
    require(
        bar.write_title_semantics(titles[1], "Edit", record) &&
            !veyra::has_state(record.states, veyra::SemanticState::focused),
        "a quiet title writes no focused state"
    );

    const veyra::MenubarTitle grayed{.id = 9, .disabled = true};
    require(
        bar.write_title_semantics(grayed, "Grayed", record) &&
            veyra::has_state(record.states, veyra::SemanticState::disabled) && record.actions == 0,
        "a disabled title exposes disabled state and no actions"
    );

    bar.open_current(titles);
    require(
        bar.write_title_semantics(titles[0], "File", record) &&
            veyra::has_state(record.states, veyra::SemanticState::expanded),
        "the open title exposes the expanded state"
    );
    require(
        bar.write_title_semantics(titles[1], "Edit", record),
        "a quiet title rewrites for the snapshot"
    );

    std::array<veyra::SemanticNodeData, 1> data{record};
    std::array<veyra::LayoutRect, 1> bounds{veyra::LayoutRect{.width = 10.0F, .height = 10.0F}};
    std::array<veyra::SemanticNode, 1> output{};
    std::size_t node_count = 0;
    veyra::NodeTree tree;
    tree.reserve(1);
    const auto root = tree.create();
    const auto result =
        veyra::build_semantic_snapshot(tree, root, {}, data, bounds, output, node_count);
    require(result == veyra::SemanticSnapshotResult::complete, "the title record validates");
}

}

int main() {
    try {
        check_enter_and_exit();
        check_wrap_roving();
        check_open_and_switch();
        check_stale_open_switch();
        check_drill_intent();
        check_two_stage_escape();
        check_click_toggle();
        check_dismiss_menu();
        check_disabled_titles();
        check_empty_strip();
        check_semantics();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
