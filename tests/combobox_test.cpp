#include "veyra/behaviors/combobox.hpp"

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

std::array<veyra::ComboOption, 4> fruit_options() {
    return {{
        {.id = 11, .label = "Apple"},
        {.id = 12, .label = "Banana"},
        {.id = 13, .label = "Pineapple"},
        {.id = 14, .label = "Cherry"},
    }};
}

void check_open_and_empty_query_matches_all() {
    auto combo = veyra::ComboBoxController{};
    require(!combo.is_open(), "a new combo starts closed");

    auto options = fruit_options();
    require(
        combo.open(options) == veyra::ComboResult::opened,
        "opening over options reports opened"
    );
    require(combo.is_open(), "open state is observable");
    require(combo.match_count() == 4, "an empty query matches every option");
    require(
        combo.active().has && combo.active().id == 11,
        "opening parks active on the first match"
    );
    require(combo.contains(13), "a matched id is reported");

    auto empty = veyra::ComboBoxController{};
    std::span<const veyra::ComboOption> no_options{};
    require(
        empty.open(no_options) == veyra::ComboResult::opened && empty.match_count() == 0 &&
            !empty.active().has,
        "opening over zero options still opens but arms nothing"
    );

    auto seeded = veyra::ComboBoxController{};
    require(
        seeded.open(options, "a") == veyra::ComboResult::opened && seeded.match_count() == 2,
        "opening accepts an initial filter"
    );
    require(seeded.active().id == 12, "the initial filter parks active on its own first match");
}

void check_substring_case_sensitive_matching() {
    auto combo = veyra::ComboBoxController{};
    auto options = fruit_options();
    combo.open(options);

    require(
        combo.apply_filter("apple", options) == veyra::ComboResult::changed,
        "filtering to lowercase reports a change"
    );
    require(
        combo.match_count() == 1 && combo.contains(13),
        "a mid-string hit matches: substring, not prefix"
    );
    require(
        combo.active().has && combo.active().id == 13,
        "a fresh filter resets active to its first match"
    );

    require(
        combo.apply_filter("Apple", options) == veyra::ComboResult::changed &&
            combo.match_count() == 1 && combo.contains(11),
        "byte-exact case keeps Apple and Pineapple apart"
    );

    require(
        combo.apply_filter("apple", options) == veyra::ComboResult::changed,
        "returning to the earlier query reports a change again"
    );
    require(
        combo.apply_filter("apple", options) == veyra::ComboResult::handled,
        "repeating the same filter over the same options reports no change"
    );
}

void check_no_match_clears_active() {
    auto combo = veyra::ComboBoxController{};
    auto options = fruit_options();
    combo.open(options);

    require(
        combo.apply_filter("zzz", options) == veyra::ComboResult::changed,
        "a filter with zero hits still reports a change"
    );
    require(combo.match_count() == 0 && !combo.active().has, "no match clears the active id");
    require(
        combo.move_active(veyra::ComboMove::next) == veyra::ComboResult::ignored,
        "roving over zero matches is ignored"
    );
    require(
        combo.commit_active().result == veyra::ComboResult::ignored,
        "committing with no active id is ignored"
    );

    require(
        combo.apply_filter({}, options) == veyra::ComboResult::changed &&
            combo.match_count() == 4 && combo.active().id == 11,
        "clearing the query restores the full match set"
    );

    std::array<veyra::ComboOption, 3> with_zero{{
        {.id = 0, .label = "Apple"},
        {.id = 21, .label = "Apple"},
        {.id = 0, .label = "Anything"},
    }};
    auto zeroed = veyra::ComboBoxController{};
    require(
        zeroed.open(with_zero) == veyra::ComboResult::opened && zeroed.match_count() == 1 &&
            zeroed.contains(21),
        "zero ids never enter the match set"
    );
}

void check_roving_wraps_over_matches() {
    auto combo = veyra::ComboBoxController{};
    auto options = fruit_options();
    combo.open(options, "a");

    require(combo.match_count() == 2, "the filter narrows to two rows");

    require(
        combo.move_active(veyra::ComboMove::next) == veyra::ComboResult::changed &&
            combo.active().id == 13,
        "next moves within the match set, not the option list"
    );
    require(
        combo.move_active(veyra::ComboMove::next) == veyra::ComboResult::changed &&
            combo.active().id == 12,
        "next wraps from the last match to the first"
    );
    require(
        combo.move_active(veyra::ComboMove::prev) == veyra::ComboResult::changed &&
            combo.active().id == 13,
        "prev wraps from the first match to the last"
    );
    require(
        combo.move_active(veyra::ComboMove::first) == veyra::ComboResult::changed &&
            combo.active().id == 12,
        "first jumps to the head of the match set"
    );
    require(
        combo.move_active(veyra::ComboMove::last) == veyra::ComboResult::changed &&
            combo.active().id == 13,
        "last jumps to the tail of the match set"
    );

    combo.apply_filter("Cherry", options);
    require(
        combo.move_active(veyra::ComboMove::next) == veyra::ComboResult::handled &&
            combo.active().id == 14,
        "roving on a single match stays put"
    );

    auto closed = veyra::ComboBoxController{};
    require(
        closed.move_active(veyra::ComboMove::next) == veyra::ComboResult::ignored,
        "roving while closed is ignored"
    );
    require(
        closed.apply_filter("an", options) == veyra::ComboResult::ignored,
        "filtering while closed is ignored"
    );

    auto invalid = veyra::ComboBoxController{};
    require(invalid.open(options) == veyra::ComboResult::opened, "invalid move fixture opens");
    require(
        invalid.move_active(static_cast<veyra::ComboMove>(255)) == veyra::ComboResult::ignored &&
            invalid.active().id == 11,
        "an invalid movement is ignored without changing the active option"
    );
}

void check_commit_closes_and_replaces() {
    auto combo = veyra::ComboBoxController{};
    auto options = fruit_options();
    combo.open(options, "a");
    combo.move_active(veyra::ComboMove::next);

    veyra::ComboCommit first = combo.commit_active();
    require(
        first.result == veyra::ComboResult::commit && first.id == 13 && !first.free_text,
        "commit names the active id"
    );
    require(
        !combo.is_open() && combo.has_value() && combo.value() == 13,
        "commit closes the popup and plants the value"
    );
    require(
        combo.match_count() == 0 && !combo.active().has,
        "commit drops the transient match set"
    );
    require(
        combo.commit_active().result == veyra::ComboResult::ignored,
        "committing while closed is ignored"
    );

    combo.open(options);
    combo.move_active(veyra::ComboMove::last);
    veyra::ComboCommit second = combo.commit_active();
    require(
        second.result == veyra::ComboResult::commit && second.id == 14 && combo.value() == 14,
        "a second commit replaces the held value"
    );

    auto pointer = veyra::ComboBoxController{};
    pointer.open(options);
    veyra::ComboCommit picked = pointer.commit_id(12, options);
    require(
        picked.result == veyra::ComboResult::commit && picked.id == 12 && !pointer.is_open() &&
            pointer.value() == 12,
        "a pointer commit validates against current options and closes"
    );

    pointer.open(options);
    require(
        pointer.commit_id(999, options).result == veyra::ComboResult::ignored &&
            pointer.is_open() && pointer.value() == 12,
        "committing an unknown id is ignored with state untouched"
    );
    require(
        pointer.commit_id(0, options).result == veyra::ComboResult::ignored && pointer.is_open(),
        "committing a zero id is ignored"
    );
}

void check_close_and_dismiss_keep_value() {
    auto combo = veyra::ComboBoxController{};
    auto options = fruit_options();
    combo.open(options);
    combo.commit_active();
    require(combo.value() == 11, "a value is committed first");

    combo.open(options, "an");
    require(
        combo.close() == veyra::ComboResult::closed && !combo.is_open(),
        "close reports closed"
    );
    require(combo.has_value() && combo.value() == 11, "close keeps the committed value");
    require(
        combo.match_count() == 0 && !combo.active().has,
        "close drops the transient filter and match"
    );
    require(
        combo.close() == veyra::ComboResult::handled,
        "closing a closed popup reports no change"
    );

    combo.open(options, "an");
    require(
        combo.dismiss() == veyra::ComboResult::dismissed && !combo.is_open(),
        "dismiss reports dismissed and closes"
    );
    require(
        combo.has_value() && combo.value() == 11 && combo.match_count() == 0,
        "dismiss keeps the value and drops the transient state"
    );

    auto fresh = veyra::ComboBoxController{};
    require(
        fresh.dismiss() == veyra::ComboResult::handled,
        "dismissing a closed popup reports no change"
    );

    combo.open(options, "an");
    require(
        combo.clear_filter() == veyra::ComboResult::changed && combo.match_count() == 0 &&
            !combo.active().has && combo.is_open(),
        "clearing the filter drops matches but stays open"
    );
    require(
        combo.clear_filter() == veyra::ComboResult::handled,
        "repeating a clear reports no change"
    );
}

void check_strict_rejects_free_text() {
    auto strict = veyra::ComboBoxController{};
    auto options = fruit_options();
    strict.open(options);

    require(strict.mode() == veyra::ComboMode::strict, "strict is the default mode");
    require(
        strict.commit_text("Durian").result == veyra::ComboResult::ignored && strict.is_open(),
        "strict mode rejects a free-text commit"
    );

    require(strict.configure(veyra::ComboMode::free), "switching to free reports a change");
    require(!strict.configure(veyra::ComboMode::free), "repeating the mode reports no change");
    require(strict.is_open() && strict.match_count() == 4, "a mode switch keeps the match set");

    veyra::ComboCommit typed = strict.commit_text("Durian");
    require(
        typed.result == veyra::ComboResult::commit_text && typed.free_text && !strict.is_open(),
        "free mode reports the raw query as a text commit"
    );
    require(
        !strict.has_value(),
        "a free-text commit holds no option id, so the option value clears"
    );
    require(
        strict.commit_text({}).result == veyra::ComboResult::ignored,
        "an empty free-text commit is ignored"
    );

    strict.open(options);
    strict.commit_active();
    require(strict.has_value() && strict.value() == 11, "free mode still commits option ids");
    strict.open(options);
    veyra::ComboCommit replaced = strict.commit_text("Durian");
    require(
        replaced.result == veyra::ComboResult::commit_text && !strict.has_value(),
        "free text replaces a held option value"
    );

    require(strict.configure(veyra::ComboMode::strict), "switching back to strict works");
    strict.open(options);
    require(
        strict.commit_text("Durian").result == veyra::ComboResult::ignored,
        "strict rejects free text again after the switch back"
    );
}

void check_value_seeding_and_overflow() {
    auto combo = veyra::ComboBoxController{};
    require(!combo.has_value(), "a new combo holds no value");

    require(combo.set_value(42), "seeding a value reports a change");
    require(combo.has_value() && combo.value() == 42, "the seeded value reads back");
    require(!combo.set_value(42), "reseeding the same value reports no change");
    require(!combo.set_value(0), "a zero id is never a value");
    require(combo.clear_value(), "clearing reports a change");
    require(!combo.has_value(), "the value is gone after clearing");
    require(!combo.clear_value(), "repeating a clear reports no change");

    std::array<std::uint64_t, veyra::ComboBoxController::kCapacity + 2> ids{};
    for (std::size_t i = 0; i < ids.size(); ++i) {
        ids[i] = static_cast<std::uint64_t>(i + 1);
    }
    std::string labels;
    for (std::size_t i = 0; i < ids.size(); ++i) {
        labels += "x";
    }

    std::array<veyra::ComboOption, veyra::ComboBoxController::kCapacity + 2> flood{};
    for (std::size_t i = 0; i < flood.size(); ++i) {
        flood[i].id = ids[i];
        flood[i].label = std::string_view{labels.data() + i, 1};
    }

    auto crowded = veyra::ComboBoxController{};
    require(
        crowded.open(flood) == veyra::ComboResult::overflow && !crowded.is_open() &&
            crowded.match_count() == 0,
        "an over-capacity open overflows without opening"
    );

    auto options = fruit_options();
    crowded.open(options);
    const std::size_t held = crowded.match_count();
    require(
        crowded.apply_filter({}, flood) == veyra::ComboResult::overflow &&
            crowded.match_count() == held && crowded.active().id == 11,
        "an over-capacity filter is rejected with the old match intact"
    );

    std::array<std::uint64_t, 1> small{};
    require(
        crowded.copy_matches(small) == 1 && small[0] == 11,
        "a short buffer truncates to its own size"
    );
}

void check_disabled_rejects_mutation() {
    auto combo = veyra::ComboBoxController{};
    auto options = fruit_options();
    combo.open(options);
    combo.commit_active();

    require(combo.set_disabled(true), "disabling reports a change");
    require(
        combo.open(options) == veyra::ComboResult::ignored &&
            combo.apply_filter("an", options) == veyra::ComboResult::ignored &&
            combo.move_active(veyra::ComboMove::next) == veyra::ComboResult::ignored &&
            combo.commit_active().result == veyra::ComboResult::ignored &&
            combo.commit_id(12, options).result == veyra::ComboResult::ignored &&
            combo.commit_text("Durian").result == veyra::ComboResult::ignored &&
            combo.close() == veyra::ComboResult::ignored &&
            combo.dismiss() == veyra::ComboResult::ignored &&
            combo.clear_filter() == veyra::ComboResult::ignored,
        "every mutation is ignored while disabled"
    );
    require(
        combo.has_value() && combo.value() == 11,
        "rejected mutations leave the value untouched"
    );
    require(combo.state().disabled, "disabled state is observable");

    auto disabled_box = veyra::SemanticNodeData{};
    require(combo.write_box_semantics("Fruit", disabled_box), "disabled boxes still write");
    require(
        veyra::has_state(disabled_box.states, veyra::SemanticState::disabled) &&
            disabled_box.actions == 0,
        "a disabled box keeps its record but offers no actions"
    );

    require(combo.set_disabled(false), "enabling reports a change");
    require(
        combo.open(options) == veyra::ComboResult::opened,
        "mutation works again after enabling"
    );
}

void check_semantics_and_active_descendant() {
    auto combo = veyra::ComboBoxController{};
    auto options = fruit_options();
    combo.open(options);
    combo.commit_active();
    combo.open(options, "a");

    auto box = veyra::SemanticNodeData{
        .role = veyra::SemanticRole::text,
        .label = "unchanged",
    };
    require(!combo.write_box_semantics({}, box), "an empty box name is rejected");
    require(box.role == veyra::SemanticRole::text, "rejected box output stays unchanged");
    require(combo.write_box_semantics("Fruit", box), "a named box writes semantics");
    require(
        box.role == veyra::SemanticRole::combobox && box.label == "Fruit" &&
            veyra::has_state(box.states, veyra::SemanticState::expanded) &&
            veyra::has_state(box.states, veyra::SemanticState::selected) &&
            veyra::has_action(box.actions, veyra::SemanticAction::expand),
        "an open box with a value exposes expanded, selected, and expand"
    );

    combo.close();
    auto shut = veyra::SemanticNodeData{};
    require(combo.write_box_semantics("Fruit", shut), "a closed box writes too");
    require(
        !veyra::has_state(shut.states, veyra::SemanticState::expanded),
        "a closed box drops the expanded state"
    );

    auto list = veyra::SemanticNodeData{};
    require(!combo.write_list_semantics({}, list), "an empty list name is rejected");
    require(combo.write_list_semantics("Fruits", list), "a named popup list writes semantics");
    require(
        list.role == veyra::SemanticRole::listbox && list.label == "Fruits",
        "the popup exposes the listbox role"
    );

    combo.open(options, "an");
    auto item = veyra::SemanticNodeData{};
    require(!combo.write_item_semantics(12, {}, item), "an empty option name is rejected");
    require(!combo.write_item_semantics(0, "Zero", item), "a zero option id is rejected");
    require(combo.write_item_semantics(12, "Banana", item), "a named option writes semantics");
    require(
        item.role == veyra::SemanticRole::option && item.label == "Banana" &&
            veyra::has_state(item.states, veyra::SemanticState::focused) &&
            veyra::has_action(item.actions, veyra::SemanticAction::select),
        "the active match carries the focused state for active-descendant wiring"
    );
    require(
        combo.write_item_semantics(11, "Apple", item) &&
            veyra::has_state(item.states, veyra::SemanticState::selected) &&
            !veyra::has_state(item.states, veyra::SemanticState::focused),
        "the committed id carries selected while active sits elsewhere"
    );
    require(
        combo.write_item_semantics(14, "Cherry", item) &&
            !veyra::has_state(item.states, veyra::SemanticState::selected) &&
            !veyra::has_state(item.states, veyra::SemanticState::focused),
        "an unrelated id writes neither state"
    );
}

}

int main() {
    try {
        check_open_and_empty_query_matches_all();
        check_substring_case_sensitive_matching();
        check_no_match_clears_active();
        check_roving_wraps_over_matches();
        check_commit_closes_and_replaces();
        check_close_and_dismiss_keep_value();
        check_strict_rejects_free_text();
        check_value_seeding_and_overflow();
        check_disabled_rejects_mutation();
        check_semantics_and_active_descendant();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
