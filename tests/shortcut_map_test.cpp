#include "veyra/core/shortcut_map.hpp"

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
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
