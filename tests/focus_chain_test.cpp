#include "veyra/behaviors/navigation/focus_chain.hpp"

#include <cstddef>
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

void check_empty_chain_reports_no_focus() {
    auto chain = veyra::FocusChain{};

    require(chain.empty(), "a new chain holds nothing");
    require(chain.count() == 0, "a new chain counts zero items");
    require(chain.focused() == 0, "a new chain focuses nothing");
    require(!chain.has_focus(), "a new chain reports no focus");
    require(chain.generation() == 0, "a new chain starts at generation zero");
    require(!chain.contains(11), "an empty chain contains nothing");
    require(!chain.is_enabled(11), "an empty chain enables nothing");

    require(!chain.focus(11), "focusing an unknown id reports no change");
    require(!chain.unregister(11), "unregistering an unknown id reports no change");
    require(!chain.set_enabled(11, false), "toggling an unknown id reports no change");
    require(!chain.focus_next(), "next on an empty chain reports no change");
    require(!chain.focus_prev(), "prev on an empty chain reports no change");
    require(!chain.focus_first(), "first on an empty chain reports no change");
    require(!chain.focus_last(), "last on an empty chain reports no change");
    require(chain.generation() == 0, "rejected verbs leave the generation alone");
}

void check_zero_id_rejected_everywhere() {
    auto chain = veyra::FocusChain{};
    require(chain.register_item(11), "a plain id registers");

    require(!chain.register_item(0), "zero never registers");
    require(!chain.unregister(0), "zero never unregisters");
    require(!chain.focus(0), "zero never focuses");
    require(!chain.set_enabled(0, false), "zero never toggles");
    require(chain.count() == 1, "rejected zero ids store nothing");
    require(chain.generation() == 1, "rejected zero ids bump nothing");
}

void check_registration_order_and_wrap() {
    auto chain = veyra::FocusChain{};
    require(chain.register_item(11), "first item registers");
    require(chain.register_item(12), "second item registers");
    require(chain.register_item(13), "third item registers");
    require(chain.register_item(14), "fourth item registers");
    require(!chain.has_focus(), "registering never parks focus by itself");

    require(chain.focus_first(), "first jumps to the head");
    require(chain.focused() == 11, "first lands on the registration head");
    require(!chain.focus_first(), "repeating first reports no change");
    require(chain.focus_next() && chain.focused() == 12, "next steps forward");
    require(chain.focus_next() && chain.focused() == 13, "next steps forward again");
    require(chain.focus_next() && chain.focused() == 14, "next reaches the tail");
    require(chain.focus_next() && chain.focused() == 11, "next wraps past the tail");
    require(chain.focus_prev() && chain.focused() == 14, "prev wraps past the head");
    require(chain.focus_prev() && chain.focused() == 13, "prev steps back");
    require(chain.focus_last() && chain.focused() == 14, "last jumps to the tail");
    require(!chain.focus_last(), "repeating last reports no change");
    require(chain.focus(12) && chain.focused() == 12, "direct focus moves");
    require(!chain.focus(12), "repeating the focused id reports no change");
    require(!chain.focus(99), "focusing outside the chain reports no change");
}

void check_disabled_items_are_skipped() {
    auto chain = veyra::FocusChain{};
    chain.register_item(21);
    chain.register_item(22);
    chain.register_item(23);
    chain.register_item(24);
    require(chain.set_enabled(23, false), "disabling a middle id reports a change");
    require(!chain.is_enabled(23) && chain.is_enabled(22), "the enabled query names the state");
    require(!chain.set_enabled(23, false), "repeating the disable reports no change");

    require(!chain.focus(23), "focusing a disabled id is ignored");
    require(chain.focus_first() && chain.focused() == 21, "first skips no head id here");
    require(chain.focus_next() && chain.focused() == 22, "next reaches the id before the gap");
    require(chain.focus_next() && chain.focused() == 24, "next skips the disabled middle id");
    require(chain.focus_next() && chain.focused() == 21, "next wraps past the gap");
    require(chain.focus_prev() && chain.focused() == 24, "prev wraps to the enabled tail");
    require(chain.focus_prev() && chain.focused() == 22, "prev skips the disabled id backward");
    require(chain.focus_last() && chain.focused() == 24, "last lands on the enabled tail");

    auto solo = veyra::FocusChain{};
    solo.register_item(31);
    solo.register_item(32, false);
    solo.focus_first();
    require(!solo.focus_next() && solo.focused() == 31, "next holds with one enabled id");
    require(!solo.focus_prev() && solo.focused() == 31, "prev holds with one enabled id");
}

void check_all_disabled_clears_focus() {
    auto chain = veyra::FocusChain{};
    chain.register_item(41);
    chain.register_item(42);
    chain.focus_first();
    require(chain.focused() == 41, "focus parks on the head");

    require(chain.set_enabled(41, false), "disabling the focused id retargets");
    require(chain.focused() == 42, "the next enabled id takes over");
    require(chain.set_enabled(42, false), "disabling the last enabled id clears");
    require(chain.focused() == 0 && !chain.has_focus(), "no enabled id leaves no focus");
    require(!chain.focus_next(), "roving with no enabled id reports no change");
    require(!chain.focus_first() && !chain.focus_last(), "jumps with no enabled id fail");

    require(chain.set_enabled(41, true), "re-enabling reports a change");
    require(chain.focused() == 0, "enabling never steals focus back by itself");
    require(chain.focus_first() && chain.focused() == 41, "an explicit verb re-parks focus");
}

void check_duplicate_registration_gating() {
    auto chain = veyra::FocusChain{};
    require(chain.register_item(51), "first registration reports a change");
    const std::uint64_t parked = chain.generation();

    require(!chain.register_item(51), "repeating the same pair reports no change");
    require(chain.generation() == parked, "a duplicate leaves the generation alone");
    require(chain.count() == 1, "a duplicate stores nothing new");

    require(chain.register_item(51, false), "flipping the enabled bit reports a change");
    require(chain.generation() == parked + 1, "the flip bumps the generation once");
    require(!chain.is_enabled(51), "the flip lands on the stored entry");
    require(chain.count() == 1, "the flip stores nothing new");
}

void check_overflow_rejects_without_touching() {
    auto chain = veyra::FocusChain{};

    for (std::uint64_t id = 1; id <= veyra::FocusChain::kCapacity; ++id) {
        require(chain.register_item(1000 + id), "filling to capacity registers");
    }
    require(chain.count() == veyra::FocusChain::kCapacity, "the chain fills exactly to capacity");
    const std::uint64_t parked = chain.generation();

    require(!chain.register_item(9999), "one id past capacity is rejected");
    require(chain.count() == veyra::FocusChain::kCapacity, "overflow stores nothing");
    require(!chain.contains(9999), "the spilled id is absent");
    require(chain.generation() == parked, "overflow bumps nothing");

    require(chain.focus_first() && chain.focused() == 1001, "a full chain still focuses");
    require(
        chain.focus_last() && chain.focused() == 1000 + veyra::FocusChain::kCapacity,
        "a full chain still reaches its tail"
    );
}

void check_unregister_fallback() {
    auto chain = veyra::FocusChain{};
    chain.register_item(61);
    chain.register_item(62);
    chain.register_item(63);
    require(chain.focus(62), "focus parks in the middle");

    require(chain.unregister(62), "dropping the focused id reports a change");
    require(chain.focused() == 63, "the successor takes over");
    require(chain.count() == 2 && !chain.contains(62), "the dropped id is gone");

    require(chain.unregister(63), "dropping the focused tail reports a change");
    require(chain.focused() == 61, "the predecessor takes over at the end");

    require(chain.unregister(61), "dropping the only id reports a change");
    require(chain.focused() == 0 && !chain.has_focus(), "the last drop clears focus");
    require(chain.empty(), "dropping every id empties the chain");

    auto kept = veyra::FocusChain{};
    kept.register_item(71);
    kept.register_item(72);
    kept.register_item(73);
    kept.focus(71);
    require(kept.unregister(73), "dropping an unfocused id reports a change");
    require(kept.focused() == 71, "dropping elsewhere leaves focus alone");
    require(!kept.unregister(99), "dropping an unknown id reports no change");
}

void check_unregister_skips_disabled_fallback() {
    auto chain = veyra::FocusChain{};
    chain.register_item(81);
    chain.register_item(82);
    chain.register_item(83, false);
    require(chain.focus(82), "focus parks on the middle id");

    require(chain.unregister(82), "dropping the focused middle id reports a change");
    require(chain.focused() == 81, "a disabled successor is skipped for the predecessor");

    auto forward = veyra::FocusChain{};
    forward.register_item(91, false);
    forward.register_item(92);
    forward.register_item(93);
    require(forward.focus(92), "focus parks on the middle id again");

    require(forward.unregister(92), "dropping the focused id reports a change");
    require(forward.focused() == 93, "an enabled successor wins over the predecessor");
}

void check_generation_gating() {
    auto chain = veyra::FocusChain{};
    require(chain.generation() == 0, "generation starts at zero");

    require(chain.register_item(101), "a registration bumps");
    require(chain.generation() == 1, "one change means one bump");

    require(!chain.register_item(101), "a duplicate bumps nothing");
    require(chain.focus_first(), "parking focus bumps");
    require(chain.generation() == 2, "focus motion counts");
    require(!chain.focus_first(), "a no-op jump bumps nothing");
    require(!chain.focus(101), "refocusing the same id bumps nothing");
    require(!chain.set_enabled(101, true), "an unchanged enabled bit bumps nothing");
    require(chain.generation() == 2, "every no-op holds the generation");

    require(chain.register_item(102), "a second registration bumps");
    require(chain.focus_next(), "stepping bumps");
    require(chain.set_enabled(102, false), "an enabled flip bumps");
    require(chain.unregister(102), "an unregister bumps");
    require(chain.generation() == 6, "each real change bumps exactly once");
}

}

int main() {
    try {
        check_empty_chain_reports_no_focus();
        check_zero_id_rejected_everywhere();
        check_registration_order_and_wrap();
        check_disabled_items_are_skipped();
        check_all_disabled_clears_focus();
        check_duplicate_registration_gating();
        check_overflow_rejects_without_touching();
        check_unregister_fallback();
        check_unregister_skips_disabled_fallback();
        check_generation_gating();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
