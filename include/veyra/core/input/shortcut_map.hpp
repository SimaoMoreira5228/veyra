#ifndef VEYRA_CORE_SHORTCUT_MAP_HPP
#define VEYRA_CORE_SHORTCUT_MAP_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace veyra {

enum class ShortcutModifier : std::uint8_t {
    ctrl = 1U << 0U,
    alt = 1U << 1U,
    shift = 1U << 2U,
    super = 1U << 3U,
};

constexpr std::uint32_t operator|(ShortcutModifier left, ShortcutModifier right) noexcept {
    return static_cast<std::uint32_t>(left) | static_cast<std::uint32_t>(right);
}

constexpr std::uint32_t operator|(std::uint32_t left, ShortcutModifier right) noexcept {
    return left | static_cast<std::uint32_t>(right);
}

constexpr std::uint32_t modifier_flag(ShortcutModifier modifier) noexcept {
    return static_cast<std::uint32_t>(modifier);
}

constexpr bool has_modifier(std::uint32_t mask, ShortcutModifier modifier) noexcept {
    return (mask & static_cast<std::uint32_t>(modifier)) != 0;
}

struct ShortcutChord {
    std::uint32_t key{};
    std::uint32_t modifiers{};

    bool operator==(const ShortcutChord& other) const noexcept = default;
};

enum class ShortcutResult : std::uint8_t {
    ignored,
    handled,
    bound,
    conflict,
    overflow,
};

struct ShortcutBind {
    ShortcutResult result{ShortcutResult::ignored};
    std::uint64_t occupier{};
};

struct ShortcutHit {
    bool has{};
    std::uint64_t command{};
};

class ShortcutMap {
  public:
    static constexpr std::size_t kCapacity = 128;

    std::size_t count() const noexcept;
    bool empty() const noexcept;
    bool contains(ShortcutChord chord) const noexcept;

    ShortcutBind bind(ShortcutChord chord, std::uint64_t command) noexcept;
    bool unbind(ShortcutChord chord) noexcept;
    std::size_t unbind_command(std::uint64_t command) noexcept;
    void clear() noexcept;

    ShortcutHit dispatch(ShortcutChord chord) const noexcept;
    std::size_t chords_for(std::uint64_t command, std::span<ShortcutChord> output) const noexcept;

  private:
    struct ShortcutEntry {
        ShortcutChord chord{};
        std::uint64_t command{};
    };

    static constexpr std::size_t kNotFound = static_cast<std::size_t>(-1);

    static constexpr std::uint32_t kValidModifiers =
        static_cast<std::uint32_t>(ShortcutModifier::ctrl) |
        static_cast<std::uint32_t>(ShortcutModifier::alt) |
        static_cast<std::uint32_t>(ShortcutModifier::shift) |
        static_cast<std::uint32_t>(ShortcutModifier::super);

    static bool is_valid(ShortcutChord chord) noexcept;
    std::size_t find(ShortcutChord chord) const noexcept;
    void remove_at(std::size_t index) noexcept;

    std::array<ShortcutEntry, kCapacity> entries_{};
    std::size_t count_{};
};

}

#endif
