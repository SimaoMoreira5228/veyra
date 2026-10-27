#include "veyra/core/input/shortcut_map.hpp"

namespace veyra {

std::size_t ShortcutMap::count() const noexcept {
    return count_;
}

bool ShortcutMap::empty() const noexcept {
    return count_ == 0;
}

bool ShortcutMap::contains(ShortcutChord chord) const noexcept {
    return find(chord) != kNotFound;
}

ShortcutBind ShortcutMap::bind(ShortcutChord chord, std::uint64_t command) noexcept {
    if (!is_valid(chord)) {
        return {};
    }

    if (command == 0) {
        return {};
    }

    const std::size_t index = find(chord);

    if (index != kNotFound) {
        if (entries_[index].command == command) {
            return {
                .result = ShortcutResult::handled,
            };
        }

        return {
            .result = ShortcutResult::conflict,
            .occupier = entries_[index].command,
        };
    }

    if (count_ == kCapacity) {
        return {
            .result = ShortcutResult::overflow,
        };
    }

    entries_[count_].chord = chord;
    entries_[count_].command = command;
    ++count_;

    return {
        .result = ShortcutResult::bound,
    };
}

bool ShortcutMap::unbind(ShortcutChord chord) noexcept {
    const std::size_t index = find(chord);

    if (index == kNotFound) {
        return false;
    }

    remove_at(index);

    return true;
}

std::size_t ShortcutMap::unbind_command(std::uint64_t command) noexcept {
    if (command == 0) {
        return 0;
    }

    std::size_t removed = 0;
    std::size_t index = 0;

    while (index < count_) {
        if (entries_[index].command != command) {
            ++index;
            continue;
        }

        remove_at(index);
        ++removed;
    }

    return removed;
}

void ShortcutMap::clear() noexcept {
    count_ = 0;
    entries_ = {};
}

ShortcutHit ShortcutMap::dispatch(ShortcutChord chord) const noexcept {
    const std::size_t index = find(chord);

    if (index == kNotFound) {
        return {};
    }

    return {
        .has = true,
        .command = entries_[index].command,
    };
}

std::size_t
ShortcutMap::chords_for(std::uint64_t command, std::span<ShortcutChord> output) const noexcept {
    if (command == 0) {
        return 0;
    }

    std::size_t copied = 0;

    for (std::size_t i = 0; i < count_; ++i) {
        if (entries_[i].command != command) {
            continue;
        }

        if (copied == output.size()) {
            break;
        }

        output[copied] = entries_[i].chord;
        ++copied;
    }

    return copied;
}

bool ShortcutMap::is_valid(ShortcutChord chord) noexcept {
    if (chord.key == 0) {
        return false;
    }

    if ((chord.modifiers & ~kValidModifiers) != 0) {
        return false;
    }

    return true;
}

std::size_t ShortcutMap::find(ShortcutChord chord) const noexcept {
    if (!is_valid(chord)) {
        return kNotFound;
    }

    for (std::size_t i = 0; i < count_; ++i) {
        if (entries_[i].chord == chord) {
            return i;
        }
    }

    return kNotFound;
}

void ShortcutMap::remove_at(std::size_t index) noexcept {
    entries_[index] = entries_[count_ - 1];
    entries_[count_ - 1] = {};
    --count_;
}

}
