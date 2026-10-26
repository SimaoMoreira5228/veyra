#ifndef VEYRA_CORE_SHORTCUT_SEQUENCE_HPP
#define VEYRA_CORE_SHORTCUT_SEQUENCE_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "veyra/core/shortcut_map.hpp"

namespace veyra {

struct SequenceHit {
    bool has{};
    std::uint64_t command{};
    bool pending{};
    bool cancelled{};
};

class ShortcutSequence {
  public:
    static constexpr std::size_t kCapacity = 32;
    static constexpr std::size_t kMaxLength = 4;
    static constexpr std::uint64_t kDefaultTimeoutMs = 1000;

    std::size_t count() const noexcept;
    bool empty() const noexcept;
    bool pending() const noexcept;
    std::uint64_t timeout_ms() const noexcept;
    void set_timeout_ms(std::uint64_t timeout_ms) noexcept;

    ShortcutBind
    bind_sequence(std::span<const ShortcutChord> chords, std::uint64_t command) noexcept;
    bool unbind_sequence(std::span<const ShortcutChord> chords) noexcept;
    std::size_t unbind_command(std::uint64_t command) noexcept;
    void clear() noexcept;

    bool is_prefix(ShortcutChord chord) const noexcept;

    void poll(std::uint64_t now_ms) noexcept;
    SequenceHit press(ShortcutChord chord, std::uint64_t now_ms) noexcept;

  private:
    struct SequenceEntry {
        std::array<ShortcutChord, kMaxLength> chords{};
        std::size_t length{};
        std::uint64_t command{};
    };

    static constexpr std::size_t kNotFound = static_cast<std::size_t>(-1);

    static bool is_valid(ShortcutChord chord) noexcept;
    std::size_t find_exact(std::span<const ShortcutChord> chords) const noexcept;
    std::size_t find_ambiguity(std::span<const ShortcutChord> chords) const noexcept;
    bool extends_candidate(ShortcutChord chord) const noexcept;
    std::size_t find_completion(ShortcutChord chord) const noexcept;
    bool starts_sequence(ShortcutChord chord) const noexcept;
    void disarm() noexcept;
    void remove_at(std::size_t index) noexcept;

    std::array<SequenceEntry, kCapacity> entries_{};
    std::size_t count_{};
    std::array<ShortcutChord, kMaxLength> prefix_{};
    std::size_t depth_{};
    bool armed_{};
    std::uint64_t armed_at_{};
    std::uint64_t timeout_ms_{kDefaultTimeoutMs};
};

}

#endif
