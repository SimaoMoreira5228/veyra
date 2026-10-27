#include "veyra/behaviors/navigation/keybinding.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

constexpr std::uint32_t kCtrl = veyra::modifier_flag(veyra::ShortcutModifier::ctrl);

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

std::array<veyra::ComboOption, 4> action_options() {
    return {{
        {.id = 11, .label = "Copy"},
        {.id = 12, .label = "Cut"},
        {.id = 13, .label = "Paste"},
        {.id = 14, .label = "Copy Path"},
    }};
}

void check_search_narrows_actions() {
    auto editor = veyra::KeybindingEditor{};
    require(!editor.is_open() && !editor.capturing(), "a new editor is closed and idle");

    auto actions = action_options();
    require(
        editor.open(actions) == veyra::KeybindingResult::opened,
        "opening over actions reports opened"
    );
    require(editor.is_open(), "open state is observable");
    require(editor.list().match_count() == 4, "an empty query matches every action");

    require(
        editor.apply_filter("Copy", actions) == veyra::KeybindingResult::changed,
        "filtering narrows the action list"
    );
    require(
        editor.list().match_count() == 2 && editor.list().contains(11) &&
            editor.list().contains(14),
        "a mid-string hit matches both Copy rows"
    );
    require(!editor.list().contains(12), "Cut drops out of the narrowed set");

    require(
        editor.apply_filter("zzz", actions) == veyra::KeybindingResult::changed &&
            editor.list().match_count() == 0,
        "a filter with zero hits empties the match set"
    );
    require(
        editor.start_capture(11) == veyra::KeybindingResult::ignored && !editor.capturing(),
        "capture cannot arm when the match set is empty"
    );

    auto closed = veyra::KeybindingEditor{};
    require(
        closed.apply_filter("Copy", actions) == veyra::KeybindingResult::ignored,
        "filtering while closed is ignored"
    );
    require(
        closed.start_capture(11) == veyra::KeybindingResult::ignored && !closed.capturing(),
        "capture cannot arm while closed"
    );
}

void check_capture_arms_only_on_matches() {
    auto editor = veyra::KeybindingEditor{};
    auto actions = action_options();
    editor.open(actions);
    editor.apply_filter("Copy", actions);

    require(
        editor.start_capture(12) == veyra::KeybindingResult::ignored && !editor.capturing(),
        "capture refuses an id outside the current match set"
    );
    require(
        editor.start_capture(0) == veyra::KeybindingResult::ignored && !editor.capturing(),
        "capture refuses the reserved id"
    );
    require(
        editor.start_capture(11) == veyra::KeybindingResult::changed && editor.capturing() &&
            editor.capture_id() == 11,
        "capture arms on a matched action and names it"
    );
    require(
        editor.start_capture(14) == veyra::KeybindingResult::ignored && editor.capture_id() == 11,
        "a second arm while capturing is ignored with the first id held"
    );
}

void check_feed_valid_binds() {
    auto editor = veyra::KeybindingEditor{};
    auto map = veyra::ShortcutMap{};
    auto actions = action_options();
    editor.open(actions);

    require(
        editor.start_capture(11) == veyra::KeybindingResult::changed,
        "capture arms for the feed fixture"
    );

    const veyra::KeybindingFeed fed = editor.feed_chord({7, kCtrl}, map);
    require(fed.result == veyra::KeybindingFeedResult::bound, "a fresh chord reports bound");
    require(fed.occupier == 0, "a fresh feed names no occupier");
    require(!editor.capturing() && editor.capture_id() == 0, "a bound feed disarms capture");

    const veyra::ShortcutHit hit = map.dispatch({7, kCtrl});
    require(hit.has && hit.command == 11, "dispatch fires the newly bound action");

    require(
        editor.feed_chord({8, kCtrl}, map).result == veyra::KeybindingFeedResult::ignored,
        "feeding while idle is ignored"
    );
    require(!map.contains({8, kCtrl}), "an idle feed binds nothing");
}

void check_feed_conflict_reports_occupier() {
    auto editor = veyra::KeybindingEditor{};
    auto map = veyra::ShortcutMap{};
    auto actions = action_options();
    editor.open(actions);
    map.bind({7, kCtrl}, 12);

    require(
        editor.start_capture(11) == veyra::KeybindingResult::changed,
        "capture arms for the conflict fixture"
    );

    const veyra::KeybindingFeed fed = editor.feed_chord({7, kCtrl}, map);
    require(fed.result == veyra::KeybindingFeedResult::conflict, "a taken chord reports conflict");
    require(fed.occupier == 12, "the conflict names the occupying action");
    require(!editor.capturing(), "a conflicted feed disarms capture");

    const veyra::ShortcutHit hit = map.dispatch({7, kCtrl});
    require(hit.has && hit.command == 12, "a conflict leaves the old binding in place");
    require(map.count() == 1, "a conflict stores nothing new");
}

void check_feed_invalid_rejected() {
    auto editor = veyra::KeybindingEditor{};
    auto map = veyra::ShortcutMap{};
    auto actions = action_options();
    editor.open(actions);

    require(
        editor.start_capture(11) == veyra::KeybindingResult::changed,
        "capture arms for the invalid fixture"
    );

    const veyra::KeybindingFeed no_key = editor.feed_chord({0, kCtrl}, map);
    require(no_key.result == veyra::KeybindingFeedResult::invalid, "key zero is invalid");
    require(!editor.capturing(), "an invalid feed disarms capture");

    require(
        editor.start_capture(11) == veyra::KeybindingResult::changed,
        "capture re-arms after an invalid feed"
    );

    constexpr std::uint32_t kUnknown = 1U << 29U;
    const veyra::KeybindingFeed unknown = editor.feed_chord({7, kUnknown}, map);
    require(
        unknown.result == veyra::KeybindingFeedResult::invalid,
        "unknown modifier bits are invalid"
    );
    require(unknown.occupier == 0, "an invalid feed names no occupier");
    require(map.empty(), "invalid feeds store nothing");
}

void check_feed_idempotent_and_overflow() {
    auto editor = veyra::KeybindingEditor{};
    auto map = veyra::ShortcutMap{};
    auto actions = action_options();
    editor.open(actions);

    editor.start_capture(11);
    require(
        editor.feed_chord({7, kCtrl}, map).result == veyra::KeybindingFeedResult::bound,
        "the idempotency fixture binds first"
    );

    require(
        editor.start_capture(11) == veyra::KeybindingResult::changed,
        "capture re-arms on the same action"
    );
    require(
        editor.feed_chord({7, kCtrl}, map).result == veyra::KeybindingFeedResult::handled,
        "feeding the action's own chord reports handled"
    );
    require(map.count() == 1, "an idempotent feed stores nothing new");

    auto full = veyra::ShortcutMap{};
    for (std::uint32_t key = 1; key <= veyra::ShortcutMap::kCapacity; ++key) {
        const std::uint64_t command = static_cast<std::uint64_t>(1000 + key);
        const veyra::ShortcutBind bound = full.bind({key, kCtrl}, command);
        require(bound.result == veyra::ShortcutResult::bound, "filling to capacity binds");
    }

    auto crowded = veyra::KeybindingEditor{};
    crowded.open(actions);
    require(
        crowded.start_capture(13) == veyra::KeybindingResult::changed,
        "capture arms against the full map"
    );

    const veyra::KeybindingFeed spill = crowded.feed_chord({200, kCtrl}, full);
    require(
        spill.result == veyra::KeybindingFeedResult::overflow,
        "one chord past capacity overflows"
    );
    require(spill.occupier == 0, "overflow names no occupier");
    require(full.count() == veyra::ShortcutMap::kCapacity, "overflow stores nothing");
    require(!full.dispatch({200, kCtrl}).has, "the spilled chord misses");
}

void check_cancel_disarms() {
    auto editor = veyra::KeybindingEditor{};
    auto actions = action_options();
    editor.open(actions);

    require(
        editor.cancel_capture() == veyra::KeybindingResult::handled,
        "cancelling while idle reports no change"
    );

    require(
        editor.start_capture(12) == veyra::KeybindingResult::changed,
        "capture arms for the cancel fixture"
    );
    require(
        editor.cancel_capture() == veyra::KeybindingResult::changed && !editor.capturing() &&
            editor.capture_id() == 0,
        "cancel disarms capture and clears the id"
    );
    require(
        editor.cancel_capture() == veyra::KeybindingResult::handled,
        "repeating a cancel reports no change"
    );
}

void check_clear_binding_removes() {
    auto editor = veyra::KeybindingEditor{};
    auto map = veyra::ShortcutMap{};
    auto actions = action_options();
    editor.open(actions);

    editor.start_capture(11);
    editor.feed_chord({7, kCtrl}, map);
    map.bind({8, kCtrl}, 11);

    const veyra::KeybindingClear cleared = editor.clear_binding(11, map);
    require(
        cleared.result == veyra::KeybindingResult::changed && cleared.removed == 2,
        "clearing drops every chord of the action and reports the count"
    );
    require(!map.dispatch({7, kCtrl}).has && !map.dispatch({8, kCtrl}).has, "cleared chords miss");

    const veyra::KeybindingClear repeat = editor.clear_binding(11, map);
    require(
        repeat.result == veyra::KeybindingResult::handled && repeat.removed == 0,
        "clearing a chordless action reports no change"
    );

    const veyra::KeybindingClear zero = editor.clear_binding(0, map);
    require(
        zero.result == veyra::KeybindingResult::ignored && zero.removed == 0,
        "clearing the reserved id is ignored"
    );
}

void check_capturing_blocks_moves() {
    auto editor = veyra::KeybindingEditor{};
    auto map = veyra::ShortcutMap{};
    auto actions = action_options();
    editor.open(actions);

    const std::uint64_t parked = editor.list().active().id;
    require(editor.list().active().has, "the open fixture parks an active row");

    require(
        editor.start_capture(12) == veyra::KeybindingResult::changed,
        "capture arms for the blocking fixture"
    );
    require(
        editor.move_active(veyra::ComboMove::next) == veyra::KeybindingResult::ignored &&
            editor.list().active().id == parked,
        "roving while capturing is ignored without moving active"
    );
    require(
        editor.start_capture(13) == veyra::KeybindingResult::ignored,
        "re-arming while capturing is ignored"
    );

    require(
        editor.apply_filter("Copy", actions) == veyra::KeybindingResult::changed &&
            editor.capturing() && editor.capture_id() == 12,
        "filtering still narrows while capturing and keeps the arm"
    );
    require(
        editor.commit_active().result == veyra::ComboResult::ignored,
        "commit_active while capturing is ignored through the editor"
    );
    require(
        editor.commit_id(11, actions).result == veyra::ComboResult::ignored,
        "commit_id while capturing is ignored through the editor"
    );
    require(
        editor.commit_text("Copy").result == veyra::ComboResult::ignored,
        "commit_text while capturing is ignored through the editor"
    );

    const veyra::KeybindingFeed fed = editor.feed_chord({7, kCtrl}, map);
    require(
        fed.result == veyra::KeybindingFeedResult::bound && map.dispatch({7, kCtrl}).command == 12,
        "the held arm still binds after a mid-capture filter"
    );
    require(
        editor.move_active(veyra::ComboMove::next) == veyra::KeybindingResult::changed,
        "roving works again once capture resolves"
    );
}

void check_commit_paths_reach_commit() {
    auto editor = veyra::KeybindingEditor{};
    auto map = veyra::ShortcutMap{};
    auto actions = action_options();

    require(
        editor.open(actions) == veyra::KeybindingResult::opened,
        "the commit fixture opens over every action"
    );
    require(
        editor.move_active(veyra::ComboMove::next) == veyra::KeybindingResult::changed,
        "roving steps off the parked first row"
    );

    const veyra::ComboCommit committed = editor.commit_active();
    require(
        committed.result == veyra::ComboResult::commit && committed.id == 12,
        "commit_active reaches commit naming the roved-to action"
    );
    require(
        editor.list().has_value() && editor.list().value() == 12,
        "the committed action is recorded as the editor value"
    );

    auto bound = veyra::KeybindingEditor{};
    bound.open(actions);

    require(
        bound.start_capture(committed.id) == veyra::KeybindingResult::changed,
        "capture arms on the committed action"
    );

    const veyra::KeybindingFeed fed = bound.feed_chord({7, kCtrl}, map);
    require(
        fed.result == veyra::KeybindingFeedResult::bound &&
            map.dispatch({7, kCtrl}).command == committed.id,
        "dispatch fires the committed action after its chord binds"
    );

    auto by_id = veyra::KeybindingEditor{};
    by_id.open(actions);

    const veyra::ComboCommit named = by_id.commit_id(14, actions);
    require(
        named.result == veyra::ComboResult::commit && named.id == 14,
        "commit_id through the editor reaches commit for a listed action"
    );

    auto text = veyra::KeybindingEditor{};
    text.open(actions);
    require(
        text.commit_text("Copy").result == veyra::ComboResult::ignored,
        "commit_text through the editor is ignored without a free-mode switch"
    );

    auto filtered = veyra::KeybindingEditor{};
    require(
        filtered.open(actions, "Copy") == veyra::KeybindingResult::opened,
        "opening with an initial filter reports opened"
    );
    require(
        filtered.list().match_count() == 2 && filtered.list().active().has &&
            filtered.list().active().id == 11,
        "an initial filter parks active on the first hit"
    );
}

void check_disabled_options_are_skipped() {
    auto actions = std::array<veyra::ComboOption, 4>{{
        {.id = 11, .label = "Copy", .disabled = true},
        {.id = 12, .label = "Cut"},
        {.id = 13, .label = "Paste"},
        {.id = 14, .label = "Copy Path"},
    }};

    auto editor = veyra::KeybindingEditor{};
    editor.open(actions);
    require(
        editor.list().active().has && editor.list().active().id == 12,
        "opening parks active on the first enabled action"
    );
    require(
        editor.start_capture(11) == veyra::KeybindingResult::ignored && !editor.capturing(),
        "capture refuses a disabled action id"
    );

    const veyra::ComboCommit named = editor.commit_id(11, actions);
    require(
        named.result == veyra::ComboResult::ignored,
        "commit_id through the editor refuses a disabled action"
    );

    const veyra::ComboCommit parked = editor.commit_active();
    require(
        parked.result == veyra::ComboResult::commit && parked.id == 12,
        "commit_active never parks on the disabled action"
    );
}

void check_close_and_dismiss_disarm() {
    auto editor = veyra::KeybindingEditor{};
    auto actions = action_options();
    editor.open(actions);
    editor.start_capture(11);

    require(
        editor.close() == veyra::KeybindingResult::closed && !editor.is_open() &&
            !editor.capturing(),
        "close reports closed and disarms capture"
    );
    require(
        editor.close() == veyra::KeybindingResult::handled,
        "closing a closed editor reports no change"
    );

    editor.open(actions);
    editor.start_capture(11);
    require(
        editor.dismiss() == veyra::KeybindingResult::dismissed && !editor.is_open() &&
            !editor.capturing(),
        "dismiss reports dismissed and disarms capture"
    );
    require(
        editor.dismiss() == veyra::KeybindingResult::handled,
        "dismissing a closed editor reports no change"
    );
}

void check_map_full_reports_overflow() {
    auto map = veyra::ShortcutMap{};

    for (auto chord = std::uint32_t{1}; chord <= veyra::ShortcutMap::kCapacity; ++chord) {
        const auto bound =
            map.bind({chord, veyra::modifier_flag(veyra::ShortcutModifier::ctrl)}, 1000 + chord);

        require(bound.result == veyra::ShortcutResult::bound, "filling the map binds every chord");
    }

    auto editor = veyra::KeybindingEditor{};
    auto actions = action_options();
    editor.open(actions);
    editor.start_capture(11);

    const auto spill =
        editor.feed_chord({9999, veyra::modifier_flag(veyra::ShortcutModifier::ctrl)}, map);

    require(
        spill.result == veyra::KeybindingFeedResult::overflow && spill.occupier == 0,
        "one chord past map capacity overflows without naming an occupier"
    );
    require(!editor.capturing(), "an overflowing feed disarms capture");
}

void check_disabled_blocks_all() {
    auto editor = veyra::KeybindingEditor{};
    auto map = veyra::ShortcutMap{};
    auto actions = action_options();
    editor.open(actions);
    editor.start_capture(11);

    require(editor.set_disabled(true), "disabling reports a change");
    require(!editor.capturing() && editor.capture_id() == 0, "disabling disarms capture");
    require(
        editor.open(actions) == veyra::KeybindingResult::ignored &&
            editor.apply_filter("Copy", actions) == veyra::KeybindingResult::ignored &&
            editor.move_active(veyra::ComboMove::next) == veyra::KeybindingResult::ignored &&
            editor.start_capture(11) == veyra::KeybindingResult::ignored &&
            editor.feed_chord({7, kCtrl}, map).result == veyra::KeybindingFeedResult::ignored &&
            editor.cancel_capture() == veyra::KeybindingResult::ignored &&
            editor.clear_binding(11, map).result == veyra::KeybindingResult::ignored &&
            editor.close() == veyra::KeybindingResult::ignored &&
            editor.dismiss() == veyra::KeybindingResult::ignored,
        "every mutation is ignored while disabled"
    );
    require(map.empty(), "rejected feeds bind nothing");
    require(!editor.set_disabled(true), "repeating the disable reports no change");

    require(editor.set_disabled(false), "enabling reports a change");
    require(editor.close() == veyra::KeybindingResult::closed, "close works again after enabling");
    require(
        editor.open(actions) == veyra::KeybindingResult::opened &&
            editor.start_capture(11) == veyra::KeybindingResult::changed,
        "mutation works again after enabling"
    );
}

void check_semantics_through_list() {
    auto editor = veyra::KeybindingEditor{};
    auto actions = action_options();
    editor.open(actions);

    auto box = veyra::SemanticNodeData{};
    require(editor.list().write_box_semantics("Shortcuts", box), "the list writes box semantics");
    require(
        box.role == veyra::SemanticRole::combobox && box.label == "Shortcuts",
        "the box record carries the combobox role and name"
    );
    require(
        !editor.list().write_box_semantics({}, box),
        "an empty box name is rejected through the accessor"
    );

    auto list = veyra::SemanticNodeData{};
    require(editor.list().write_list_semantics("Actions", list), "the list writes list semantics");
    require(
        list.role == veyra::SemanticRole::listbox,
        "the popup exposes the listbox role through the accessor"
    );

    auto item = veyra::SemanticNodeData{};
    require(editor.list().write_item_semantics(11, "Copy", item), "an item writes semantics");
    require(
        item.role == veyra::SemanticRole::option && item.label == "Copy",
        "the item record carries the option role and name"
    );
    require(
        !editor.list().write_item_semantics(0, "Zero", item),
        "a zero item id is rejected through the accessor"
    );
}

}

int main() {
    try {
        check_search_narrows_actions();
        check_capture_arms_only_on_matches();
        check_feed_valid_binds();
        check_feed_conflict_reports_occupier();
        check_feed_invalid_rejected();
        check_feed_idempotent_and_overflow();
        check_cancel_disarms();
        check_clear_binding_removes();
        check_capturing_blocks_moves();
        check_commit_paths_reach_commit();
        check_disabled_options_are_skipped();
        check_close_and_dismiss_disarm();
        check_map_full_reports_overflow();
        check_disabled_blocks_all();
        check_semantics_through_list();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
