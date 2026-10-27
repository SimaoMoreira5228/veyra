#include "veyra/core/input/shortcut_sequence.hpp"

namespace veyra {

std::size_t ShortcutSequence::count() const noexcept {
    return count_;
}

bool ShortcutSequence::empty() const noexcept {
    return count_ == 0;
}

bool ShortcutSequence::pending() const noexcept {
    return armed_;
}

std::uint64_t ShortcutSequence::timeout_ms() const noexcept {
    return timeout_ms_;
}

void ShortcutSequence::set_timeout_ms(std::uint64_t timeout_ms) noexcept {
    timeout_ms_ = timeout_ms;
}

ShortcutBind ShortcutSequence::bind_sequence(
    std::span<const ShortcutChord> chords,
    std::uint64_t command
) noexcept {
    if (command == 0) {
        return {};
    }

    if (chords.size() < 2 || chords.size() > kMaxLength) {
        return {};
    }

    for (const ShortcutChord chord : chords) {
        if (!is_valid(chord)) {
            return {};
        }
    }

    const std::size_t exact = find_exact(chords);

    if (exact != kNotFound) {
        if (entries_[exact].command == command) {
            return {
                .result = ShortcutResult::handled,
            };
        }

        return {
            .result = ShortcutResult::conflict,
            .occupier = entries_[exact].command,
        };
    }

    const std::size_t clash = find_ambiguity(chords);

    if (clash != kNotFound) {
        return {
            .result = ShortcutResult::conflict,
            .occupier = entries_[clash].command,
        };
    }

    if (count_ == kCapacity) {
        return {
            .result = ShortcutResult::overflow,
        };
    }

    SequenceEntry entry{};

    for (std::size_t i = 0; i < chords.size(); ++i) {
        entry.chords[i] = chords[i];
    }

    entry.length = chords.size();
    entry.command = command;
    entries_[count_] = entry;
    ++count_;

    return {
        .result = ShortcutResult::bound,
    };
}

bool ShortcutSequence::unbind_sequence(std::span<const ShortcutChord> chords) noexcept {
    const std::size_t index = find_exact(chords);

    if (index == kNotFound) {
        return false;
    }

    remove_at(index);

    return true;
}

std::size_t ShortcutSequence::unbind_command(std::uint64_t command) noexcept {
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

void ShortcutSequence::clear() noexcept {
    count_ = 0;
    entries_ = {};
    disarm();
}

bool ShortcutSequence::is_prefix(ShortcutChord chord) const noexcept {
    return starts_sequence(chord);
}

void ShortcutSequence::poll(std::uint64_t now_ms) noexcept {
    if (armed_ && now_ms - armed_at_ >= timeout_ms_) {
        disarm();
    }
}

SequenceHit ShortcutSequence::press(ShortcutChord chord, std::uint64_t now_ms) noexcept {
    poll(now_ms);

    bool cancelled = false;

    if (armed_) {
        const std::size_t done = find_completion(chord);

        if (done != kNotFound) {
            const std::uint64_t command = entries_[done].command;
            disarm();

            return {
                .has = true,
                .command = command,
            };
        }

        if (extends_candidate(chord)) {
            prefix_[depth_] = chord;
            ++depth_;
            armed_at_ = now_ms;

            return {
                .pending = true,
            };
        }

        disarm();
        cancelled = true;
    }

    if (starts_sequence(chord)) {
        prefix_[0] = chord;
        depth_ = 1;
        armed_ = true;
        armed_at_ = now_ms;

        return {
            .pending = true,
            .cancelled = cancelled,
        };
    }

    return {
        .cancelled = cancelled,
    };
}

bool ShortcutSequence::is_valid(ShortcutChord chord) noexcept {
    if (chord.key == 0) {
        return false;
    }

    constexpr std::uint32_t kValid = static_cast<std::uint32_t>(ShortcutModifier::ctrl) |
        static_cast<std::uint32_t>(ShortcutModifier::alt) |
        static_cast<std::uint32_t>(ShortcutModifier::shift) |
        static_cast<std::uint32_t>(ShortcutModifier::super);

    return (chord.modifiers & ~kValid) == 0;
}

std::size_t ShortcutSequence::find_exact(std::span<const ShortcutChord> chords) const noexcept {
    for (std::size_t i = 0; i < count_; ++i) {
        if (entries_[i].length != chords.size()) {
            continue;
        }

        bool same = true;

        for (std::size_t j = 0; j < chords.size(); ++j) {
            if (!(entries_[i].chords[j] == chords[j])) {
                same = false;
                break;
            }
        }

        if (same) {
            return i;
        }
    }

    return kNotFound;
}

std::size_t ShortcutSequence::find_ambiguity(std::span<const ShortcutChord> chords) const noexcept {
    for (std::size_t i = 0; i < count_; ++i) {
        std::size_t shared = entries_[i].length;

        if (chords.size() < shared) {
            shared = chords.size();
        }

        bool same = true;

        for (std::size_t j = 0; j < shared; ++j) {
            if (!(entries_[i].chords[j] == chords[j])) {
                same = false;
                break;
            }
        }

        if (same && entries_[i].length != chords.size()) {
            return i;
        }
    }

    return kNotFound;
}

bool ShortcutSequence::extends_candidate(ShortcutChord chord) const noexcept {
    for (std::size_t i = 0; i < count_; ++i) {
        if (entries_[i].length <= depth_ + 1) {
            continue;
        }

        bool same = true;

        for (std::size_t j = 0; j < depth_; ++j) {
            if (!(entries_[i].chords[j] == prefix_[j])) {
                same = false;
                break;
            }
        }

        if (same && entries_[i].chords[depth_] == chord) {
            return true;
        }
    }

    return false;
}

std::size_t ShortcutSequence::find_completion(ShortcutChord chord) const noexcept {
    for (std::size_t i = 0; i < count_; ++i) {
        if (entries_[i].length != depth_ + 1) {
            continue;
        }

        bool same = true;

        for (std::size_t j = 0; j < depth_; ++j) {
            if (!(entries_[i].chords[j] == prefix_[j])) {
                same = false;
                break;
            }
        }

        if (same && entries_[i].chords[depth_] == chord) {
            return i;
        }
    }

    return kNotFound;
}

bool ShortcutSequence::starts_sequence(ShortcutChord chord) const noexcept {
    for (std::size_t i = 0; i < count_; ++i) {
        if (entries_[i].chords[0] == chord) {
            return true;
        }
    }

    return false;
}

void ShortcutSequence::disarm() noexcept {
    armed_ = false;
    depth_ = 0;
    prefix_ = {};
}

void ShortcutSequence::remove_at(std::size_t index) noexcept {
    entries_[index] = entries_[count_ - 1];
    entries_[count_ - 1] = {};
    --count_;
}

}
