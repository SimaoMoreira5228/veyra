#include "veyra/behaviors/navigation/menu.hpp"

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

using Items = std::array<veyra::MenuItem, 6>;

Items root_items() {
    return {{
        {.id = 1, .kind = veyra::MenuItemKind::command},
        {.id = 2, .kind = veyra::MenuItemKind::separator},
        {.id = 3, .kind = veyra::MenuItemKind::command, .disabled = true},
        {.id = 4, .kind = veyra::MenuItemKind::checkable},
        {.id = 5, .kind = veyra::MenuItemKind::radio},
        {.id = 6, .kind = veyra::MenuItemKind::submenu, .has_children = true},
    }};
}

void check_open_resets_and_arms_first() {
    auto menu = veyra::MenuController{};
    const auto items = root_items();

    require(menu.empty(), "stack starts empty");
    require(menu.depth() == 0, "depth starts at zero");
    require(!menu.active().has, "no active item before open");

    require(menu.open_root(40, items) == veyra::MenuResult::opened, "root opens");
    require(!menu.empty(), "stack holds the root");
    require(menu.depth() == 1, "root is a single level");
    require(menu.menu_at(0) == 40, "root level keeps its menu id");
    require(menu.active().has && menu.active().id == 1, "open arms the first enabled item");

    require(menu.move(veyra::MenuMove::next, items) == veyra::MenuResult::changed, "next moves");
    require(menu.active().id == 4, "next skips the separator and the disabled command");
    require(menu.open_root(41, items) == veyra::MenuResult::opened, "reopen replaces the path");
    require(menu.depth() == 1 && menu.menu_at(0) == 41, "reopen resets to one level");
    require(menu.active().id == 1, "reopen re-arms the first item");

    require(menu.open_root(0, items) == veyra::MenuResult::ignored, "null menu id is rejected");
    require(menu.depth() == 1, "rejected open leaves the stack alone");
}

void check_wrap_navigation() {
    auto menu = veyra::MenuController{};
    const auto items = root_items();
    menu.open_root(40, items);

    require(menu.move(veyra::MenuMove::prev, items) == veyra::MenuResult::changed, "prev moves");
    require(menu.active().id == 6, "prev wraps from the first item to the last");
    require(menu.move(veyra::MenuMove::next, items) == veyra::MenuResult::changed, "next moves");
    require(menu.active().id == 1, "next wraps from the last item to the first");
    require(menu.move(veyra::MenuMove::last, items) == veyra::MenuResult::changed, "last moves");
    require(menu.active().id == 6, "last lands on the final enabled item");
    require(menu.move(veyra::MenuMove::last, items) == veyra::MenuResult::handled, "last repeats");
    require(menu.move(veyra::MenuMove::first, items) == veyra::MenuResult::changed, "first moves");
    require(menu.active().id == 1, "first lands on the front item");
    require(
        menu.move(veyra::MenuMove::first, items) == veyra::MenuResult::handled,
        "repeating first reports no change"
    );

    std::array<veyra::MenuItem, 4> solo{{
        {.id = 9, .kind = veyra::MenuItemKind::command},
        {.id = 10, .kind = veyra::MenuItemKind::separator},
        {.id = 11, .kind = veyra::MenuItemKind::command, .disabled = true},
        {.id = 12, .kind = veyra::MenuItemKind::command, .disabled = true},
    }};
    menu.open_root(50, solo);
    require(menu.active().id == 9, "lone enabled item arms");
    require(
        menu.move(veyra::MenuMove::next, solo) == veyra::MenuResult::handled &&
            menu.move(veyra::MenuMove::prev, solo) == veyra::MenuResult::handled,
        "roving on a single enabled item stays put"
    );

    std::array<veyra::MenuItem, 2> blocked{{
        {.id = 13, .kind = veyra::MenuItemKind::separator},
        {.id = 14, .kind = veyra::MenuItemKind::command, .disabled = true},
    }};
    menu.open_root(51, blocked);
    require(!menu.active().has, "all-blocked root arms nothing");
    require(
        menu.move(veyra::MenuMove::next, blocked) == veyra::MenuResult::ignored &&
            menu.move(veyra::MenuMove::first, blocked) == veyra::MenuResult::ignored,
        "roving with nothing selectable is ignored"
    );
}

void check_hover_policy() {
    auto menu = veyra::MenuController{};
    const auto items = root_items();
    menu.open_root(40, items);

    require(menu.hover(4, items) == veyra::MenuResult::changed, "hover moves the active item");
    require(menu.active().id == 4, "hovered item becomes active");
    require(menu.hover(4, items) == veyra::MenuResult::handled, "re-hover reports no change");
    require(menu.hover(2, items) == veyra::MenuResult::ignored, "separator hover is rejected");
    require(menu.hover(3, items) == veyra::MenuResult::ignored, "disabled hover is rejected");
    require(menu.active().id == 4, "rejected hover leaves the active item alone");
    require(menu.hover(99, items) == veyra::MenuResult::ignored, "unknown hover is rejected");

    auto closed = veyra::MenuController{};
    require(
        closed.hover(1, items) == veyra::MenuResult::ignored,
        "hover on empty stack is ignored"
    );
}

void check_drill_and_close_stack() {
    auto menu = veyra::MenuController{};
    const auto items = root_items();
    std::array<veyra::MenuItem, 2> children{{
        {.id = 61, .kind = veyra::MenuItemKind::command},
        {.id = 62, .kind = veyra::MenuItemKind::command, .disabled = true},
    }};

    menu.open_root(40, items);

    require(
        menu.open_submenu(1, items, children) == veyra::MenuResult::ignored,
        "drilling a command is rejected"
    );
    require(
        menu.open_submenu(2, items, children) == veyra::MenuResult::ignored,
        "drilling a separator is rejected"
    );
    require(
        menu.open_submenu(99, items, children) == veyra::MenuResult::ignored,
        "drilling an unknown id is rejected"
    );
    require(menu.depth() == 1, "rejected drills leave the stack alone");

    require(menu.open_submenu(6, items, children) == veyra::MenuResult::opened, "submenu drills");
    require(menu.depth() == 2, "drill pushes a level");
    require(menu.menu_at(1) == 6, "child level keeps its parent item id");
    require(menu.active().id == 61, "drill arms the first enabled child");

    const auto leaf = menu.close_level();
    require(
        leaf.result == veyra::MenuResult::closed && leaf.closed == 6,
        "closing a child reports the submenu id"
    );
    require(menu.depth() == 1, "close pops one level");

    const auto root = menu.close_level();
    require(
        root.result == veyra::MenuResult::dismissed && root.closed == 40,
        "closing the root reports menu-dismissed"
    );
    require(menu.empty(), "closing the root empties the stack");

    const auto missing = menu.close_level();
    require(missing.result == veyra::MenuResult::ignored, "close on empty stack is ignored");
}

void check_empty_submenu_policy() {
    auto menu = veyra::MenuController{};
    const auto items = root_items();
    std::span<const veyra::MenuItem> no_children{};

    menu.open_root(40, items);
    require(
        menu.open_submenu(6, items, no_children) == veyra::MenuResult::opened,
        "empty drill opens"
    );
    require(menu.depth() == 2, "empty submenu still pushes a level");
    require(!menu.active().has, "empty submenu arms no active item");
    require(
        menu.move(veyra::MenuMove::next, no_children) == veyra::MenuResult::ignored,
        "roving an empty level is ignored"
    );
    require(
        menu.activate(no_children, no_children).result == veyra::MenuResult::ignored,
        "activating an empty level is ignored"
    );

    const auto back = menu.close_level();
    require(back.result == veyra::MenuResult::closed, "empty submenu closes like any level");

    std::array<veyra::MenuItem, 1> bare{{
        {.id = 70, .kind = veyra::MenuItemKind::submenu},
    }};
    menu.open_root(41, bare);
    require(
        menu.open_submenu(70, bare, no_children) == veyra::MenuResult::ignored,
        "a submenu without children never drills"
    );
    require(menu.depth() == 1, "childless drill leaves the stack alone");
}

void check_activation_intents() {
    auto menu = veyra::MenuController{};
    const auto items = root_items();
    std::span<const veyra::MenuItem> no_children{};
    menu.open_root(40, items);

    menu.hover(1, items);
    const auto command = menu.activate(items, no_children);
    require(
        command.result == veyra::MenuResult::command && command.id == 1,
        "command reports its id"
    );
    require(menu.depth() == 1, "command intent leaves the stack alone");

    menu.hover(4, items);
    const auto toggle = menu.activate(items, no_children);
    require(
        toggle.result == veyra::MenuResult::toggle && toggle.id == 4,
        "checkable reports toggle"
    );

    menu.hover(5, items);
    const auto radio = menu.activate(items, no_children);
    require(radio.result == veyra::MenuResult::radio && radio.id == 5, "radio reports its id");

    std::array<veyra::MenuItem, 1> children{{
        {.id = 61, .kind = veyra::MenuItemKind::command},
    }};
    menu.hover(6, items);
    const auto drill = menu.activate(items, children);
    require(
        drill.result == veyra::MenuResult::opened && drill.id == 6 && menu.depth() == 2,
        "activate on a submenu drills like right"
    );

    menu.hover(61, children);
    const auto right_on_command = menu.press_right(children, no_children);
    require(
        right_on_command.result == veyra::MenuResult::handled && right_on_command.id == 61,
        "right on a command is swallowed with its id"
    );
    require(menu.depth() == 2, "swallowed right leaves the stack alone");
}

void check_press_right_drills_active() {
    auto menu = veyra::MenuController{};
    const auto items = root_items();
    std::array<veyra::MenuItem, 1> children{{
        {.id = 61, .kind = veyra::MenuItemKind::command},
    }};
    std::span<const veyra::MenuItem> no_children{};

    menu.open_root(40, items);
    menu.hover(1, items);
    require(
        menu.press_right(items, no_children).result == veyra::MenuResult::handled,
        "right off a command goes nowhere"
    );

    menu.hover(6, items);
    const auto drilled = menu.press_right(items, children);
    require(
        drilled.result == veyra::MenuResult::opened && drilled.id == 6 && menu.depth() == 2,
        "right on a submenu drills and names the parent"
    );

    auto empty_drill = veyra::MenuController{};
    require(
        empty_drill.press_right(items, children).result == veyra::MenuResult::ignored,
        "right on empty stack is ignored"
    );
}

void check_escape_level_vs_root_dismiss() {
    auto menu = veyra::MenuController{};
    const auto items = root_items();
    std::array<veyra::MenuItem, 1> children{{
        {.id = 61, .kind = veyra::MenuItemKind::command},
    }};

    menu.open_root(40, items);
    menu.open_submenu(6, items, children);

    const auto level = menu.close_level();
    require(
        level.result == veyra::MenuResult::closed && menu.depth() == 1,
        "escape on a child closes one level"
    );

    const auto root = menu.close_level();
    require(
        root.result == veyra::MenuResult::dismissed && menu.empty(),
        "escape on the root dismisses the menu"
    );
}

void check_outside_and_close_all() {
    auto menu = veyra::MenuController{};
    const auto items = root_items();
    std::array<veyra::MenuItem, 1> children{{
        {.id = 61, .kind = veyra::MenuItemKind::command},
    }};

    require(
        menu.outside_press().result == veyra::MenuResult::ignored,
        "outside press on empty stack is ignored"
    );

    menu.open_root(40, items);
    menu.open_submenu(6, items, children);

    const auto press = menu.outside_press();
    require(
        press.result == veyra::MenuResult::dismissed && press.id == 6,
        "outside press reports dismiss for the top level"
    );
    require(menu.depth() == 2, "outside press reports without popping");

    const auto reset = menu.close_all();
    require(reset.closed == 2, "close all pops every level");
    require(menu.empty(), "close all empties the stack");

    const auto empty_reset = menu.close_all();
    require(empty_reset.closed == 0, "close all on empty stack closes nothing");
}

void check_context_invocation_anchor() {
    auto menu = veyra::MenuController{};
    const auto items = root_items();

    require(menu.open_root_at(40, items, 5) == veyra::MenuResult::opened, "anchored open works");
    require(menu.active().id == 5, "anchor wins over the first item");

    require(menu.open_root_at(41, items, 3) == veyra::MenuResult::opened, "disabled anchor opens");
    require(menu.active().id == 1, "disabled anchor falls back to the first enabled item");

    require(menu.open_root_at(42, items, 2) == veyra::MenuResult::opened, "separator anchor opens");
    require(menu.active().id == 1, "separator anchor falls back too");

    require(menu.open_root_at(43, items, 99) == veyra::MenuResult::opened, "unknown anchor opens");
    require(menu.active().id == 1, "unknown anchor falls back too");
}

void check_overflow_at_capacity() {
    auto menu = veyra::MenuController{};
    std::array<veyra::MenuItem, 1> level{{
        {.id = 6, .kind = veyra::MenuItemKind::submenu, .has_children = true},
    }};
    std::array<veyra::MenuItem, 1> child{{
        {.id = 6, .kind = veyra::MenuItemKind::submenu, .has_children = true},
    }};

    require(menu.open_root(1, level) == veyra::MenuResult::opened, "root opens");

    for (std::size_t i = 1; i < veyra::MenuController::kCapacity; ++i) {
        require(
            menu.open_submenu(6, child, child) == veyra::MenuResult::opened,
            "levels drill up to capacity"
        );
    }
    require(menu.depth() == veyra::MenuController::kCapacity, "stack fills to capacity");

    require(
        menu.open_submenu(6, child, child) == veyra::MenuResult::overflow,
        "drill past capacity overflows"
    );
    require(menu.depth() == veyra::MenuController::kCapacity, "overflow keeps the stack");
}

void check_closed_menu_and_stale_contents() {
    auto menu = veyra::MenuController{};
    const auto items = root_items();
    std::span<const veyra::MenuItem> no_children{};

    require(
        menu.open_root_at(0, items, 1) == veyra::MenuResult::ignored,
        "anchored open rejects a null menu id"
    );
    require(
        menu.open_submenu(6, items, no_children) == veyra::MenuResult::ignored,
        "drilling with no open menu is ignored"
    );
    require(
        menu.move(veyra::MenuMove::next, items) == veyra::MenuResult::ignored,
        "roving with no open menu is ignored"
    );
    require(
        menu.activate(items, no_children).result == veyra::MenuResult::ignored,
        "activating with no open menu is ignored"
    );

    std::array<veyra::MenuItem, 2> blocked{{
        {.id = 13, .kind = veyra::MenuItemKind::separator},
        {.id = 14, .kind = veyra::MenuItemKind::command, .disabled = true},
    }};
    menu.open_root(51, blocked);
    require(
        menu.move(veyra::MenuMove::prev, blocked) == veyra::MenuResult::ignored,
        "backward roving with nothing selectable is ignored"
    );
    require(
        menu.press_right(blocked, no_children).result == veyra::MenuResult::ignored,
        "right with no active item is ignored"
    );

    menu.open_root(40, items);
    std::array<veyra::MenuItem, 1> swapped{{
        {.id = 9, .kind = veyra::MenuItemKind::command},
    }};
    require(
        menu.press_right(swapped, no_children).result == veyra::MenuResult::ignored,
        "right against swapped contents is ignored"
    );
    require(
        menu.activate(swapped, no_children).result == veyra::MenuResult::ignored,
        "activate against swapped contents is ignored"
    );
    require(menu.active().id == 1, "rejected calls keep the armed item");

    std::array<veyra::MenuItem, 1> retired{{
        {.id = 1, .kind = veyra::MenuItemKind::command, .disabled = true},
    }};
    require(
        menu.activate(retired, no_children).result == veyra::MenuResult::ignored,
        "activate on a retired item is ignored"
    );

    std::array<veyra::MenuItem, 2> stale{{
        {.id = 1, .kind = veyra::MenuItemKind::command, .disabled = true},
        {.id = 2, .kind = veyra::MenuItemKind::separator},
    }};
    require(
        menu.move(veyra::MenuMove::next, stale) == veyra::MenuResult::ignored,
        "roving a span with nothing selectable is ignored"
    );

    menu.open_root(40, items);
    menu.hover(6, items);
    std::array<veyra::MenuItem, 1> retired_submenu{{
        {.id = 6, .kind = veyra::MenuItemKind::submenu, .disabled = true, .has_children = true},
    }};
    require(
        menu.open_submenu(6, retired_submenu, no_children) == veyra::MenuResult::ignored,
        "drilling a disabled submenu is rejected"
    );
    require(
        menu.press_right(retired_submenu, no_children).result == veyra::MenuResult::ignored,
        "right on a disabled submenu goes nowhere"
    );
    require(menu.depth() == 1, "rejected submenu calls leave the stack alone");

    std::array<veyra::MenuItem, 1> bare{{
        {.id = 70, .kind = veyra::MenuItemKind::submenu},
    }};
    menu.open_root(42, bare);
    require(
        menu.activate(bare, no_children).result == veyra::MenuResult::ignored,
        "activate on a childless submenu is ignored"
    );
}

void check_semantics() {
    auto menu = veyra::MenuController{};
    const auto items = root_items();
    auto record = veyra::SemanticNodeData{
        .role = veyra::SemanticRole::text,
        .label = "unchanged",
    };

    require(!menu.write_menu_semantics({}, record), "empty menu name is rejected");
    require(record.role == veyra::SemanticRole::text, "rejected menu output stays unchanged");
    require(menu.write_menu_semantics("File", record), "menu semantics write");
    require(
        record.role == veyra::SemanticRole::menu && record.label == "File",
        "container uses the menu role"
    );

    menu.open_root(40, items);

    require(!menu.write_item_semantics(items[1], {}, false, false, record), "empty name rejected");
    require(
        !menu.write_item_semantics(items[1], "Rule", false, false, record),
        "separators write no record"
    );

    require(menu.write_item_semantics(items[0], "Open", false, false, record), "command writes");
    require(
        record.role == veyra::SemanticRole::menu_item &&
            veyra::has_state(record.states, veyra::SemanticState::focused) &&
            veyra::has_action(record.actions, veyra::SemanticAction::activate),
        "active command exposes focused state and an activate action"
    );

    require(menu.write_item_semantics(items[3], "Wrap", true, false, record), "checkable writes");
    require(
        veyra::has_state(record.states, veyra::SemanticState::checked),
        "checked checkable exposes the checked state"
    );

    require(menu.write_item_semantics(items[4], "Unix", false, false, record), "radio writes");
    require(
        !veyra::has_state(record.states, veyra::SemanticState::checked),
        "unchecked radio writes no checked state"
    );
    require(
        !veyra::has_state(record.states, veyra::SemanticState::focused),
        "inactive item writes no focused state"
    );

    require(
        menu.write_item_semantics(items[2], "Cut", false, false, record) &&
            veyra::has_state(record.states, veyra::SemanticState::disabled) && record.actions == 0,
        "disabled items expose the disabled state and no actions"
    );

    require(menu.write_item_semantics(items[5], "More", false, true, record), "submenu writes");
    require(
        veyra::has_state(record.states, veyra::SemanticState::expanded) &&
            veyra::has_action(record.actions, veyra::SemanticAction::expand),
        "expanded submenu exposes expanded state and an expand action"
    );
    require(
        menu.write_item_semantics(items[5], "More", false, false, record) &&
            !veyra::has_state(record.states, veyra::SemanticState::expanded),
        "collapsed submenu writes no expanded state"
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
    require(result == veyra::SemanticSnapshotResult::complete, "item record survives validation");
}

}

void check_keyboard_focus_recovery() {
    auto focus = veyra::FocusChain{};
    focus.register_item(101);
    focus.register_item(102);
    focus.register_item(103);
    focus.register_item(201);
    focus.focus(101);

    auto menu = veyra::MenuController{};
    const auto items = root_items();
    std::array<veyra::MenuItem, 1> children{{
        {.id = 61, .kind = veyra::MenuItemKind::command},
    }};

    require(
        menu.open_root_at(40, items, 1, 102) == veyra::MenuResult::opened,
        "a menu opens with a restore target"
    );
    require(menu.restore_focus() == 102, "the controller keeps the restore target");
    menu.open_submenu(6, items, children);
    focus.focus(201);

    require(
        menu.press_left(focus) == veyra::MenuResult::closed,
        "left closes a submenu before dismissing the root"
    );
    require(focus.focused() == 201, "closing one level keeps the menu focused");

    require(
        menu.press_left(focus) == veyra::MenuResult::dismissed,
        "left on the root dismisses the menu"
    );
    require(focus.focused() == 102, "dismissing restores the opener");
    require(menu.restore_focus() == 0, "a dismissed menu clears its restore target");

    menu.open_root_at(41, items, 1, 102);
    menu.close_all(focus);
    require(focus.focused() == 102, "closing every level restores the opener");

    focus.unregister(102);
    menu.open_root_at(42, items, 1, 102);
    menu.close_all(focus);
    require(focus.focused() == 101, "a removed opener falls back to the first item");

    require(menu.open_root(43, items) == veyra::MenuResult::opened, "a plain open succeeds");
    require(menu.restore_focus() == 0, "a plain open has no restore target");

    menu.open_root_at(44, items, 1, 103);
    focus.focus(201);
    const auto action = menu.activate(items, children, focus);
    require(
        action.result == veyra::MenuResult::command && action.id == 1,
        "enter activates the active command"
    );
    require(menu.empty(), "activation closes every menu level");
    require(focus.focused() == 103, "activation restores the opener");
}

int main() {
    try {
        check_open_resets_and_arms_first();
        check_wrap_navigation();
        check_hover_policy();
        check_drill_and_close_stack();
        check_empty_submenu_policy();
        check_activation_intents();
        check_press_right_drills_active();
        check_escape_level_vs_root_dismiss();
        check_keyboard_focus_recovery();
        check_outside_and_close_all();
        check_context_invocation_anchor();
        check_overflow_at_capacity();
        check_closed_menu_and_stale_contents();
        check_semantics();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
