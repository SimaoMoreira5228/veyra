#include "veyra/core/shortcut_map.hpp"
#include "veyra/core/shortcut_sequence.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

constexpr std::uint32_t kCtrl = veyra::modifier_flag(veyra::ShortcutModifier::ctrl);
constexpr std::uint32_t kAlt = veyra::modifier_flag(veyra::ShortcutModifier::alt);
constexpr std::uint32_t kShift = veyra::modifier_flag(veyra::ShortcutModifier::shift);
constexpr std::uint32_t kSuper = veyra::modifier_flag(veyra::ShortcutModifier::super);

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

void check_bind_dispatch_miss() {
    auto map = veyra::ShortcutMap{};
    require(map.empty(), "a new map holds nothing");
    require(map.count() == 0, "a new map holds zero bindings");

    const veyra::ShortcutChord copy{11, kCtrl};
    const veyra::ShortcutBind bound = map.bind(copy, 100);
    require(bound.result == veyra::ShortcutResult::bound, "binding a fresh chord reports bound");
    require(bound.occupier == 0, "a fresh bind names no occupier");
    require(map.count() == 1 && !map.empty(), "a bound chord is stored");
    require(map.contains(copy), "contains finds the bound chord");

    const veyra::ShortcutHit hit = map.dispatch(copy);
    require(hit.has && hit.command == 100, "dispatch returns the bound command");

    const veyra::ShortcutHit miss = map.dispatch({12, kCtrl});
    require(!miss.has && miss.command == 0, "an unbound chord misses");

    const veyra::ShortcutHit bare_miss = map.dispatch({11, 0});
    require(!bare_miss.has, "the same key without modifiers misses");
}

void check_conflict_names_occupier() {
    auto map = veyra::ShortcutMap{};
    map.bind({11, kCtrl}, 100);

    const veyra::ShortcutBind clash = map.bind({11, kCtrl}, 200);
    require(clash.result == veyra::ShortcutResult::conflict, "a taken chord reports conflict");
    require(clash.occupier == 100, "the conflict names the occupying command");

    const veyra::ShortcutHit kept = map.dispatch({11, kCtrl});
    require(kept.has && kept.command == 100, "a conflict leaves the old binding in place");
    require(map.count() == 1, "a conflict stores nothing new");

    require(map.unbind({11, kCtrl}), "explicit unbind drops the occupier");
    const veyra::ShortcutBind retake = map.bind({11, kCtrl}, 200);
    require(retake.result == veyra::ShortcutResult::bound, "the chord is reusable after unbind");
    require(map.dispatch({11, kCtrl}).command == 200, "the new command wins after rebind");
}

void check_idempotent_rebind() {
    auto map = veyra::ShortcutMap{};
    map.bind({11, kCtrl}, 100);

    const veyra::ShortcutBind repeat = map.bind({11, kCtrl}, 100);
    require(repeat.result == veyra::ShortcutResult::handled, "rebinding the same pair is handled");
    require(repeat.occupier == 0, "an idempotent rebind names no occupier");
    require(map.count() == 1, "an idempotent rebind stores nothing new");
}

void check_unbind_paths() {
    auto map = veyra::ShortcutMap{};
    map.bind({11, kCtrl}, 100);
    map.bind({12, kCtrl}, 100);
    map.bind({13, kCtrl}, 200);

    require(!map.unbind({99, kCtrl}), "unbinding an absent chord reports false");
    require(!map.unbind({0, kCtrl}), "unbinding a reserved key reports false");
    require(map.count() == 3, "failed unbinds touch nothing");

    require(map.unbind({11, kCtrl}), "unbinding a held chord reports true");
    require(!map.contains({11, kCtrl}), "the unbound chord is gone");
    require(map.dispatch({12, kCtrl}).command == 100, "sibling chords survive an unbind");

    const std::size_t dropped = map.unbind_command(100);
    require(dropped == 1, "unbind_command reports how many chords went away");
    require(!map.contains({12, kCtrl}), "every chord of the command is gone");
    require(map.contains({13, kCtrl}), "other commands keep their chords");

    require(map.unbind_command(100) == 0, "repeating unbind_command for a gone command drops zero");
    require(map.unbind_command(0) == 0, "unbind_command on the reserved id drops zero");
    require(map.count() == 1, "only the unrelated binding is left");
}

void check_modifier_combinations() {
    auto map = veyra::ShortcutMap{};
    const std::uint32_t both = kCtrl | kShift;

    require(
        (map.bind({11, kCtrl}, 100).result == veyra::ShortcutResult::bound),
        "the ctrl chord binds"
    );
    require(
        (map.bind({11, both}, 200).result == veyra::ShortcutResult::bound),
        "ctrl+shift on the same key is a different chord and binds"
    );
    require(
        map.dispatch({11, both}).command == 200 && map.dispatch({11, kCtrl}).command == 100,
        "each modifier mix dispatches its own command"
    );

    require((map.bind({21, kAlt}, 300).result == veyra::ShortcutResult::bound), "alt binds");
    require((map.bind({22, kSuper}, 400).result == veyra::ShortcutResult::bound), "super binds");
    require(
        veyra::has_modifier(both, veyra::ShortcutModifier::ctrl) &&
            !veyra::has_modifier(kCtrl, veyra::ShortcutModifier::shift),
        "the modifier helpers read the mask"
    );
}

void check_reserved_and_unknown_rejected() {
    auto map = veyra::ShortcutMap{};

    const veyra::ShortcutBind no_key = map.bind({0, kCtrl}, 100);
    require(no_key.result == veyra::ShortcutResult::ignored, "key zero is rejected");
    require(
        map.bind({0, 0}, 100).result == veyra::ShortcutResult::ignored,
        "a fully empty chord is rejected"
    );
    require(
        map.bind({11, kCtrl}, 0).result == veyra::ShortcutResult::ignored,
        "command zero is rejected"
    );

    constexpr std::uint32_t kUnknown = 1U << 29U;
    const veyra::ShortcutBind unknown = map.bind({11, kUnknown}, 100);
    require(unknown.result == veyra::ShortcutResult::ignored, "unknown modifier bits are rejected");

    const veyra::ShortcutBind mixed = map.bind({11, kCtrl | kUnknown}, 100);
    require(
        mixed.result == veyra::ShortcutResult::ignored,
        "one unknown bit poisons the whole mask"
    );

    require(map.empty(), "rejected binds store nothing");
    require(!map.contains({0, kCtrl}), "contains says no to a reserved key");
    require(!map.dispatch({0, kCtrl}).has, "dispatch misses a reserved key");
    require(!map.dispatch({11, kUnknown}).has, "dispatch misses unknown modifier bits");
}

void check_modifier_only_rejected() {
    auto map = veyra::ShortcutMap{};

    require(
        map.bind({0, kShift}, 100).result == veyra::ShortcutResult::ignored,
        "a bare shift is rejected"
    );
    require(
        map.bind({0, kCtrl | kAlt}, 100).result == veyra::ShortcutResult::ignored,
        "a bare modifier mix is rejected"
    );
    require(map.empty(), "modifier-only binds store nothing");
}

void check_overflow_untouched() {
    auto map = veyra::ShortcutMap{};

    for (std::uint32_t key = 1; key <= veyra::ShortcutMap::kCapacity; ++key) {
        const veyra::ShortcutBind bound = map.bind({key, kCtrl}, static_cast<std::uint64_t>(key));
        require(bound.result == veyra::ShortcutResult::bound, "filling to capacity binds");
    }
    require(map.count() == veyra::ShortcutMap::kCapacity, "the map fills exactly to capacity");

    const veyra::ShortcutBind spill = map.bind({veyra::ShortcutMap::kCapacity + 1, kCtrl}, 9999);
    require(spill.result == veyra::ShortcutResult::overflow, "one chord past capacity overflows");
    require(spill.occupier == 0, "overflow names no occupier");
    require(map.count() == veyra::ShortcutMap::kCapacity, "overflow stores nothing");
    require(
        !map.dispatch({veyra::ShortcutMap::kCapacity + 1, kCtrl}).has,
        "the spilled chord misses"
    );

    const veyra::ShortcutBind same = map.bind({1, kCtrl}, 1);
    require(
        same.result == veyra::ShortcutResult::handled,
        "an idempotent rebind still fits when full"
    );

    const veyra::ShortcutBind clash = map.bind({1, kCtrl}, 7777);
    require(
        clash.result == veyra::ShortcutResult::conflict && clash.occupier == 1,
        "a full map still reports conflicts with the occupier"
    );
    require(map.dispatch({1, kCtrl}).command == 1, "the conflict on a full map changes nothing");
}

void check_reverse_lookup() {
    auto map = veyra::ShortcutMap{};
    map.bind({11, kCtrl}, 100);
    map.bind({12, kCtrl | kShift}, 100);
    map.bind({13, kCtrl}, 200);

    std::array<veyra::ShortcutChord, 4> buffer{};
    const std::size_t found = map.chords_for(100, buffer);
    require(found == 2, "reverse lookup finds both chords of the command");
    require(buffer[0] == veyra::ShortcutChord{11, kCtrl}, "the first chord comes back intact");
    require(
        buffer[1] == veyra::ShortcutChord{12, kCtrl | kShift},
        "the second chord comes back intact"
    );

    std::array<veyra::ShortcutChord, 1> small{};
    const std::size_t trimmed = map.chords_for(100, small);
    require(
        trimmed == 1 && small[0] == veyra::ShortcutChord{11, kCtrl},
        "a short buffer truncates to its own size"
    );

    std::array<veyra::ShortcutChord, 2> other{};
    require(map.chords_for(200, other) == 1, "a single-chord command reports one");
    require(map.chords_for(999, other) == 0, "an unbound command reports zero");
    require(map.chords_for(0, other) == 0, "the reserved command id reports zero");
}

void check_clear() {
    auto map = veyra::ShortcutMap{};
    map.bind({11, kCtrl}, 100);
    map.bind({12, kCtrl}, 200);

    map.clear();
    require(map.empty() && map.count() == 0, "clear drops every binding");
    require(!map.dispatch({11, kCtrl}).has, "cleared chords miss");

    const veyra::ShortcutBind after = map.bind({11, kCtrl}, 300);
    require(
        after.result == veyra::ShortcutResult::bound && map.dispatch({11, kCtrl}).command == 300,
        "the map binds again after clear"
    );
}

void check_sequence_two_chord_completes() {
    auto sequences = veyra::ShortcutSequence{};
    require(sequences.empty() && sequences.count() == 0, "a fresh sequence table holds nothing");
    require(!sequences.pending(), "nothing is pending before the first press");
    require(sequences.timeout_ms() == 1000, "the default gap between chords is one second");

    const std::array<veyra::ShortcutChord, 2> save{{{21, kCtrl}, {22, kCtrl}}};
    const veyra::ShortcutBind bound = sequences.bind_sequence(save, 100);
    require(bound.result == veyra::ShortcutResult::bound, "a fresh two-chord sequence binds");
    require(sequences.count() == 1, "the sequence table stores the binding");

    const veyra::SequenceHit first = sequences.press({21, kCtrl}, 0);
    require(!first.has && first.pending && !first.cancelled, "the prefix chord arms and swallows");
    require(sequences.pending(), "the table reports the armed prefix");

    const veyra::SequenceHit second = sequences.press({22, kCtrl}, 100);
    require(second.has && second.command == 100, "the completing chord names the command");
    require(!second.pending && !second.cancelled, "a completion is neither pending nor a cancel");
    require(!sequences.pending(), "a completion disarms the table");

    const veyra::SequenceHit idle = sequences.press({22, kCtrl}, 200);
    require(
        !idle.has && !idle.pending && !idle.cancelled,
        "a lone chord with no prefix misses clean"
    );
}

void check_sequence_prefix_pending_swallows_single() {
    auto sequences = veyra::ShortcutSequence{};
    auto singles = veyra::ShortcutMap{};
    const std::array<veyra::ShortcutChord, 3> save{{{21, kCtrl}, {22, kCtrl}, {23, kCtrl}}};
    require(
        sequences.bind_sequence(save, 100).result == veyra::ShortcutResult::bound,
        "save binds"
    );
    require(
        singles.bind({22, kCtrl}, 200).result == veyra::ShortcutResult::bound,
        "mid chord singles"
    );

    require(sequences.press({21, kCtrl}, 0).pending, "the first chord of three arms");
    const veyra::SequenceHit mid = sequences.press({22, kCtrl}, 100);
    require(mid.pending && !mid.has, "the middle chord continues instead of dispatching single");
    require(
        singles.dispatch({22, kCtrl}).command == 200,
        "the single table still owns the mid chord"
    );

    const veyra::SequenceHit last = sequences.press({23, kCtrl}, 200);
    require(last.has && last.command == 100, "the third chord completes the sequence");
}

void check_sequence_wrong_chord_cancels_to_single() {
    auto sequences = veyra::ShortcutSequence{};
    auto singles = veyra::ShortcutMap{};
    const std::array<veyra::ShortcutChord, 2> save{{{21, kCtrl}, {22, kCtrl}}};
    require(
        sequences.bind_sequence(save, 100).result == veyra::ShortcutResult::bound,
        "save binds"
    );
    require(
        singles.bind({24, 0}, 300).result == veyra::ShortcutResult::bound,
        "an outsider singles"
    );

    require(sequences.press({21, kCtrl}, 0).pending, "the prefix chord arms");
    const veyra::SequenceHit wrong = sequences.press({24, 0}, 50);
    require(!wrong.has && !wrong.pending && wrong.cancelled, "a wrong chord cancels back to idle");
    require(!sequences.pending(), "the cancel disarms the table");
    require(
        singles.dispatch({24, 0}).command == 300,
        "the cancelling chord still dispatches single"
    );
}

void check_sequence_timeout_cancels_silently() {
    auto sequences = veyra::ShortcutSequence{};
    const std::array<veyra::ShortcutChord, 2> save{{{21, kCtrl}, {22, kCtrl}}};
    require(
        sequences.bind_sequence(save, 100).result == veyra::ShortcutResult::bound,
        "save binds"
    );

    require(sequences.press({21, kCtrl}, 0).pending, "the prefix chord arms");
    const veyra::SequenceHit stale = sequences.press({22, kCtrl}, 2000);
    require(!stale.has && !stale.pending, "a late chord never completes the sequence");
    require(!stale.cancelled, "an expiry is silent, not a cancel");
    require(!sequences.pending(), "the stale press leaves nothing armed");

    require(sequences.press({21, kCtrl}, 3000).pending, "the prefix chord rearms");
    sequences.poll(4500);
    require(!sequences.pending(), "polling past the deadline disarms without a press");
    sequences.poll(4600);
    require(!sequences.pending(), "polling while idle is a no-op");
}

void check_sequence_continuation_refreshes_deadline() {
    auto sequences = veyra::ShortcutSequence{};
    const std::array<veyra::ShortcutChord, 3> save{{{21, kCtrl}, {22, kCtrl}, {23, kCtrl}}};
    require(
        sequences.bind_sequence(save, 100).result == veyra::ShortcutResult::bound,
        "save binds"
    );

    require(sequences.press({21, kCtrl}, 0).pending, "the first chord arms");
    require(sequences.press({22, kCtrl}, 900).pending, "a chord inside the gap continues");
    const veyra::SequenceHit last = sequences.press({23, kCtrl}, 1800);
    require(last.has && last.command == 100, "the gap restarts at every continuation");
}

void check_sequence_cancel_rearms_on_prefix() {
    auto sequences = veyra::ShortcutSequence{};
    const std::array<veyra::ShortcutChord, 2> save{{{21, kCtrl}, {22, kCtrl}}};
    require(
        sequences.bind_sequence(save, 100).result == veyra::ShortcutResult::bound,
        "save binds"
    );

    require(sequences.press({21, kCtrl}, 0).pending, "the prefix chord arms");
    const veyra::SequenceHit restart = sequences.press({21, kCtrl}, 50);
    require(restart.pending && restart.cancelled, "a fresh prefix cancels the old arm and rearms");
    require(sequences.pending(), "the table stays armed on the new prefix");

    const veyra::SequenceHit done = sequences.press({22, kCtrl}, 100);
    require(done.has && done.command == 100, "the rearmed sequence still completes");
}

void check_sequence_shared_prefix_branching() {
    auto sequences = veyra::ShortcutSequence{};
    const std::array<veyra::ShortcutChord, 2> save{{{21, kCtrl}, {22, kCtrl}}};
    const std::array<veyra::ShortcutChord, 2> quit{{{21, kCtrl}, {23, kCtrl}}};
    require(
        sequences.bind_sequence(save, 100).result == veyra::ShortcutResult::bound,
        "save binds"
    );
    require(
        sequences.bind_sequence(quit, 200).result == veyra::ShortcutResult::bound,
        "quit binds"
    );

    require(sequences.press({21, kCtrl}, 0).pending, "the shared prefix arms");
    require(sequences.press({23, kCtrl}, 10).command == 200, "the second chord picks the branch");

    require(sequences.press({21, kCtrl}, 20).pending, "the shared prefix rearms");
    require(sequences.press({22, kCtrl}, 30).command == 100, "the other branch completes too");

    require(sequences.press({21, kCtrl}, 40).pending, "the shared prefix arms again");
    const veyra::SequenceHit wrong = sequences.press({24, 0}, 50);
    require(wrong.cancelled && !wrong.has, "a chord on no branch cancels");
}

void check_sequence_ambiguous_bind_rejected() {
    auto sequences = veyra::ShortcutSequence{};
    const std::array<veyra::ShortcutChord, 2> save{{{21, kCtrl}, {22, kCtrl}}};
    const std::array<veyra::ShortcutChord, 3> longer{{{21, kCtrl}, {22, kCtrl}, {23, kCtrl}}};
    const std::array<veyra::ShortcutChord, 2> other{{{21, kCtrl}, {23, kCtrl}}};

    require(
        sequences.bind_sequence(save, 100).result == veyra::ShortcutResult::bound,
        "save binds"
    );

    const veyra::ShortcutBind grown = sequences.bind_sequence(longer, 200);
    require(grown.result == veyra::ShortcutResult::conflict, "growing a bound sequence conflicts");
    require(grown.occupier == 100, "the ambiguity names the blocking command");

    const veyra::ShortcutBind repeat = sequences.bind_sequence(save, 100);
    require(repeat.result == veyra::ShortcutResult::handled, "rebinding the same pair is handled");

    const veyra::ShortcutBind clash = sequences.bind_sequence(save, 200);
    require(clash.result == veyra::ShortcutResult::conflict, "retaking a sequence conflicts");
    require(clash.occupier == 100, "the retake names the blocking command");

    require(
        sequences.bind_sequence(other, 200).result == veyra::ShortcutResult::bound,
        "a branch binds"
    );
    require(sequences.count() == 2, "only clean binds are stored");

    auto narrow = veyra::ShortcutSequence{};
    require(narrow.bind_sequence(longer, 100).result == veyra::ShortcutResult::bound, "long binds");
    const veyra::ShortcutBind shrunk = narrow.bind_sequence(save, 200);
    require(shrunk.result == veyra::ShortcutResult::conflict, "shrinking onto a prefix conflicts");
    require(shrunk.occupier == 100, "the shrink names the blocking command");

    const std::array<veyra::ShortcutChord, 1> single{{{21, kCtrl}}};
    require(
        sequences.bind_sequence(single, 300).result == veyra::ShortcutResult::ignored,
        "a length-one bind is ignored"
    );
    require(
        sequences.bind_sequence({}, 300).result == veyra::ShortcutResult::ignored,
        "an empty bind is ignored"
    );
    require(
        sequences.bind_sequence(save, 0).result == veyra::ShortcutResult::ignored,
        "command zero is ignored"
    );

    const std::array<veyra::ShortcutChord, 2> no_key{{{0, kCtrl}, {22, kCtrl}}};
    require(
        sequences.bind_sequence(no_key, 300).result == veyra::ShortcutResult::ignored,
        "a reserved key poisons the bind"
    );

    constexpr std::uint32_t kUnknown = 1U << 29U;
    const std::array<veyra::ShortcutChord, 2> unknown{{{21, kUnknown}, {22, kCtrl}}};
    require(
        sequences.bind_sequence(unknown, 300).result == veyra::ShortcutResult::ignored,
        "an unknown modifier bit poisons the bind"
    );

    const std::array<veyra::ShortcutChord, 5> too_long{
        {{21, kCtrl}, {22, kCtrl}, {23, kCtrl}, {24, kCtrl}, {25, kCtrl}}
    };
    require(
        sequences.bind_sequence(too_long, 300).result == veyra::ShortcutResult::ignored,
        "a bind past the length cap is ignored"
    );
    require(sequences.count() == 2, "rejected binds store nothing");
}

void check_sequence_overflow_untouched() {
    auto sequences = veyra::ShortcutSequence{};

    for (std::uint32_t key = 1; key <= veyra::ShortcutSequence::kCapacity; ++key) {
        const std::array<veyra::ShortcutChord, 2> chords{{{key, kCtrl}, {200, kCtrl}}};
        const veyra::ShortcutBind bound = sequences.bind_sequence(chords, 1000 + key);
        require(bound.result == veyra::ShortcutResult::bound, "filling to capacity binds");
    }
    require(sequences.count() == veyra::ShortcutSequence::kCapacity, "the table fills exactly");

    const std::array<veyra::ShortcutChord, 2> spill{{{250, kCtrl}, {251, kCtrl}}};
    const veyra::ShortcutBind overflowed = sequences.bind_sequence(spill, 9999);
    require(
        overflowed.result == veyra::ShortcutResult::overflow,
        "one sequence past capacity overflows"
    );
    require(sequences.count() == veyra::ShortcutSequence::kCapacity, "overflow stores nothing");
    require(!sequences.press({250, kCtrl}, 0).pending, "the spilled prefix never arms");
}

void check_sequence_unbind_and_clear() {
    auto sequences = veyra::ShortcutSequence{};
    const std::array<veyra::ShortcutChord, 2> first{{{21, kCtrl}, {22, kCtrl}}};
    const std::array<veyra::ShortcutChord, 2> second{{{23, kCtrl}, {24, kCtrl}}};
    const std::array<veyra::ShortcutChord, 2> third{{{25, kCtrl}, {26, kCtrl}}};
    require(
        sequences.bind_sequence(first, 100).result == veyra::ShortcutResult::bound,
        "first binds"
    );
    require(
        sequences.bind_sequence(second, 100).result == veyra::ShortcutResult::bound,
        "second binds"
    );
    require(
        sequences.bind_sequence(third, 200).result == veyra::ShortcutResult::bound,
        "third binds"
    );

    require(sequences.unbind_sequence(first), "unbinding a held sequence reports true");
    require(!sequences.unbind_sequence(first), "repeating the unbind reports false");
    require(!sequences.press({21, kCtrl}, 0).pending, "the dropped prefix never arms");

    const std::size_t dropped = sequences.unbind_command(100);
    require(dropped == 1, "unbind_command reports how many sequences went away");
    require(sequences.unbind_command(0) == 0, "unbind_command on the reserved id drops zero");
    require(sequences.count() == 1, "only the unrelated sequence is left");

    require(sequences.press({25, kCtrl}, 0).pending, "the surviving prefix arms");
    sequences.clear();
    require(sequences.empty() && sequences.count() == 0, "clear drops every sequence");
    require(!sequences.pending(), "clear disarms a pending prefix");
    require(!sequences.press({25, kCtrl}, 10).pending, "cleared prefixes miss");
}

}

int main() {
    try {
        check_bind_dispatch_miss();
        check_conflict_names_occupier();
        check_idempotent_rebind();
        check_unbind_paths();
        check_modifier_combinations();
        check_reserved_and_unknown_rejected();
        check_modifier_only_rejected();
        check_overflow_untouched();
        check_reverse_lookup();
        check_clear();
        check_sequence_two_chord_completes();
        check_sequence_prefix_pending_swallows_single();
        check_sequence_wrong_chord_cancels_to_single();
        check_sequence_timeout_cancels_silently();
        check_sequence_continuation_refreshes_deadline();
        check_sequence_cancel_rearms_on_prefix();
        check_sequence_shared_prefix_branching();
        check_sequence_ambiguous_bind_rejected();
        check_sequence_overflow_untouched();
        check_sequence_unbind_and_clear();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
