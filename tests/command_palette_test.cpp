#include "veyra/behaviors/navigation/command_palette.hpp"

#include <array>
#include <cstddef>
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

std::span<const char> chars_of(std::string_view text) {
    return {text.data(), text.size()};
}

struct PaletteFixture {
    std::array<veyra::PaletteItem, 4> items{{
        {.id = 11, .group_id = 2},
        {.id = 12, .group_id = 1},
        {.id = 13, .group_id = 2},
        {.id = 14, .group_id = 1},
    }};
    std::array<std::string_view, 4> titles{{"Apple", "Banana", "Apricot", "Cherry"}};
    std::array<std::string_view, 4> keywords{{"", "", "", ""}};
};

void seed_fixture(veyra::CommandPalette& palette, PaletteFixture& fixture) {
    require(
        palette.set_items(fixture.items) == veyra::PaletteResult::changed,
        "seeding the item store reports a change"
    );
    require(
        palette.apply_filter(fixture.titles, fixture.keywords) == veyra::PaletteResult::changed,
        "seeding the filter parks the first enabled row"
    );
}

void check_initial_state_reports_empty_but_ready() {
    auto palette = veyra::CommandPalette{};
    const veyra::PaletteState state = palette.state();

    require(
        state.items == 0 && state.matches == 0 && state.total == 0 && !state.overflow &&
            !state.has_active && state.active == 0 && state.generation == 0,
        "a new palette holds no items, no matches, and generation zero"
    );
    require(
        !state.expanded && !state.disabled && state.loop,
        "a new palette starts collapsed, enabled, and looping"
    );
    require(
        !palette.has_results() && !palette.active().has && palette.activate() == 0,
        "an empty palette arms nothing and commits nothing"
    );
    require(palette.query().empty(), "a new palette holds an empty borrowed query");
    require(!palette.set_loop(true), "repeating the default loop reports no change");
    require(palette.set_loop(false), "leaving loop mode reports a change");
    require(!palette.loops(), "loop mode is observable");
}

void check_empty_query_matches_all_in_group_order() {
    auto palette = veyra::CommandPalette{};
    auto fixture = PaletteFixture{};
    seed_fixture(palette, fixture);

    require(palette.match_count() == 4, "an empty query matches every stored item");
    require(palette.total_matches() == 4, "the total names every match");
    require(palette.has_results(), "a full match set reports results");

    std::array<std::uint64_t, 4> ids{};

    require(
        palette.copy_matches(ids) == 4 && ids[0] == 11 && ids[1] == 12 && ids[2] == 13 &&
            ids[3] == 14,
        "an empty query preserves registration order for header grouping"
    );

    std::array<std::uint64_t, 4> groups{};

    require(
        palette.copy_groups(groups) == 2 && groups[0] == 2 && groups[1] == 1,
        "groups surface in first-appearance order for header rendering"
    );
    require(
        palette.active().has && palette.active().id == 11,
        "an empty query parks active on the first row"
    );
}

void check_ranking_prefers_exact_word_then_subsequence() {
    auto palette = veyra::CommandPalette{};
    const std::array<veyra::PaletteItem, 4> items{{
        {.id = 1},
        {.id = 2},
        {.id = 3},
        {.id = 4},
    }};
    const std::array<std::string_view, 4> titles{{"save", "copy", "re-open view", "open file"}};
    const std::array<std::string_view, 4> keywords{{"", "", "", ""}};

    require(
        palette.set_items(items) == veyra::PaletteResult::changed,
        "the ranking fixture registers"
    );
    require(
        palette.set_query(chars_of("op")) == veyra::PaletteResult::changed,
        "the ranking query stores"
    );
    require(
        palette.apply_filter(titles, keywords) == veyra::PaletteResult::changed,
        "the ranking filter applies"
    );

    std::array<std::uint64_t, 4> ids{};

    require(palette.copy_matches(ids) == 3, "only the three matching rows survive");
    require(
        ids[0] == 4 && ids[1] == 3 && ids[2] == 2,
        "exact-prefix beats word-prefix beats subsequence regardless of registration order"
    );
    require(!palette.contains(1), "a non-matching row drops out of the window");
}

void check_ranking_ties_keep_item_order() {
    auto palette = veyra::CommandPalette{};
    const std::array<veyra::PaletteItem, 3> items{{
        {.id = 21},
        {.id = 22},
        {.id = 23},
    }};
    const std::array<std::string_view, 3> titles{{"go to file", "go to symbol", "cargo go"}};
    const std::array<std::string_view, 3> keywords{{"", "", ""}};

    palette.set_items(items);
    palette.set_query(chars_of("go"));
    palette.apply_filter(titles, keywords);

    std::array<std::uint64_t, 3> ids{};

    require(palette.copy_matches(ids) == 3, "every row matches the tie fixture");
    require(
        ids[0] == 21 && ids[1] == 22 && ids[2] == 23,
        "tied exact-prefix rows keep item order ahead of the word-prefix row"
    );
}

void check_keywords_rank_alongside_titles() {
    auto palette = veyra::CommandPalette{};
    const std::array<veyra::PaletteItem, 2> items{{
        {.id = 31},
        {.id = 32},
    }};
    const std::array<std::string_view, 2> titles{{"pretty print", "toggle panel"}};
    const std::array<std::string_view, 2> keywords{{"", "tty terminal"}};

    palette.set_items(items);
    palette.set_query(chars_of("tty"));
    require(
        palette.apply_filter(titles, keywords) == veyra::PaletteResult::changed,
        "a keyword hit applies"
    );

    std::array<std::uint64_t, 2> ids{};

    require(palette.copy_matches(ids) == 2, "both rows match the keyword fixture");
    require(ids[0] == 32 && ids[1] == 31, "a keyword exact-prefix outranks a title subsequence");
}

void check_pure_filter_fills_window_and_reports_overflow() {
    const std::array<veyra::PaletteItem, 5> items{{
        {.id = 41},
        {.id = 0},
        {.id = 42},
        {.id = 43},
        {.id = 44},
    }};
    const std::array<std::string_view, 5> titles{{"a1", "a-zero", "a2", "a3", "a4"}};
    const std::array<std::string_view, 5> keywords{{"", "", "", "", ""}};
    std::array<std::uint64_t, 2> window{};
    std::size_t count = 0;

    require(
        veyra::CommandPalette::filter(chars_of("a"), items, titles, keywords, window, count) ==
            veyra::PaletteResult::overflow,
        "a window smaller than the match set overflows"
    );
    require(count == 4, "the count still names the full match total");
    require(
        window[0] == 41 && window[1] == 42,
        "an overflowing pure filter fills rank order with what fits"
    );

    std::array<std::uint64_t, 8> wide{};
    std::size_t wide_count = 0;

    require(
        veyra::CommandPalette::filter(chars_of("a"), items, titles, keywords, wide, wide_count) ==
            veyra::PaletteResult::changed,
        "a wide window reports a change"
    );
    require(
        wide_count == 4 && wide[0] == 41 && wide[3] == 44,
        "a wide window holds every non-zero id in order"
    );

    std::array<std::uint64_t, 8> empty_hits{};
    std::size_t empty_count = 99;

    require(
        veyra::CommandPalette::filter(
            chars_of("zzz"),
            items,
            titles,
            keywords,
            empty_hits,
            empty_count
        ) == veyra::PaletteResult::changed &&
            empty_count == 0,
        "a pure filter with zero hits still resolves with a zero count"
    );

    const std::array<std::string_view, 2> short_titles{{"a1", "a2"}};
    std::size_t ignored_count = 99;

    require(
        veyra::CommandPalette::filter(
            chars_of("a"),
            items,
            short_titles,
            keywords,
            wide,
            ignored_count
        ) == veyra::PaletteResult::ignored &&
            ignored_count == 0,
        "mismatched title and keyword spans are ignored with a zeroed count"
    );
}

void check_loop_wraps_and_clamp_holds_edges() {
    auto palette = veyra::CommandPalette{};
    auto fixture = PaletteFixture{};
    seed_fixture(palette, fixture);

    require(palette.active().id == 11, "roving starts parked on the first row");
    require(
        palette.move_next() == veyra::PaletteResult::changed && palette.active().id == 12,
        "next steps within the window"
    );
    require(
        palette.move_last() == veyra::PaletteResult::changed && palette.active().id == 14,
        "last jumps to the tail of the window"
    );
    require(
        palette.move_next() == veyra::PaletteResult::changed && palette.active().id == 11,
        "next wraps from the tail to the head while looping"
    );
    require(
        palette.move_prev() == veyra::PaletteResult::changed && palette.active().id == 14,
        "prev wraps from the head to the tail while looping"
    );
    require(
        palette.move_first() == veyra::PaletteResult::changed && palette.active().id == 11,
        "first jumps to the head of the window"
    );
    require(
        palette.move_first() == veyra::PaletteResult::handled,
        "repeating first on the head row reports no change"
    );
    require(
        palette.move(static_cast<veyra::PaletteMove>(255)) == veyra::PaletteResult::ignored &&
            palette.active().id == 11,
        "an invalid direction is ignored with active untouched"
    );

    require(palette.set_loop(false), "clamp mode arms");
    require(palette.move_last() == veyra::PaletteResult::changed, "last still jumps while clamped");
    require(
        palette.move_next() == veyra::PaletteResult::handled && palette.active().id == 14,
        "next holds the tail row without wrapping while clamped"
    );
    require(
        palette.move_first() == veyra::PaletteResult::changed,
        "first still jumps while clamped"
    );
    require(
        palette.move_prev() == veyra::PaletteResult::handled && palette.active().id == 11,
        "prev holds the head row without wrapping while clamped"
    );
}

void check_disabled_items_list_but_never_arm() {
    auto palette = veyra::CommandPalette{};
    const std::array<veyra::PaletteItem, 3> items{{
        {.id = 51},
        {.id = 52, .disabled = true},
        {.id = 53},
    }};
    const std::array<std::string_view, 3> titles{{"Alpha", "Beta", "Gamma"}};
    const std::array<std::string_view, 3> keywords{{"", "", ""}};

    palette.set_items(items);
    palette.apply_filter(titles, keywords);

    require(
        palette.match_count() == 3 && palette.contains(52),
        "a disabled row stays listed in the window"
    );
    require(
        !palette.is_enabled(52) && palette.is_enabled(51) && !palette.is_enabled(99),
        "the enabled query separates listed-but-disabled from unknown ids"
    );
    require(
        palette.active().has && palette.active().id == 51,
        "filtering parks active on the first enabled row"
    );
    require(
        palette.move_next() == veyra::PaletteResult::changed && palette.active().id == 53,
        "next skips the disabled middle row"
    );
    require(
        palette.move_next() == veyra::PaletteResult::changed && palette.active().id == 51,
        "next wraps past the disabled row"
    );
    require(
        palette.move_prev() == veyra::PaletteResult::changed && palette.active().id == 53,
        "prev wraps past the disabled row"
    );
    require(
        palette.move_first() == veyra::PaletteResult::changed && palette.active().id == 51,
        "first lands on the first enabled row"
    );
    require(
        palette.move_last() == veyra::PaletteResult::changed && palette.active().id == 53,
        "last lands on the last enabled row"
    );
    require(palette.activate() == 53, "committing arms an enabled row");
}

void check_all_disabled_arms_nothing() {
    auto palette = veyra::CommandPalette{};
    const std::array<veyra::PaletteItem, 2> items{{
        {.id = 61, .disabled = true},
        {.id = 62, .disabled = true},
    }};
    const std::array<std::string_view, 2> titles{{"Alpha", "Beta"}};
    const std::array<std::string_view, 2> keywords{{"", ""}};

    palette.set_items(items);
    palette.apply_filter(titles, keywords);

    require(
        palette.match_count() == 2 && !palette.active().has,
        "disabled matches are listed but nothing is armed"
    );
    require(palette.has_results(), "listed rows still count as results");
    require(
        palette.move_next() == veyra::PaletteResult::ignored,
        "roving with no enabled match is ignored"
    );
    require(palette.activate() == 0, "committing with no enabled match reports invalid");

    const std::array<veyra::PaletteItem, 2> reopened{{
        {.id = 61},
        {.id = 62, .disabled = true},
    }};

    require(
        palette.set_items(reopened) == veyra::PaletteResult::changed,
        "re-enabling a row replaces the store"
    );
    require(
        palette.apply_filter(titles, keywords) == veyra::PaletteResult::changed &&
            palette.active().id == 61,
        "re-enabling a row re-arms active on it"
    );
    require(palette.activate() == 61, "the re-armed row commits");
}

void check_empty_states_arm_nothing() {
    auto palette = veyra::CommandPalette{};
    auto fixture = PaletteFixture{};
    seed_fixture(palette, fixture);

    require(
        palette.set_query(chars_of("zzz")) == veyra::PaletteResult::changed,
        "a narrowing query stores"
    );
    require(
        palette.apply_filter(fixture.titles, fixture.keywords) == veyra::PaletteResult::changed,
        "filtering everything out reports a change"
    );
    require(
        palette.match_count() == 0 && palette.total_matches() == 0 && !palette.has_results() &&
            !palette.active().has,
        "zero hits leave an empty window with no armed row"
    );
    require(
        palette.move_next() == veyra::PaletteResult::ignored &&
            palette.move_first() == veyra::PaletteResult::ignored,
        "roving an empty window is ignored"
    );
    require(palette.activate() == 0, "committing an empty window reports invalid");

    std::array<std::uint64_t, 2> ids{};

    require(
        palette.copy_matches(ids) == 0 && palette.copy_groups(ids) == 0,
        "copying an empty window reports zero rows"
    );

    auto bare = veyra::CommandPalette{};
    const std::array<std::string_view, 0> no_titles{};
    const std::array<std::string_view, 0> no_keywords{};

    require(
        bare.apply_filter(no_titles, no_keywords) == veyra::PaletteResult::handled &&
            !bare.has_results(),
        "filtering an empty store over empty spans reports no change"
    );
}

void check_stateful_window_overflow_keeps_usable_prefix() {
    auto palette = veyra::CommandPalette{};
    std::array<veyra::PaletteItem, 300> items{};
    std::array<std::string, 300> storage{};
    std::array<std::string_view, 300> titles{};
    std::array<std::string_view, 300> keywords{};

    for (std::size_t i = 0; i < items.size(); ++i) {
        items[i].id = static_cast<std::uint64_t>(1000 + i);
        items[i].group_id = i % 7;
        storage[i] = "command " + std::to_string(i);
        titles[i] = storage[i];
        keywords[i] = "";
    }

    require(
        palette.set_items(items) == veyra::PaletteResult::changed,
        "three hundred rows fit the item store"
    );
    require(
        palette.apply_filter(titles, keywords) == veyra::PaletteResult::overflow,
        "a match set past the window reports overflow"
    );
    require(
        palette.match_count() == veyra::CommandPalette::kResultCapacity,
        "an overflowing window fills to capacity"
    );
    require(
        palette.total_matches() == 300 && palette.overflowed(),
        "the total still names every match behind the window"
    );
    require(
        palette.active().has && palette.active().id == 1000,
        "an overflowing window still parks active on its head"
    );
    require(
        palette.move_next() == veyra::PaletteResult::changed && palette.active().id == 1001,
        "an overflowing window still roves"
    );
    require(palette.activate() == 1001, "an overflowing window still commits");

    std::array<std::uint64_t, 8> groups{};
    const std::size_t copied = palette.copy_groups(groups);

    require(
        copied == 7 && groups[0] == 0 && groups[6] == 6,
        "group headers span the stored group cycle in first-appearance order"
    );
}

void check_item_store_overflow_rejects_without_touching() {
    auto palette = veyra::CommandPalette{};
    auto fixture = PaletteFixture{};
    seed_fixture(palette, fixture);

    std::array<veyra::PaletteItem, veyra::CommandPalette::kItemCapacity + 1> flood{};

    for (std::size_t i = 0; i < flood.size(); ++i) {
        flood[i].id = static_cast<std::uint64_t>(9000 + i);
    }

    require(
        palette.set_items(flood) == veyra::PaletteResult::overflow,
        "one row past item capacity overflows"
    );
    require(
        palette.item_count() == 4 && palette.match_count() == 4 && palette.active().id == 11,
        "a rejected store keeps the old items, window, and armed row"
    );
    require(
        palette.set_items(fixture.items) == veyra::PaletteResult::changed &&
            palette.match_count() == 0 && !palette.active().has,
        "re-registering the same store resets the window for fresh titles"
    );
}

void check_borrowed_query_span_tracks_caller_storage_until_replaced() {
    auto palette = veyra::CommandPalette{};
    std::string storage = "open";

    require(
        palette.set_query(chars_of(storage)) == veyra::PaletteResult::changed,
        "storing a caller-owned query reports a change"
    );
    require(
        palette.query().data() == storage.data() && palette.query().size() == storage.size(),
        "the query borrows caller storage instead of copying it"
    );
    require(palette.generation() == 1, "a fresh query bumps the generation once");
    require(
        palette.set_query(chars_of(storage)) == veyra::PaletteResult::handled &&
            palette.generation() == 1,
        "repeating the same query bytes reports no change without a bump"
    );

    std::string next = "open file";

    require(
        palette.set_query(chars_of(next)) == veyra::PaletteResult::changed &&
            palette.generation() == 2,
        "replacing the query swaps the borrow and bumps again"
    );
    require(
        palette.query().data() == next.data(),
        "the borrowed span now tracks the replacement storage"
    );
    require(
        palette.invalidate() == 3 && palette.generation() == 3 && palette.invalidate() == 4,
        "invalidation bumps the generation without touching the query"
    );
}

void check_activate_commits_the_armed_row() {
    auto palette = veyra::CommandPalette{};
    auto fixture = PaletteFixture{};
    seed_fixture(palette, fixture);

    require(palette.activate() == 11, "committing parks on the first row by default");
    require(
        palette.move_next() == veyra::PaletteResult::changed,
        "roving steps off the parked row"
    );
    require(palette.activate() == 12, "committing names the roved-to row");

    auto pointer = veyra::CommandPalette{};
    seed_fixture(pointer, fixture);
    pointer.move_last();

    require(pointer.activate() == 14, "a tail-row commit names the tail id");
    require(
        pointer.active().has && pointer.active().id == 14,
        "committing leaves the armed row parked for the caller menu"
    );
}

void check_disabled_vetoes_every_mutation() {
    auto palette = veyra::CommandPalette{};
    auto fixture = PaletteFixture{};
    seed_fixture(palette, fixture);
    palette.set_query(chars_of("a"));
    const std::uint64_t parked_generation = palette.generation();

    require(palette.set_enabled(false), "disabling reports a change");
    require(palette.disabled(), "disabled state is observable");
    require(palette.state().disabled, "disabled state surfaces through the snapshot");

    require(
        palette.set_query(chars_of("b")) == veyra::PaletteResult::ignored &&
            palette.set_items(fixture.items) == veyra::PaletteResult::ignored &&
            palette.apply_filter(fixture.titles, fixture.keywords) ==
                veyra::PaletteResult::ignored &&
            palette.move_next() == veyra::PaletteResult::ignored &&
            palette.move_prev() == veyra::PaletteResult::ignored &&
            palette.move_first() == veyra::PaletteResult::ignored &&
            palette.move_last() == veyra::PaletteResult::ignored,
        "every mutation is ignored while disabled"
    );
    require(
        !palette.set_expanded(true) && !palette.expanded(),
        "expansion is refused while disabled"
    );
    require(palette.activate() == 0, "committing while disabled reports invalid");
    require(
        palette.invalidate() == parked_generation && palette.generation() == parked_generation,
        "invalidation holds the generation while disabled"
    );
    require(
        palette.match_count() == 4 && palette.active().id == 11,
        "rejected mutations leave the window and armed row untouched"
    );

    auto box = veyra::SemanticNodeData{};
    require(palette.write_box_semantics("Palette", box), "a disabled palette still writes");
    require(
        veyra::has_state(box.states, veyra::SemanticState::disabled) && box.actions == 0,
        "a disabled box keeps its record but offers no actions"
    );

    require(!palette.set_enabled(false), "repeating the disable reports no change");
    require(palette.set_enabled(true), "enabling reports a change");
    require(
        palette.move_next() == veyra::PaletteResult::changed,
        "mutation works again after enabling"
    );
    require(palette.activate() == 12, "committing works again after enabling");
}

void check_semantics_expose_expanded_and_focused() {
    auto palette = veyra::CommandPalette{};
    auto fixture = PaletteFixture{};
    seed_fixture(palette, fixture);

    auto box = veyra::SemanticNodeData{
        .role = veyra::SemanticRole::text,
        .label = "unchanged",
    };

    require(!palette.write_box_semantics({}, box), "an empty box name is rejected");
    require(box.role == veyra::SemanticRole::text, "rejected box output stays unchanged");
    require(palette.write_box_semantics("Palette", box), "a named box writes semantics");
    require(
        box.role == veyra::SemanticRole::combobox && box.label == "Palette" &&
            !veyra::has_state(box.states, veyra::SemanticState::expanded) &&
            veyra::has_action(box.actions, veyra::SemanticAction::expand),
        "a collapsed box drops the expanded state but keeps the expand action"
    );

    require(palette.set_expanded(true), "expansion arms");
    require(palette.write_box_semantics("Palette", box), "an expanded box writes too");
    require(
        veyra::has_state(box.states, veyra::SemanticState::expanded),
        "an expanded box carries the expanded state"
    );

    auto item = veyra::SemanticNodeData{};
    require(!palette.write_item_semantics(11, {}, item), "an empty item name is rejected");
    require(!palette.write_item_semantics(0, "Zero", item), "a zero item id is rejected");
    require(palette.write_item_semantics(11, "Apple", item), "a named item writes semantics");
    require(
        item.role == veyra::SemanticRole::option && item.label == "Apple" &&
            veyra::has_state(item.states, veyra::SemanticState::focused) &&
            veyra::has_action(item.actions, veyra::SemanticAction::select),
        "the armed row carries the focused state for active-descendant wiring"
    );
    require(
        palette.write_item_semantics(12, "Banana", item) &&
            !veyra::has_state(item.states, veyra::SemanticState::focused),
        "an unarmed row writes without the focused state"
    );
}

void check_disabled_item_semantics() {
    auto palette = veyra::CommandPalette{};
    const std::array<veyra::PaletteItem, 2> items{{
        {.id = 71},
        {.id = 72, .disabled = true},
    }};
    const std::array<std::string_view, 2> titles{{"Alpha", "Beta"}};
    const std::array<std::string_view, 2> keywords{{"", ""}};

    palette.set_items(items);
    palette.apply_filter(titles, keywords);

    auto item = veyra::SemanticNodeData{};
    require(palette.write_item_semantics(72, "Beta", item), "a disabled row writes semantics");
    require(
        veyra::has_state(item.states, veyra::SemanticState::disabled) &&
            !veyra::has_action(item.actions, veyra::SemanticAction::select),
        "a disabled row carries the disabled state and no select action"
    );
    require(
        palette.write_item_semantics(71, "Alpha", item) &&
            !veyra::has_state(item.states, veyra::SemanticState::disabled) &&
            veyra::has_action(item.actions, veyra::SemanticAction::select),
        "an enabled row keeps the select action"
    );
}

}

int main() {
    try {
        check_initial_state_reports_empty_but_ready();
        check_empty_query_matches_all_in_group_order();
        check_ranking_prefers_exact_word_then_subsequence();
        check_ranking_ties_keep_item_order();
        check_keywords_rank_alongside_titles();
        check_pure_filter_fills_window_and_reports_overflow();
        check_loop_wraps_and_clamp_holds_edges();
        check_disabled_items_list_but_never_arm();
        check_all_disabled_arms_nothing();
        check_empty_states_arm_nothing();
        check_stateful_window_overflow_keeps_usable_prefix();
        check_item_store_overflow_rejects_without_touching();
        check_borrowed_query_span_tracks_caller_storage_until_replaced();
        check_activate_commits_the_armed_row();
        check_disabled_vetoes_every_mutation();
        check_semantics_expose_expanded_and_focused();
        check_disabled_item_semantics();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
