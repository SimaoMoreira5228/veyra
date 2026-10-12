#include "veyra/behaviors/selection_typeahead.hpp"

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

struct Fixture {
    std::array<std::uint64_t, 6> ids{11, 12, 13, 14, 15, 16};
    std::array<std::string_view, 6> labels{
        "apple",
        "apricot",
        "banana",
        "blueberry",
        "blackberry",
        "cherry",
    };
    veyra::SelectionController selection{};
    veyra::SelectionTypeahead typeahead{};
};

void check_prefix_accumulates_and_jumps_to_first_match() {
    auto fixed = Fixture{};

    require(
        fixed.typeahead.advance('a', 100, fixed.selection, fixed.ids, fixed.labels) ==
            veyra::SelectionResult::changed,
        "first key jumps to the first match"
    );
    require(fixed.selection.contains(11), "the first apple-prefix id is selected");
    require(fixed.typeahead.prefix_length() == 1, "one key keeps a one-byte prefix");

    require(
        fixed.typeahead.advance('p', 200, fixed.selection, fixed.ids, fixed.labels) ==
            veyra::SelectionResult::handled,
        "extending the prefix to an already-held id reports no change"
    );
    require(
        fixed.selection.contains(11) && fixed.selection.count() == 1,
        "the selection stays on the first prefix match"
    );

    std::array<char, 4> held{};
    require(fixed.typeahead.copy_prefix(held) == 2, "the prefix copies into a caller buffer");
    require(held[0] == 'a' && held[1] == 'p', "the copied prefix keeps both keystrokes");
}

void check_timeout_expiry_resets_the_prefix() {
    auto fixed = Fixture{};

    require(
        fixed.typeahead.advance('a', 100, fixed.selection, fixed.ids, fixed.labels) ==
            veyra::SelectionResult::changed,
        "the opening key selects"
    );
    require(
        fixed.typeahead.advance('p', 100 + 500, fixed.selection, fixed.ids, fixed.labels) ==
            veyra::SelectionResult::handled,
        "a key exactly at the timeout still extends the prefix"
    );
    require(fixed.selection.contains(11), "apple still matches the extended prefix");

    require(
        fixed.typeahead.advance('b', 100 + 500 + 501, fixed.selection, fixed.ids, fixed.labels) ==
            veyra::SelectionResult::changed,
        "a key past the timeout starts a fresh prefix"
    );
    require(
        fixed.selection.contains(13) && fixed.typeahead.prefix_length() == 1,
        "the fresh prefix jumps to the first b label"
    );
    require(fixed.typeahead.now_ms() == 100 + 500 + 501, "the clock tracks the latest keystroke");
}

void check_repeated_character_cycles_through_matches() {
    auto fixed = Fixture{};

    require(
        fixed.typeahead.advance('b', 0, fixed.selection, fixed.ids, fixed.labels) ==
                veyra::SelectionResult::changed &&
            fixed.selection.contains(13),
        "the first b selects banana"
    );
    require(
        fixed.typeahead.advance('b', 100, fixed.selection, fixed.ids, fixed.labels) ==
                veyra::SelectionResult::changed &&
            fixed.selection.contains(14),
        "the second b moves to blueberry"
    );
    require(
        fixed.typeahead.advance('b', 200, fixed.selection, fixed.ids, fixed.labels) ==
                veyra::SelectionResult::changed &&
            fixed.selection.contains(15),
        "the third b moves to blackberry"
    );
    require(
        fixed.typeahead.advance('b', 300, fixed.selection, fixed.ids, fixed.labels) ==
                veyra::SelectionResult::changed &&
            fixed.selection.contains(13),
        "cycling wraps past the last match to the first"
    );

    auto solo = Fixture{};
    std::array<std::uint64_t, 1> one_id{21};
    std::array<std::string_view, 1> one_label{"cherry"};
    require(
        solo.typeahead.advance('c', 0, solo.selection, one_id, one_label) ==
            veyra::SelectionResult::changed,
        "a lone match selects"
    );
    require(
        solo.typeahead.advance('c', 100, solo.selection, one_id, one_label) ==
                veyra::SelectionResult::handled &&
            solo.selection.contains(21),
        "cycling a single match reports no change"
    );
}

void check_no_match_and_empty_leave_selection_alone() {
    auto fixed = Fixture{};

    require(
        fixed.typeahead.advance('a', 0, fixed.selection, fixed.ids, fixed.labels) ==
            veyra::SelectionResult::changed,
        "the opening key selects"
    );
    require(
        fixed.typeahead.advance('z', 100, fixed.selection, fixed.ids, fixed.labels) ==
            veyra::SelectionResult::handled,
        "a dead extension reports no change"
    );
    require(
        fixed.selection.contains(11) && fixed.selection.count() == 1,
        "a miss keeps the old selection"
    );
    require(fixed.typeahead.prefix_length() == 2, "a miss keeps the prefix until the timeout");

    auto fresh = Fixture{};
    require(
        fresh.typeahead.advance('z', 0, fresh.selection, fresh.ids, fresh.labels) ==
                veyra::SelectionResult::handled &&
            fresh.selection.empty(),
        "a first key with no match selects nothing"
    );

    std::span<const std::uint64_t> no_ids{};
    std::span<const std::string_view> no_labels{};
    require(
        fresh.typeahead.advance('a', 100, fresh.selection, no_ids, no_labels) ==
                veyra::SelectionResult::handled &&
            fresh.selection.empty(),
        "empty spans report no change"
    );

    std::array<std::uint64_t, 2> short_ids{1, 2};
    std::array<std::string_view, 1> short_labels{"apple"};
    require(
        fresh.typeahead.advance('a', 200, fresh.selection, short_ids, short_labels) ==
            veyra::SelectionResult::ignored,
        "mismatched spans are rejected"
    );
}

void check_matching_is_byte_exact() {
    auto fixed = Fixture{};

    require(
        fixed.typeahead.advance('A', 0, fixed.selection, fixed.ids, fixed.labels) ==
                veyra::SelectionResult::handled &&
            fixed.selection.empty(),
        "an uppercase key does not match lowercase labels"
    );
}

void check_disabled_and_reconfigure() {
    auto fixed = Fixture{};

    require(fixed.selection.set_disabled(true), "disabling reports a state change");
    require(
        fixed.typeahead.advance('a', 0, fixed.selection, fixed.ids, fixed.labels) ==
                veyra::SelectionResult::ignored &&
            fixed.selection.empty(),
        "typing while disabled is rejected"
    );
    require(fixed.typeahead.prefix_length() == 0, "rejected input leaves the prefix untouched");
    require(fixed.selection.set_disabled(false), "enabling reports a state change");
    require(
        fixed.typeahead.advance('a', 0, fixed.selection, fixed.ids, fixed.labels) ==
                veyra::SelectionResult::changed &&
            fixed.selection.contains(11),
        "typing works again after enabling"
    );

    require(fixed.typeahead.configure(50), "a new timeout reports a change");
    require(fixed.typeahead.timeout_ms() == 50, "the new timeout is retained");
    require(!fixed.typeahead.configure(50), "repeating the same timeout reports no change");
    require(fixed.typeahead.prefix_length() == 0, "reconfiguring clears the prefix");
    fixed.typeahead.reset();
    require(fixed.typeahead.prefix_length() == 0, "reset clears the prefix");
}

void check_full_prefix_restarts_from_the_new_key() {
    auto fixed = Fixture{};

    std::array<std::uint64_t, 1> one_id{31};
    std::array<std::string_view, 1> one_label{"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"};

    for (std::size_t i = 0; i < veyra::SelectionTypeahead::kPrefixCapacity; ++i) {
        fixed.typeahead.advance('a', i, fixed.selection, one_id, one_label);
    }
    require(
        fixed.typeahead.prefix_length() == veyra::SelectionTypeahead::kPrefixCapacity,
        "the prefix fills to capacity"
    );
    require(
        fixed.typeahead.advance('b', 1'000, fixed.selection, fixed.ids, fixed.labels) ==
            veyra::SelectionResult::changed,
        "one more key restarts instead of wedging"
    );
    require(
        fixed.typeahead.prefix_length() == 1 && fixed.selection.contains(13),
        "the restart jumps to the new key's first match"
    );
}

}

int main() {
    try {
        check_prefix_accumulates_and_jumps_to_first_match();
        check_timeout_expiry_resets_the_prefix();
        check_repeated_character_cycles_through_matches();
        check_no_match_and_empty_leave_selection_alone();
        check_matching_is_byte_exact();
        check_disabled_and_reconfigure();
        check_full_prefix_restarts_from_the_new_key();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
